import cv2
import mediapipe as mp
import numpy as np
from dataclasses import dataclass, field


SMOOTH_K = 0.18

_PEACE_CONFIRM_FRAMES  = 8
_PEACE_COOLDOWN_FRAMES = 20

# MediaPipe hand connections (pairs of landmark indices)
# Same list the library uses internally, duplicated here so ui.py
# can draw skeletons without importing mediapipe directly.
HAND_CONNECTIONS = [
    (0,1),(1,2),(2,3),(3,4),       # thumb
    (0,5),(5,6),(6,7),(7,8),       # index
    (0,9),(9,10),(10,11),(11,12),  # middle
    (0,13),(13,14),(14,15),(15,16),# ring
    (0,17),(17,18),(18,19),(19,20),# pinky
    (5,9),(9,13),(13,17),          # palm knuckle line
]


@dataclass
class GestureState:
    hand_y:         float = 0.5
    is_fist:        bool  = False
    delay_time_val: float = 0.1
    delay_feedback: float = 0.0
    delay_locked:   bool  = False
    # Raw (pre-smooth) values
    _raw_delay_time:    float = 0.1
    _raw_delay_feedback: float = 0.0
    _raw_hand_y:        float = 0.5
    # Raw landmark lists for skeleton drawing — each entry is a list of
    # (x, y) normalised coords [0..1], or None if hand not detected.
    # Index 0 = first detected hand, index 1 = second.
    # Each list has label ('Left'/'Right') + landmarks.
    hand_landmarks: list = field(default_factory=list)

def _is_peace(lms) -> bool:
    """
    Index (8) and middle (12) tips above their PIP joints → extended.
    Ring (16), pinky (20) tips below their PIPs              → curled.
    Thumb tip (4) not too close to index tip (8)             → not pinching.

    MediaPipe y increases downward, so tip.y < pip.y means the finger
    is pointing upward (extended).
    """
    index_up  = lms[8].y  < lms[6].y   # index tip above PIP
    middle_up = lms[12].y < lms[10].y  # middle tip above PIP
    ring_down = lms[16].y > lms[14].y  # ring curled
    pinky_down = lms[20].y > lms[18].y # pinky curled
    # Thumb not pinching index (avoids misfiring during pinch on right hand)
    not_pinching = np.hypot(lms[4].x - lms[8].x, lms[4].y - lms[8].y) > 0.06

    return index_up and middle_up and ring_down and pinky_down and not_pinching


class HandTracker:
    def __init__(self):
        self._mp_hands = mp.solutions.hands
        self._hands = self._mp_hands.Hands(
            max_num_hands=2,
            min_detection_confidence=0.7,
            min_tracking_confidence=0.6,
        )
        self.state = GestureState()

        # Peace sign debounce state (left hand)
        self._peace_count    = 0   # consecutive frames peace sign is held
        self._peace_cooldown = 0   # frames remaining before we can fire again

    def process(self, bgr_frame: np.ndarray) -> tuple[np.ndarray, GestureState]:
        """
        Process a BGR camera frame.
        Returns (rgb_frame, updated GestureState).
        """
        rgb = cv2.cvtColor(bgr_frame, cv2.COLOR_BGR2RGB)
        results = self._hands.process(rgb)
        s = self.state

        # Reset landmarks each frame so stale data doesn't linger
        s.hand_landmarks = []

        if results.multi_hand_landmarks:
            # Store raw landmarks for skeleton drawing
            for i, hand_lms in enumerate(results.multi_hand_landmarks):
                label = results.multi_handedness[i].classification[0].label
                pts   = [(lm.x, lm.y) for lm in hand_lms.landmark]
                s.hand_landmarks.append({"label": label, "pts": pts})

            for i, hand_lms in enumerate(results.multi_hand_landmarks):
                label = results.multi_handedness[i].classification[0].label
                lms = hand_lms.landmark

                if label == "Right":
                    # Index tip height → filter cutoff
                    raw_y = lms[8].y
                    s._raw_hand_y = raw_y
                    s.hand_y = s.hand_y + SMOOTH_K * (raw_y - s.hand_y)
                    # Pinch (thumb ↔ index tip) → bit-crush toggle
                    pinch_dist = np.hypot(lms[4].x - lms[8].x, lms[4].y - lms[8].y)
                    s.is_fist = pinch_dist < 0.04

                else:  # Left hand
                    # ---- Peace sign → toggle delay lock ----
                    if self._peace_cooldown > 0:
                        # In cooldown: drain counter, don't register gesture
                        self._peace_cooldown -= 1
                        self._peace_count = 0
                    else:
                        if _is_peace(lms):
                            self._peace_count += 1
                        else:
                            self._peace_count = 0

                        if self._peace_count >= _PEACE_CONFIRM_FRAMES:
                            s.delay_locked = not s.delay_locked   # toggle
                            self._peace_count    = 0
                            self._peace_cooldown = _PEACE_COOLDOWN_FRAMES

                    # Wrist height → delay feedback
                    raw_fb = np.clip(1.1 - lms[0].y * 1.3, 0.0, 0.85)
                    s._raw_delay_feedback = raw_fb
                    s.delay_feedback = s.delay_feedback + SMOOTH_K * (raw_fb - s.delay_feedback)

                    # Hand tilt → delay time (only when unlocked)
                    if not s.delay_locked:
                        dy = abs(lms[5].y - lms[17].y)
                        raw_dt = np.clip(dy * 4.0, 0.05, 0.8)
                        s._raw_delay_time = raw_dt
                        s.delay_time_val = s.delay_time_val + SMOOTH_K * (raw_dt - s.delay_time_val)

        return rgb, s

    def release(self):
        self._hands.close()