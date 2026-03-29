import cv2, mediapipe as mp, numpy as np
from pythonosc import udp_client

PORT = 9000
ADDR = "/pose"        # JUCE listens here
CAM  = 0              # webcam index

mp_pose = mp.solutions.pose
client  = udp_client.SimpleUDPClient("127.0.0.1", PORT)

with mp_pose.Pose(
        model_complexity=1,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5) as pose, \
     cv2.VideoCapture(CAM) as cap:

    print(f"[Vision] sending OSC {ADDR} to localhost:{PORT}")
    while cap.isOpened():
        ok, frame = cap.read()
        if not ok: continue
        frame = cv2.flip(frame, 1)
        rgb   = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        res   = pose.process(rgb)

        if res.pose_landmarks:
            lms = res.pose_landmarks.landmark   # 33 points
            payload = []
            for lm in lms:
                payload.extend([lm.x, lm.y])    # 66 floats
            client.send_message(ADDR, payload)

        # tiny preview window – close with q
        cv2.imshow("vision", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break