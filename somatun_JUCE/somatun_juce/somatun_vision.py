"""
somatun_vision.py  - unified vision helper for SOMATUN
=======================================================
Sends data over two channels:

  OSC  port 9000  →  /pose   (33 × 2 floats, normalised x/y)
                     /hands  (see layout below)
  TCP  port 9001  →  JPEG frames (4-byte big-endian length prefix)

/hands OSC layout
-----------------
  [0]           = numHands  (float cast of int, 0‥2)
  per hand block  (stride = 1 + 21*2 = 43 floats):
    [base + 0]      = label   (0.0 = Left, 1.0 = Right, mirror-corrected)
    [base + 1..42]  = lm[0].x, lm[0].y, ..., lm[20].x, lm[20].y  (normalised)
"""

import cv2
import mediapipe as mp
from pythonosc import udp_client
import numpy as np
import socket
import struct
import threading

# ── Ports ────────────────────────────────────────────────────
OSC_IP    = "127.0.0.1"
OSC_PORT  = 9000
VIDEO_PORT = 9001
JPEG_QUALITY = 60

# ── MediaPipe ────────────────────────────────────────────────
mp_pose    = mp.solutions.pose
mp_hands   = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils

# ── Colour scheme (BGR) ──────────────────────────────────────
COLORS = {
    'pose_joints':       (50,  50,  255),
    'pose_joints_core':  (10,  10,  180),
    'pose_joints_line':  (40,  40,  200),
    'pose_ears':         (30,  30,  220),
    'pose_ears_ring':    (0,   0,   120),
    # Dualcast: left hand = blue-ish, right hand = green-ish
    'hand_left_tip':     (255, 140, 60),
    'hand_left_conn':    (180, 90,  40),
    'hand_right_tip':    (80,  210, 80),
    'hand_right_conn':   (40,  140, 40),
}

# FleshSynth landmark indices
FLESHSYNTH_JOINTS = [15, 13, 11, 12, 14, 16]
FLESHSYNTH_EARS   = [7, 8]

# MediaPipe hand landmark indices
HAND_TIPS         = [4, 8, 12, 16, 20]
HAND_CONNECTIONS  = [
    (0,1),(1,2),(2,3),(3,4),
    (0,5),(5,6),(6,7),(7,8),
    (0,9),(9,10),(10,11),(11,12),
    (0,13),(13,14),(14,15),(15,16),
    (0,17),(17,18),(18,19),(19,20),
    (5,9),(9,13),(13,17),
]


# ============================================================
#  TCP video server
# ============================================================
class VideoServer:
    """Accepts a single TCP connection and streams length-prefixed JPEG frames."""

    def __init__(self, port: int):
        self._lock   = threading.Lock()
        self._client = None

        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", port))
        self._server.listen(1)
        self._server.settimeout(0.5)

        self._running = True
        self._thread  = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        print(f"[VideoServer] listening on port {port}")

    def _accept_loop(self):
        while self._running:
            try:
                conn, addr = self._server.accept()
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                print(f"[VideoServer] client connected from {addr}")
                with self._lock:
                    if self._client:
                        try: self._client.close()
                        except: pass
                    self._client = conn
            except socket.timeout:
                pass
            except Exception as e:
                if self._running:
                    print(f"[VideoServer] accept error: {e}")

    def send_frame(self, bgr_image):
        with self._lock:
            if self._client is None:
                return
            client = self._client

        ok, buf = cv2.imencode(".jpg", bgr_image, [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])
        if not ok:
            return
        data   = buf.tobytes()
        header = struct.pack(">I", len(data))
        try:
            client.sendall(header + data)
        except Exception:
            with self._lock:
                self._client = None

    def close(self):
        self._running = False
        self._server.close()


# ============================================================
#  Drawing helpers
# ============================================================
def draw_pose_landmarks(image, landmarks, width, height):
    """Draw FleshSynth arm joints + ears."""
    if not landmarks:
        return

    joint_points = []
    for idx in FLESHSYNTH_JOINTS:
        lm = landmarks.landmark[idx]
        x, y = int(lm.x * width), int(lm.y * height)
        cv2.circle(image, (x, y), 10, COLORS['pose_joints'],      -1)
        cv2.circle(image, (x, y),  5, COLORS['pose_joints_core'], -1)
        joint_points.append((x, y))

    if len(joint_points) == 6:
        pts = np.array(joint_points, np.int32)
        cv2.polylines(image, [pts], False, COLORS['pose_joints_line'], 2)

    ear_points = []
    for idx in FLESHSYNTH_EARS:
        lm = landmarks.landmark[idx]
        x, y = int(lm.x * width), int(lm.y * height)
        cv2.circle(image, (x, y), 10, COLORS['pose_ears'],      -1)
        cv2.circle(image, (x, y), 13, COLORS['pose_ears_ring'],  2)
        ear_points.append((x, y))

    if len(ear_points) == 2:
        cv2.line(image, ear_points[0], ear_points[1], COLORS['pose_ears_ring'], 1)


