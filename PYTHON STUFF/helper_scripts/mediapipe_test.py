import cv2
import mediapipe as mp

# Initialize MediaPipe Holistic and Drawing utils
mp_holistic = mp.solutions.holistic
mp_drawing = mp.solutions.drawing_utils

# Initialize the webcam
cap = cv2.VideoCapture(0)

with mp_holistic.Holistic(
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5) as holistic:

    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break

        # Flip the image horizontally for a selfie-view display
        # Convert BGR to RGB as MediaPipe requires RGB input
        image = cv2.cvtColor(cv2.flip(frame, 1), cv2.COLOR_BGR2RGB)
        
        # To improve performance, optionally mark the image as not writeable
        image.flags.writeable = False
        results = holistic.process(image)

        # Draw the landmarks back onto the image
        image.flags.writeable = True
        image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

        # 1. Draw Pose Landmarks (Shoulders, Elbows, etc.)
        mp_drawing.draw_landmarks(
            image, results.pose_landmarks, mp_holistic.POSE_CONNECTIONS)

        # 2. Draw Left Hand Landmarks (Fingers)
        mp_drawing.draw_landmarks(
            image, results.left_hand_landmarks, mp_holistic.HAND_CONNECTIONS)

        # 3. Draw Right Hand Landmarks (Fingers)
        mp_drawing.draw_landmarks(
            image, results.right_hand_landmarks, mp_holistic.HAND_CONNECTIONS)

        cv2.imshow('MediaPipe Holistic Tracking', image)

        if cv2.waitKey(5) & 0xFF == 27: # Press 'ESC' to exit
            break

cap.release()
cv2.destroyAllWindows()