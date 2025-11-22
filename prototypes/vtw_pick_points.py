import cv2
import mediapipe as mp
import numpy as np

mp_pose = mp.solutions.pose
mp_draw = mp.solutions.drawing_utils

CAM_INDEX = 1  # phone cam

# mediapipe landmark indices (fixed)
LEFT_WRIST = 15
LEFT_ELBOW = 13
LEFT_SHOULDER = 11
RIGHT_SHOULDER = 12
RIGHT_ELBOW = 14
RIGHT_WRIST = 16

# smoothing
SMOOTH_LOW_MOVEMENT_ALPHA = 0.2
SMOOTH_HIGH_MOVEMENT_ALPHA = 0.7
MOVEMENT_THRESHOLD = 0.03

# how big you want the wave region under the camera feed
WAVE_HEIGHT = 300   # increase this to make wave even bigger
WAVE_SCALE = 0.4    # vertical scale of waveform inside that region (0..1)


def extract_ordered_points(landmarks, w, h):
    # order: right wrist -> right elbow -> right shoulder -> left shoulder -> left elbow -> left wrist
    ordered_ids = [
        RIGHT_WRIST,
        RIGHT_ELBOW,
        RIGHT_SHOULDER,
        LEFT_SHOULDER,
        LEFT_ELBOW,
        LEFT_WRIST,
    ]

    pts_px = []
    ys_norm = []

    for idx in ordered_ids:
        lm = landmarks[idx]
        x = int(lm.x * w)
        y = int(lm.y * h)
        pts_px.append((x, y))

        # y normalized to [-1,1] (top=+1, bottom=-1)
        yn = (0.5 - lm.y) * 2.0
        ys_norm.append(yn)

    return np.array(pts_px, np.int32), np.array(ys_norm, np.float32)


def hybrid_smooth(prev, new):
    if prev is None:
        return new.copy(), 0.0

    movement = np.abs(new - prev).mean()
    alpha = SMOOTH_LOW_MOVEMENT_ALPHA if movement < MOVEMENT_THRESHOLD else SMOOTH_HIGH_MOVEMENT_ALPHA
    return prev * (1 - alpha) + new * alpha, movement


def draw_wave_overlay(canvas, joint_px, wave):
    """
    draw wave using:
      - X from joint_px, BUT SORTED left→right
      - Y from wave (normalized amplitudes)
    """
    if wave is None or joint_px is None:
        return canvas

    h, w, _ = canvas.shape

    # 1. extract xs
    xs = joint_px[:, 0].astype(np.float32)

    # 2. combine xs with ys (wave values)
    pts = list(zip(xs, wave))

    # 3. sort by x (left → right)
    pts_sorted = sorted(pts, key=lambda p: p[0])

    xs_sorted = np.array([p[0] for p in pts_sorted])
    wave_sorted = np.array([p[1] for p in pts_sorted])

    # vertical band settings
    band_h = h * WAVE_SCALE
    center_y = h * 0.5

    ys = center_y - wave_sorted * (band_h * 0.5)

    draw_pts = np.stack([xs_sorted, ys], axis=1).astype(np.int32)

    # draw circles
    for (x, y) in draw_pts:
        cv2.circle(canvas, (int(x), int(y)), 10, (255, 255, 0), -1)

    cv2.polylines(canvas, [draw_pts], False, (0, 255, 255), 4)

    return canvas



def main():
    cap = cv2.VideoCapture(CAM_INDEX)

    with mp_pose.Pose(model_complexity=1, smooth_landmarks=True) as pose:
        prev_wave = None

        while True:
            ret, frame = cap.read()
            if not ret:
                continue

            frame = cv2.flip(frame, 1)
            h, w, _ = frame.shape

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = pose.process(rgb)

            raw_wave = None
            smoothed_wave = None
            pts_px = None

            if result.pose_landmarks:
                lm = result.pose_landmarks.landmark

                mp_draw.draw_landmarks(frame, result.pose_landmarks, mp_pose.POSE_CONNECTIONS)

                pts_px, raw_wave = extract_ordered_points(lm, w, h)

                # draw joints on camera feed
                for (x, y) in pts_px:
                    cv2.circle(frame, (x, y), 8, (0, 255, 255), -1)

                # draw polyline on body
                cv2.polylines(frame, [pts_px], False, (0, 255, 255), 2)

                # smooth the y-wave
                smoothed_wave, movement = hybrid_smooth(prev_wave, raw_wave)
                prev_wave = smoothed_wave

                cv2.putText(frame, f"movement: {movement:.3f}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                            0.7, (255, 255, 255), 2)

            # build combined frame (camera on top, wave area below)
            combined = np.zeros((h + WAVE_HEIGHT, w, 3), dtype=np.uint8)

            # put camera feed on top
            combined[:h, :w] = frame

            # wave canvas at the bottom
            wave_canvas = combined[h:h + WAVE_HEIGHT, :w]
            wave_canvas[:] = (0, 0, 0)
            draw_wave_overlay(wave_canvas, pts_px, smoothed_wave)

            cv2.imshow("VTW - Body-shaped Wave", combined)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
