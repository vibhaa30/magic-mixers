import math
import time
from collections import deque
import numpy as np
import cv2
import mediapipe as mp
import spidev
from picamera2 import Picamera2
from latency_measure import LatencyProfiler

# MediaPipe landmark indices for fingertips
THUMB_TIP  = 4
INDEX_TIP  = 8
MIDDLE_TIP = 12
RING_TIP   = 16
PINKY_TIP  = 20

# Amplitude tuning
ANGLE_MAX        = 45      # max thumb/index opening angle in degrees (empirical)
NUM_REGIONS      = 256     # quantization levels for amplitude
SMOOTHING_WINDOW = 10      # larger = smoother but more lag

# Camera
FRAME_WIDTH  = 640
FRAME_HEIGHT = 480

# Feature names (index 0 = fist/none, 1-4 = effects)
FEATURE_NAMES = ["None", "Volume", "Chorus", "LPF", "Pitch"]

# SPI
SPI_BUS    = 0
SPI_DEVICE = 0
SPI_SPEED  = 1_000_000
SPI_MODE   = 0b00

mp_hands = mp.solutions.hands
mp_draw  = mp.solutions.drawing_utils
hands    = mp_hands.Hands(max_num_hands=2, min_detection_confidence=0.7)

picam2 = Picamera2()
picam2.configure(
    picam2.create_preview_configuration(
        main={"format": "RGB888", "size": (FRAME_WIDTH, FRAME_HEIGHT)}
    )
)
picam2.start()

spi = spidev.SpiDev()
spi.open(SPI_BUS, SPI_DEVICE)
spi.mode = SPI_MODE
spi.max_speed_hz = SPI_SPEED

profiler = LatencyProfiler(spi, gpio_echo_pin=None)

def is_finger_extended(landmarks, tip_id):
    """True if a non-thumb finger is extended (tip above the PIP joint in image space)."""
    tip = landmarks.landmark[tip_id]
    pip = landmarks.landmark[tip_id - 2]
    return tip.y < pip.y


def is_thumb_extended(landmarks, tip_id=THUMB_TIP):
    """True if the thumb is extended outward (works for a mirrored right-hand-as-left view)."""
    tip = landmarks.landmark[tip_id]
    ip  = landmarks.landmark[tip_id - 2]
    return tip.x > ip.x


def left_hand_feature(landmarks):
    """
    Map extended-finger count (index outward) to a feature 0-4.

        0 -> fist (no fingers up)
        1 -> index
        2 -> index + middle
        3 -> index + middle + ring
        4 -> index + middle + ring + pinky

    Thumb is ignored for selection so the user can rest it naturally.
    """
    index  = is_finger_extended(landmarks, INDEX_TIP)
    middle = is_finger_extended(landmarks, MIDDLE_TIP)
    ring   = is_finger_extended(landmarks, RING_TIP)
    pinky  = is_finger_extended(landmarks, PINKY_TIP)

    if index and middle and ring and pinky:
        return 4
    if index and middle and ring:
        return 3
    if index and middle:
        return 2
    if index:
        return 1
    return 0


def right_hand_amplitude(landmarks):
    index_tip  = landmarks.landmark[INDEX_TIP]
    thumb_tip  = landmarks.landmark[THUMB_TIP]
    thumb_base = landmarks.landmark[1]

    # "opposite" side: 2D distance between index tip and thumb tip
    dist_opp = math.dist(
        (index_tip.x, index_tip.y),
        (thumb_tip.x, thumb_tip.y),
    )

    # "hypotenuse": 3D distance from index tip to thumb base
    dist_hyp = math.dist(
        (index_tip.x, index_tip.y, index_tip.z),
        (thumb_base.x, thumb_base.y, thumb_base.z),
    )

    # Guard against divide-by-zero and out-of-domain values
    if dist_hyp == 0:
        return 0
    ratio = min(dist_opp / dist_hyp, 1.0)
    angle = math.degrees(math.asin(ratio))

    amplitude = math.ceil(angle / (ANGLE_MAX / NUM_REGIONS)) - 1
    return max(0, min(NUM_REGIONS - 1, amplitude))


def spi_send(feature, intensity):
    spi.xfer([int(feature), int(intensity)])

amplitude_buffer = deque(maxlen=SMOOTHING_WINDOW)
current_feature  = 0
prev_feature     = -1       
prev_amplitude   = -1
AMPLITUDE_DEAD_ZONE = 2     

