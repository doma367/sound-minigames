import cv2
import mediapipe as mp
import numpy as np
import sounddevice as sd

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
WAVE_HEIGHT = 300
WAVE_SCALE = 0.4

# sidebar width
DEBUG_SIDEBAR_WIDTH = 500

# audio settings
SAMPLE_RATE = 44100
OSC_FREQ = 440.0   # will be controlled by slider
AUDIO_BLOCKSIZE = 256

# slider settings
SLIDER_MIN_FREQ = 55.0      # low A-ish
SLIDER_MAX_FREQ = 1760.0    # high A-ish
slider_pos_norm = 0.5       # 0..1 -> maps to freq
slider_dragging = False
slider_rect = (0, 0, 0, 0)  # x0, y0, x1, y1 (updated each frame)

WINDOW_NAME = "VTW - Body-shaped Wave"

# globals
current_wave_buffer = None      # live 512 sample wave from body
play_wave_buffer = None         # smoothed buffer used by oscillator
phase = 0.0                     # oscillator phase

# how fast the oscillator morphs toward new shapes per audio block
MORPH_ALPHA = 0.08   # between 0 and 1 smaller = slower smoother morph


def extract_ordered_points(landmarks, w, h):
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
    if wave is None or joint_px is None:
        return canvas

    global current_wave_buffer

    h, w, _ = canvas.shape

    # extract raw xs
    xs_raw = joint_px[:, 0].astype(np.float32)
    pts = list(zip(xs_raw, wave))
    pts_sorted = sorted(pts, key=lambda p: p[0])

    xs_sorted = np.array([p[0] for p in pts_sorted], dtype=np.float32)
    wave_sorted = np.array([p[1] for p in pts_sorted], dtype=np.float32)

    # relative 2 coordinate spacing
    x0 = xs_sorted[0]
    x5 = xs_sorted[-1]
    dist = max(x5 - x0, 1.0)
    relative_x = (xs_sorted - x0) / dist

    # full width mapping
    left_x = w * 0.05
    right_x = w * 0.95
    width = right_x - left_x

    num_samples = 512
    target_norm = np.linspace(0, 1, num_samples, dtype=np.float32)
    wave_resampled = np.interp(target_norm, relative_x, wave_sorted)

    xs_fixed = left_x + target_norm * width

    # rms normalization + hard clip
    rms = np.sqrt(np.mean(wave_resampled ** 2)) + 1e-6
    wave_norm = wave_resampled / (rms * 3.0)   # 3.0 is gain factor tweak if needed
    wave_final = np.clip(wave_norm, -1.0, 1.0)

    # store final buffer for audio
    current_wave_buffer = wave_final.copy()

    # draw bottom wave
    band_h = h * WAVE_SCALE
    center_y = h * 0.5
    ys = center_y - wave_final * (band_h * 0.5)

    draw_pts = np.stack([xs_fixed, ys], axis=1).astype(np.int32)

    for (x, y) in draw_pts:
        cv2.circle(canvas, (int(x), int(y)), 8, (255, 255, 0), -1)

    cv2.polylines(canvas, [draw_pts], False, (0, 255, 255), 3)

    return canvas


def audio_callback(outdata, frames, time_info, status):
    global phase, play_wave_buffer, current_wave_buffer, OSC_FREQ

    out = np.zeros(frames, dtype=np.float32)

    # if we dont have a playable wave yet try to seed it from current
    if play_wave_buffer is None:
        if current_wave_buffer is not None:
            play_wave_buffer = current_wave_buffer.copy()
        else:
            outdata[:] = out.reshape(-1, 1)
            return

    # live morphing step
    if current_wave_buffer is not None and len(current_wave_buffer) == len(play_wave_buffer):
        play_wave_buffer[:] = (
            (1.0 - MORPH_ALPHA) * play_wave_buffer +
            MORPH_ALPHA * current_wave_buffer
        )

    wave = play_wave_buffer
    n = len(wave)
    if n == 0:
        outdata[:] = out.reshape(-1, 1)
        return

    phase_inc = (OSC_FREQ * n) / SAMPLE_RATE

    for i in range(frames):
        idx = int(phase) % n
        frac = phase - int(phase)
        next_idx = (idx + 1) % n

        s0 = wave[idx]
        s1 = wave[next_idx]
        sample = s0 * (1.0 - frac) + s1 * frac

        out[i] = sample

        phase += phase_inc
        if phase >= n:
            phase -= n

    outdata[:] = out.reshape(-1, 1)


