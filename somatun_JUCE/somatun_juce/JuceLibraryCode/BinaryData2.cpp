/* ==================================== JUCER_BINARY_RESOURCE ====================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#include <cstring>

namespace BinaryData
{

//================== somatun_vision.py ==================
static const unsigned char temp_binary_data_2[] =
"import cv2\n"
"import mediapipe as mp\n"
"import numpy as np\n"
"\n"
"mp_pose = mp.solutions.pose\n"
"mp_drawing = mp.solutions.drawing_utils\n"
"\n"
"cap = cv2.VideoCapture(0)\n"
"\n"
"with mp_pose.Pose(\n"
"    min_detection_confidence=0.5,\n"
"    min_tracking_confidence=0.5\n"
") as pose:\n"
"\n"
"    while cap.isOpened():\n"
"        ret, frame = cap.read()\n"
"        if not ret:\n"
"            break\n"
"\n"
"        # Convert BGR \xe2\x86\x92 RGB\n"
"        image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)\n"
"        image.flags.writeable = False\n"
"\n"
"        results = pose.process(image)\n"
"\n"
"        # Convert back RGB \xe2\x86\x92 BGR\n"
"        image.flags.writeable = True\n"
"        image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)\n"
"\n"
"        # Draw pose\n"
"        if results.pose_landmarks:\n"
"            mp_drawing.draw_landmarks(\n"
"                image,\n"
"                results.pose_landmarks,\n"
"                mp_pose.POSE_CONNECTIONS\n"
"            )\n"
"\n"
"        cv2.imshow('Somatun Vision', image)\n"
"\n"
"        if cv2.waitKey(10) & 0xFF == 27:  # ESC to quit\n"
"            break\n"
"\n"
"cap.release()\n"
"cv2.destroyAllWindows()";

const char* somatun_vision_py = (const char*) temp_binary_data_2;
}
