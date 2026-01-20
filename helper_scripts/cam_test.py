import cv2

cap = cv2.VideoCapture(0)  # sometimes 0 or 2 depending on device
while True:
    ret, frame = cap.read()
    if not ret:
        break
    cv2.imshow("phone cam", frame)
    if cv2.waitKey(1) & 0xFF == 27:
        break

cap.release()
cv2.destroyAllWindows()