def update_slider_from_x(x):
    global slider_pos_norm, slider_rect, OSC_FREQ

    x0, y0, x1, y1 = slider_rect
    if x1 <= x0:
        return

    t = (x - x0) / float(x1 - x0)
    t = max(0.0, min(1.0, t))
    slider_pos_norm = t

    # map to frequency
    OSC_FREQ = SLIDER_MIN_FREQ + t * (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ)


def mouse_callback(event, x, y, flags, param):
    global slider_dragging, slider_rect

    if event == cv2.EVENT_LBUTTONDOWN:
        x0, y0, x1, y1 = slider_rect
        if x0 <= x <= x1 and y0 <= y <= y1:
            slider_dragging = True
            update_slider_from_x(x)

    elif event == cv2.EVENT_MOUSEMOVE:
        if slider_dragging:
            update_slider_from_x(x)

    elif event == cv2.EVENT_LBUTTONUP:
        slider_dragging = False


def main():
    global current_wave_buffer, play_wave_buffer, slider_rect

    cap = cv2.VideoCapture(CAM_INDEX)

    cv2.namedWindow(WINDOW_NAME)
    cv2.setMouseCallback(WINDOW_NAME, mouse_callback)

    audio_stream = sd.OutputStream(
        channels=1,
        samplerate=SAMPLE_RATE,
        blocksize=AUDIO_BLOCKSIZE,
        callback=audio_callback,
        dtype="float32",
    )

    # init slider position based on default freq
    global slider_pos_norm
    slider_pos_norm = (OSC_FREQ - SLIDER_MIN_FREQ) / (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ)

    with audio_stream, mp_pose.Pose(model_complexity=1, smooth_landmarks=True) as pose:
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

                for (x, y) in pts_px:
                    cv2.circle(frame, (x, y), 8, (0, 255, 255), -1)

                cv2.polylines(frame, [pts_px], False, (0, 255, 255), 2)

                smoothed_wave, movement = hybrid_smooth(prev_wave, raw_wave)
                prev_wave = smoothed_wave

                cv2.putText(frame, f"movement: {movement:.3f}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                            0.7, (255, 255, 255), 2)

            # build combined ui
            total_h = h + WAVE_HEIGHT
            total_w = w + DEBUG_SIDEBAR_WIDTH
            combined = np.zeros((total_h, total_w, 3), dtype=np.uint8)

            combined[:h, :w] = frame

            wave_canvas = combined[h:h + WAVE_HEIGHT, :w]
            wave_canvas[:] = (0, 0, 0)
            draw_wave_overlay(wave_canvas, pts_px, smoothed_wave)

            # sidebar region
            sidebar = combined[:, w:w + DEBUG_SIDEBAR_WIDTH]
            sidebar[:] = (20, 20, 20)

            sh, sw, _ = sidebar.shape
            scope_h = sh // 2

            # top half: scope
            scope = sidebar[0:scope_h, :]

            if current_wave_buffer is not None:
                buf = current_wave_buffer.astype(np.float32)

                s_h, s_w, _ = scope.shape
                margin = 10

                xs = np.linspace(margin, s_w - margin, len(buf)).astype(np.int32)

                max_amp = np.max(np.abs(buf)) + 1e-6
                norm = buf / max_amp

                center_y = s_h // 2
                amp = (s_h // 2 - margin)

                ys = (center_y - norm * amp).astype(np.int32)

                pts = np.stack([xs, ys], axis=1)
                cv2.polylines(scope, [pts], False, (0, 255, 100), 2)

                cv2.putText(scope, f"min: {buf.min():.3f}", (10, 20),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)
                cv2.putText(scope, f"max: {buf.max():.3f}", (10, 40),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)
                cv2.putText(scope, f"rms: {np.sqrt(np.mean(buf**2)):.3f}", (10, 60),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)

            # bottom half: slider
            slider_area = sidebar[scope_h:, :]
            sa_h, sa_w, _ = slider_area.shape
            slider_margin = 30

            # compute slider geometry in global coords for mouse hit
            line_y = scope_h + sa_h // 2
            x0 = w + slider_margin
            x1 = w + DEBUG_SIDEBAR_WIDTH - slider_margin
            slider_rect = (x0, line_y - 15, x1, line_y + 15)

            # draw slider line and knob on slider_area
            cv2.line(combined, (x0, line_y), (x1, line_y), (80, 80, 80), 3)

            knob_x = int(x0 + slider_pos_norm * (x1 - x0))
            cv2.circle(combined, (knob_x, line_y), 10, (0, 255, 255), -1)

            # draw current freq text
            cv2.putText(combined, f"freq: {OSC_FREQ:.1f} Hz",
                        (w + 20, line_y - 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (200, 200, 200), 2)

            cv2.imshow(WINDOW_NAME, combined)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
