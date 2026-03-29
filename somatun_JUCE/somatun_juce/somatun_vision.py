import cv2
import mediapipe as mp
from pythonosc import udp_client
import numpy as np
import socket
import struct
import threading

# OSC setup for FleshSynth compatibility
OSC_IP = "127.0.0.1"
OSC_PORT = 9000

# Video stream setup (sends frames to JUCE CameraView)
VIDEO_PORT = 9001
JPEG_QUALITY = 60   # lower = faster, adequate for display

# MediaPipe setup
mp_pose = mp.solutions.pose
mp_hands = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils

# Color scheme for different landmark types
COLORS = {
    'pose_joints': (0, 255, 255),       # Cyan - FleshSynth arm joints
    'pose_ears': (255, 150, 0),         # Orange - FleshSynth ear tracking
    'pose_other': (100, 100, 150),      # Dim purple - other pose landmarks
    'hand_tips': (0, 255, 0),           # Green - finger tips
    'hand_joints': (255, 0, 255),       # Magenta - other hand landmarks
    'hand_connections': (150, 150, 200) # Light purple - hand skeleton
}

# FleshSynth specific landmark indices
FLESHSYNTH_JOINTS = [15, 13, 11, 12, 14, 16]
FLESHSYNTH_EARS   = [7, 8]
FINGER_TIPS       = [4, 8, 12, 16, 20]
HAND_CONNECTIONS  = [
    (0,1),(1,2),(2,3),(3,4),
    (0,5),(5,6),(6,7),(7,8),
    (0,9),(9,10),(10,11),(11,12),
    (0,13),(13,14),(14,15),(15,16),
    (0,17),(17,18),(18,19),(19,20),
    (5,9),(9,13),(13,17),
]


# ============================================================
#  TCP video server — one accepted client at a time
# ============================================================
class VideoServer:
    """Accepts a single TCP connection and streams length-prefixed JPEG frames."""

    def __init__(self, port: int):
        self._lock   = threading.Lock()
        self._client = None                         # current connected socket

        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", port))
        self._server.listen(1)
        self._server.settimeout(0.5)               # non-blocking accept loop

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
        """Encode image as JPEG and send with a 4-byte big-endian length prefix."""
        with self._lock:
            if self._client is None:
                return
            client = self._client

        ok, buf = cv2.imencode(".jpg", bgr_image, [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY])
        if not ok:
            return

        data   = buf.tobytes()
        header = struct.pack(">I", len(data))   # 4-byte big-endian length
        try:
            client.sendall(header + data)
        except Exception:
            with self._lock:
                self._client = None

    def close(self):
        self._running = False
        self._server.close()


# ============================================================
#  Drawing helpers (unchanged from original)
# ============================================================
def draw_pose_landmarks(image, landmarks, width, height):
    """Draw only the landmarks used by FleshSynth (arm joints + ears).
    All other pose landmarks are intentionally skipped here;
    they will be drawn by the dedicated minigame pages when integrated."""
    if not landmarks:
        return

    # Cyan arm joints + connecting polyline
    joint_points = []
    for idx in FLESHSYNTH_JOINTS:
        lm = landmarks.landmark[idx]
        x, y = int(lm.x * width), int(lm.y * height)
        cv2.circle(image, (x, y), 8, COLORS['pose_joints'], -1)
        joint_points.append((x, y))

    if len(joint_points) == 6:
        pts = np.array(joint_points, np.int32)
        cv2.polylines(image, [pts], False, COLORS['pose_joints'], 2)

    # Orange ears + connecting line
    ear_points = []
    for idx in FLESHSYNTH_EARS:
        lm = landmarks.landmark[idx]
        x, y = int(lm.x * width), int(lm.y * height)
        cv2.circle(image, (x, y), 12, COLORS['pose_ears'], -1)
        cv2.circle(image, (x, y), 16, (0, 0, 0), 3)
        ear_points.append((x, y))

    if len(ear_points) == 2:
        cv2.line(image, ear_points[0], ear_points[1], COLORS['pose_ears'], 2)


def draw_hand_landmarks(image, landmarks, width, height, hand_label):
    """Reserved for Gestsamp/Pulsefield minigame — not drawn in FleshSynth mode."""
    pass


def send_osc_data(client, pose_landmarks):
    if pose_landmarks:
        pose_data = []
        for landmark in pose_landmarks.landmark:
            pose_data.extend([float(landmark.x), float(landmark.y)])
        client.send_message("/pose", pose_data)


# ============================================================
#  Main
# ============================================================
def main():
    osc_client   = udp_client.SimpleUDPClient(OSC_IP, OSC_PORT)
    video_server = VideoServer(VIDEO_PORT)

    pose = mp_pose.Pose(
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
        model_complexity=1
    )
    hands = mp_hands.Hands(
        max_num_hands=2,
        min_detection_confidence=0.7,
        min_tracking_confidence=0.6,
        model_complexity=1
    )

    cap = cv2.VideoCapture(0)

    print("Unified Vision Helper (headless — streaming to JUCE)")
    print("=" * 50)
    print(f"OSC   → port {OSC_PORT}")
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

            pose_results = pose.process(rgb_image)
            hand_results = hands.process(rgb_image)

            rgb_image.flags.writeable = True
            image = cv2.cvtColor(rgb_image, cv2.COLOR_RGB2BGR)

            if pose_results.pose_landmarks:
                draw_pose_landmarks(image, pose_results.pose_landmarks, width, height)

            # Hand landmarks and legend intentionally not drawn here.
            # draw_hand_landmarks() is reserved for Gestsamp/Pulsefield pages.

            # Send pose data over OSC (unchanged)
            send_osc_data(osc_client, pose_results.pose_landmarks)

            # Send annotated frame over TCP to JUCE
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