def draw_hand_landmarks(image, landmarks, width, height, is_left: bool):
    """
    Draw hand skeleton for Dualcast mode.
    Left hand = blue accent, Right hand = green accent.
    """
    tip_col  = COLORS['hand_left_tip']  if is_left else COLORS['hand_right_tip']
    conn_col = COLORS['hand_left_conn'] if is_left else COLORS['hand_right_conn']

    pts = [(int(lm.x * width), int(lm.y * height))
           for lm in landmarks.landmark]

    # Connections
    for a, b in HAND_CONNECTIONS:
        cv2.line(image, pts[a], pts[b], conn_col, 1, cv2.LINE_AA)

    # All joints: small dot
    for i, (x, y) in enumerate(pts):
        r = 5 if i in HAND_TIPS else 3
        cv2.circle(image, (x, y), r, tip_col, -1, cv2.LINE_AA)

    # Pinch indicator: thumb-tip (4) to index-tip (8) distance
    t4, t8 = pts[4], pts[8]
    dist = np.hypot(t4[0] - t8[0], t4[1] - t8[1])
    norm_dist = np.hypot(
        landmarks.landmark[4].x - landmarks.landmark[8].x,
        landmarks.landmark[4].y - landmarks.landmark[8].y,
    )
    if norm_dist < 0.055:
        mid = ((t4[0] + t8[0]) // 2, (t4[1] + t8[1]) // 2)
        cv2.circle(image, mid, 10, (255, 255, 255), 1, cv2.LINE_AA)


# ============================================================
#  OSC senders
# ============================================================
def send_pose_osc(client, pose_landmarks):
    """Send /pose with 33×2 normalised landmark floats."""
    if pose_landmarks:
        data = []
        for lm in pose_landmarks.landmark:
            data.extend([float(lm.x), float(lm.y)])
        client.send_message("/pose", data)


def send_hands_osc(client, hand_results):
    """
    Send /hands OSC message.

    Layout:
      [0]          = numHands (0..2)
      per hand:
        [base + 0] = label (0.0=Left, 1.0=Right, mirror-corrected)
        [base + 1..42] = lm[0].x, lm[0].y, ..., lm[20].x, lm[20].y
    """
    if not hand_results.multi_hand_landmarks or not hand_results.multi_handedness:
        client.send_message("/hands", [0.0])
        return

    data = [float(len(hand_results.multi_hand_landmarks))]
    for i, lms in enumerate(hand_results.multi_hand_landmarks):
        # MediaPipe labels are mirrored; correct them since we flip the frame
        raw   = hand_results.multi_handedness[i].classification[0].label
        label = 1.0 if raw == "Left" else 0.0   # after flip: Left→Right, Right→Left
        data.append(label)
        for lm in lms.landmark:
            data.extend([float(lm.x), float(lm.y)])

    client.send_message("/hands", data)


# ============================================================
#  Main
# ============================================================
def main():
    osc_client   = udp_client.SimpleUDPClient(OSC_IP, OSC_PORT)
    video_server = VideoServer(VIDEO_PORT)

    pose = mp_pose.Pose(
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
        model_complexity=1,
    )
    hands = mp_hands.Hands(
        max_num_hands=2,
        min_detection_confidence=0.80,
        min_tracking_confidence=0.80,
        model_complexity=1,
    )

    cap = cv2.VideoCapture(0)

    print("SOMATUN Vision Helper (headless - streaming to JUCE)")
    print("=" * 55)
    print(f"OSC   → port {OSC_PORT}  (/pose and /hands)")
    print(f"Video → port {VIDEO_PORT}  (TCP JPEG stream)")
    print("Press Ctrl+C to quit")

    try:
        while cap.isOpened():
            success, image = cap.read()
            if not success:
                continue

            image = cv2.flip(image, 1)
            height, width = image.shape[:2]

            rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            rgb_image.flags.writeable = False

            pose_results  = pose.process(rgb_image)
            hand_results  = hands.process(rgb_image)

            rgb_image.flags.writeable = True
            image = cv2.cvtColor(rgb_image, cv2.COLOR_RGB2BGR)

            # ── Pose overlay (FleshSynth joints) ──────────────
            # if pose_results.pose_landmarks:
            #    draw_pose_landmarks(image, pose_results.pose_landmarks, width, height)

            # ── Hand overlay (Dualcast) ────────────────────────
            # if hand_results.multi_hand_landmarks and hand_results.multi_handedness:
            #    for i, lms in enumerate(hand_results.multi_hand_landmarks):
            #        raw      = hand_results.multi_handedness[i].classification[0].label
            #        is_left  = (raw == "Right")   # flipped frame: raw Right → displayed Left
            #        draw_hand_landmarks(image, lms, width, height, is_left)

            # ── OSC ───────────────────────────────────────────
            send_pose_osc  (osc_client, pose_results.pose_landmarks)
            send_hands_osc (osc_client, hand_results)

            # ── Video ─────────────────────────────────────────
            video_server.send_frame(image)

    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
        video_server.close()
        pose.close()
        hands.close()
        print("[Vision] shutdown complete")


if __name__ == "__main__":
    main()