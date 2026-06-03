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
    """
    Quantize the angle between thumb tip and index tip (relative to the thumb base)
    into 0..NUM_REGIONS-1.
    """
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
