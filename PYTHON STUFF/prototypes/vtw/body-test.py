import cv2
import mediapipe as mp

mp_pose = mp.solutions.pose
mp_draw = mp.solutions.drawing_utils

# use your phone cam index (likely 1)
cap = cv2.VideoCapture(0)

with mp_pose.Pose(
    model_complexity=1,
    enable_segmentation=False,
    smooth_landmarks=True,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5
) as pose:

    while True:
        ret, frame = cap.read()
        if not ret:
            continue

        frame = cv2.flip(frame, 1)
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        results = pose.process(rgb)

        # draw skeleton
        if results.pose_landmarks:
            mp_draw.draw_landmarks(
                frame,
                results.pose_landmarks,
                mp_pose.POSE_CONNECTIONS,
                landmark_drawing_spec=mp_draw.DrawingSpec(color=(0,255,0), thickness=2),
                connection_drawing_spec=mp_draw.DrawingSpec(color=(0,0,255), thickness=2)
            )
            h, w, _ = frame.shape
            for idx, lm in enumerate(results.pose_landmarks.landmark):
                cx, cy = int(lm.x * w), int(lm.y * h)
                cv2.putText(frame, str(idx), (cx, cy),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4,
                (0, 255, 255), 1)

        cv2.imshow("Body Tracking", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

cap.release()
cv2.destroyAllWindows()