try:
    while True:
        frame  = picam2.capture_array()
        profiler.tick_frame_start()   # T0: frame just arrived from sensor
        frame  = cv2.flip(frame, 1)
        
        # MediaPipe expects RGB, which Picamera2 is already providing.
        result = hands.process(frame)

        left_detected  = False
        right_detected = False

        if result.multi_hand_landmarks and result.multi_handedness:
            for hand_lms, handedness in zip(
                result.multi_hand_landmarks, result.multi_handedness
            ):
                label = handedness.classification[0].label
                mp_draw.draw_landmarks(frame, hand_lms, mp_hands.HAND_CONNECTIONS)

                if label == "Left":
                    current_feature = left_hand_feature(hand_lms)
                    left_detected   = True
                elif label == "Right":
                    amplitude_buffer.append(right_hand_amplitude(hand_lms))
                    right_detected  = True

        if not amplitude_buffer:
            smoothed_amplitude = 0
        else:
            smoothed_amplitude = round(sum(amplitude_buffer) / len(amplitude_buffer))

        feature_changed   = left_detected  and (current_feature != prev_feature)
        amplitude_changed = right_detected and (abs(smoothed_amplitude - prev_amplitude) > AMPLITUDE_DEAD_ZONE)

        if feature_changed or amplitude_changed:
            profiler.tick_frame_end()   # T1: CV decision done, about to send
            profiler.spi_send(current_feature, smoothed_amplitude)   # times the xfer
            prev_feature   = current_feature
            prev_amplitude = smoothed_amplitude

            # Rolling report every 100 SPI sends
            if profiler.send_count % 100 == 0:
                profiler.report(f"Rolling report @ send #{profiler.send_count}")
                
        disp_frame = frame.copy()
        h, w = disp_frame.shape[:2]

        # Optimized Translucent Panel
        # Extract only the 290x70 Region of Interest (ROI) instead of blending the whole 640x480 frame
        x1, y1, x2, y2 = 10, 10, 300, 80
        roi = disp_frame[y1:y2, x1:x2]
        
        # Create a dark gray block exactly the size of the ROI and blend it
        dark_panel = np.full(roi.shape, (20, 20, 20), dtype=np.uint8)
        cv2.addWeighted(dark_panel, 0.5, roi, 0.5, 0, roi) 

        # HUD Text
        effect_name = FEATURE_NAMES[current_feature]
        active = current_feature != 0
        name_color = (0, 255, 0) if active else (160, 160, 160)

        cv2.putText(disp_frame, f"Effect: {effect_name}", (22, 42),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, name_color, 2, cv2.LINE_AA)
        cv2.putText(disp_frame, f"Level:  {smoothed_amplitude}", (22, 70),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2, cv2.LINE_AA)

        # 4. Vertical Intensity Bar 
        bar_w      = 28
        bar_margin = 30
        bar_top    = 40
        bar_bottom = h - 40
        bar_x      = w - bar_margin - bar_w
        bar_h      = bar_bottom - bar_top

        # Clamp fraction strictly between 0.0 and 1.0 to prevent crash loops
        frac      = max(0.0, min(1.0, smoothed_amplitude / (NUM_REGIONS - 1)))
        fill_h    = int(frac * bar_h)
        fill_top  = bar_bottom - fill_h

        # OpenCV BGR Color gradient: Green (0,255,0) -> Yellow (0,255,255) -> Red (0,0,255)
        if frac < 0.5:
            fill_color = (0, 255, int(255 * (frac / 0.5)))
        else:
            fill_color = (0, int(255 * (1 - (frac - 0.5) / 0.5)), 255)

        # Track background
        cv2.rectangle(disp_frame, (bar_x, bar_top), (bar_x + bar_w, bar_bottom), (60, 60, 60), -1)
        
        # Filled portion
        if fill_h > 0:
            cv2.rectangle(disp_frame, (bar_x, fill_top), (bar_x + bar_w, bar_bottom), fill_color, -1)
            
        # Border
        cv2.rectangle(disp_frame, (bar_x, bar_top), (bar_x + bar_w, bar_bottom), (220, 220, 220), 2)

        # Tick marks at 0/25/50/75/100%
        for t in range(5):
            ty = bar_bottom - int(t / 4 * bar_h)
            cv2.line(disp_frame, (bar_x - 6, ty), (bar_x, ty), (220, 220, 220), 1)

        # Label above the bar
        cv2.putText(disp_frame, "LVL", (bar_x - 4, bar_top - 12),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (220, 220, 220), 1, cv2.LINE_AA)

        # Show the correctly formatted BGR frame
        cv2.imshow("Output", disp_frame)

        if cv2.waitKey(1) == ord('q'):
            break

finally:
    profiler.report("=== FINAL LATENCY SUMMARY ===")
    profiler.cleanup()
    picam2.stop()
    cv2.destroyAllWindows()
    spi.close()
