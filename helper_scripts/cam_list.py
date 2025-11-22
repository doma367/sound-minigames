import cv2

def check_cams(max_index=10):
    for i in range(max_index):
        cap = cv2.VideoCapture(i)
        if cap.isOpened():
            ret, frame = cap.read()
            print(i, ret)
            cap.release()

check_cams()
