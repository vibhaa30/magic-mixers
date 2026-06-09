"""
Hand-tracking controller.

Left hand  -> selects a feature (0-4) based on how many fingers are extended.
Right hand -> controls amplitude via the thumb/index pinch angle.

Both values are sent over SPI as a pair of bytes: [feature, amplitude].
"""

import math
from collections import deque

import cv2
import mediapipe as mp
import spidev
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# MediaPipe landmark indices for fingertips
THUMB_BASE = 1
THUMB_TIP  = 4
INDEX_TIP  = 8
MIDDLE_TIP = 12
RING_TIP   = 16
PINKY_TIP  = 20

# Amplitude tuning
ANGLE_MAX = math.radians(60)      # max thumb/index opening angle in radians
NUM_REGIONS      = 256     # quantization levels for amplitude
SMOOTHING_WINDOW = 10      # larger = smoother but more lag

# Camera
FRAME_WIDTH  = 640
FRAME_HEIGHT = 480

# SPI
SPI_BUS    = 0
SPI_DEVICE = 0
SPI_SPEED  = 1_000_000
SPI_MODE   = 0b00


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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

        1 -> index
        2 -> index + middle
        3 -> index + middle + ring
        4 -> index + middle + ring + pinky

        Any other finger combination is treated as zero/not a recognized number

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
    """
    Quantize the angle between thumb tip and index tip (relative to the thumb base)
    into 0..NUM_REGIONS-1.
    """
    index_tip  = landmarks.landmark[INDEX_TIP]
    thumb_tip  = landmarks.landmark[THUMB_TIP]
    thumb_base = landmarks.landmark[THUMB_BASE]

    point_a = index_tip
    point_b = thumb_base
    point_c = thumb_tip

    #---Find angle b---#

    #Get side lengths

    #dist btwn thumb tip and base
    side_A = math.dist(
        (thumb_base.x, thumb_base.y),
        (thumb_tip.x, thumb_tip.y),
    )

    #dist btwn thumb and index tips
    side_B = math.dist(
        (index_tip.x, index_tip.y),
        (thumb_tip.x, thumb_tip.y),
    )

    #dist btwn index and thumb base
    side_C = math.dist(
        (index_tip.x, index_tip.y),
        (thumb_base.x, thumb_base.y),
    )

    if (not(side_A and side_B and side_C)):
        angle_a = angle_b = angle_c = 0
        print("zero length side -- angles = 0")
    else:
        angle_a = math.acos((side_B**2 + side_C**2 - side_A**2) / (2 * side_B * side_C))
        angle_b = math.acos((side_A**2 + side_C**2 - side_B**2) / (2 * side_A * side_C))
        angle_c = math.acos((side_A**2 + side_B**2 - side_C**2) / (2 * side_A * side_B))

        print(angle_a)
        print(angle_b)
        print(angle_c)
        print(angle_a + angle_b + angle_c)
        if not math.isclose(angle_a + angle_b + angle_c, math.pi, abs_tol=1e-4):
            raise ValueError("Math Error")
    

    amplitude = math.ceil(angle_b / (ANGLE_MAX / NUM_REGIONS)) - 1
    return max(0, min(NUM_REGIONS - 1, amplitude))


def spi_send(feature, intensity):
    spi.xfer([int(feature), int(intensity)])


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

amplitude_buffer = deque(maxlen=SMOOTHING_WINDOW)
current_feature  = 0

try:
    while True:
        frame  = picam2.capture_array()
        frame  = cv2.flip(frame, 1)
        result = hands.process(frame)

        feature   = 0
        amplitude = 0

        if result.multi_hand_landmarks and result.multi_handedness:
            for hand_lms, handedness in zip(
                result.multi_hand_landmarks, result.multi_handedness
            ):
                label = handedness.classification[0].label
                mp_draw.draw_landmarks(frame, hand_lms, mp_hands.HAND_CONNECTIONS)

                if label == "Left":
                    feature = left_hand_feature(hand_lms)
                    current_feature = feature
                elif label == "Right":
                    amplitude = right_hand_amplitude(hand_lms)

        # Smooth amplitude over a rolling window
        amplitude_buffer.append(amplitude)
        smoothed_amplitude = round(sum(amplitude_buffer) / len(amplitude_buffer))

        spi_send(feature, smoothed_amplitude)

        # HUD
        cv2.putText(
            frame,
            f"feat:{current_feature} amp:{smoothed_amplitude}",
            (10, 50),
            cv2.FONT_HERSHEY_SIMPLEX,
            1,
            (0, 0, 255),
            2,
            cv2.LINE_AA,
        )
        cv2.imshow("Output", frame)

        if cv2.waitKey(1) == ord('q'):
            break

finally:
    picam2.stop()
    cv2.destroyAllWindows()
    spi.close()