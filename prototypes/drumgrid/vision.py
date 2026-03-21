import cv2
import mediapipe as mp
import numpy as np
from dataclasses import dataclass


SMOOTH_K = 0.18   # EMA smoothing for all gesture values

# Peace sign: index + middle extended, ring + pinky + thumb curled.
# Must hold for this many frames to confirm the gesture, then the
# counter resets so it won't re-fire until you drop and re-raise it.
_PEACE_CONFIRM_FRAMES = 8

# After a toggle fires, ignore further peace signs for this many frames.
# Stops a single held peace sign from toggling repeatedly.
_PEACE_COOLDOWN_FRAMES = 20


@dataclass
class GestureState:
    hand_y: float = 0.5
    is_fist: bool = False
    delay_time_val: float = 0.1
    delay_feedback: float = 0.0
    delay_locked: bool = False
    # Raw (pre-smooth) versions for immediate UI feedback
    _raw_delay_time: float = 0.1
    _raw_delay_feedback: float = 0.0
    _raw_hand_y: float = 0.5


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

        if results.multi_hand_landmarks:
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