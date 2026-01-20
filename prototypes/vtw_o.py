import cv2
import mediapipe as mp
import numpy as np
import sounddevice as sd

mp_pose = mp.solutions.pose

CAM_INDEX = 0

# mediapipe landmarks
LEFT_WRIST = 15
LEFT_ELBOW = 13
LEFT_SHOULDER = 11
RIGHT_SHOULDER = 12
RIGHT_ELBOW = 14
RIGHT_WRIST = 16
LEFT_EAR = 7
RIGHT_EAR = 8

# layout
WAVE_HEIGHT = 300
WAVE_SCALE = 0.4
DEBUG_SIDEBAR_WIDTH = 500
WINDOW_NAME = "VTW - Body-shaped Wave"

# audio
SAMPLE_RATE = 44100
OSC_FREQ = 440.0
AUDIO_BLOCKSIZE = 256

BASE_FREQ = 440.0
BASE_FREQ_SMOOTH = 440.0
BASE_FREQ_SMOOTH_ALPHA = 0.07

SLIDER_MIN_FREQ = 55.0
SLIDER_MAX_FREQ = 1760.0

# sliders state
freq_slider_pos_norm = 0.5
tilt_slider_pos_norm = 0.25

freq_slider_rect = (0, 0, 0, 0)
tilt_slider_rect = (0, 0, 0, 0)
toggle_rect = (0, 0, 0, 0)
slider_dragging = None
tilt_invert = False

# wave state
current_wave_buffer = None
play_wave_buffer = None
phase = 0.0
MORPH_ALPHA = 0.08

# pitch and tilt
pitch_factor = 1.0
PITCH_ALPHA_TRACK = 0.12
PITCH_ALPHA_RELAX = 0.05
PITCH_FACTOR_MIN = 0.2
PITCH_FACTOR_MAX = 5.0

TILT_MAX_RANGE_SEMITONES = 4.0
tilt_raw_smooth = 0.0
TILT_SMOOTH_ALPHA = 0.35   # faster response
TILT_DEADZONE = 0.008
MAX_TILT = 0.07

# joint and wave smoothing
JOINT_SMOOTH_ALPHA = 0.45   # faster visual response
joint_smooth_prev = None
WAVE_SMOOTH_ALPHA = 0.35


def smooth_joints(pts_px: np.ndarray) -> np.ndarray:
    global joint_smooth_prev
    if pts_px is None:
        return None
    pts = pts_px.astype(np.float32)
    if joint_smooth_prev is None:
        joint_smooth_prev = pts.copy()
    else:
        joint_smooth_prev = (
            (1.0 - JOINT_SMOOTH_ALPHA) * joint_smooth_prev
            + JOINT_SMOOTH_ALPHA * pts
        )
    return joint_smooth_prev.astype(np.int32)


def smooth_wave(prev: np.ndarray | None, new: np.ndarray | None) -> np.ndarray | None:
    if new is None:
        return prev
    if prev is None:
        return new.copy()
    return (1.0 - WAVE_SMOOTH_ALPHA) * prev + WAVE_SMOOTH_ALPHA * new


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

        # map y from image space to roughly -1..1 for audio
        yn = (0.5 - lm.y) * 2.0
        ys_norm.append(yn)

    return np.array(pts_px, np.int32), np.array(ys_norm, np.float32)


def draw_wave_overlay(canvas, joint_px, wave):
    if wave is None or joint_px is None:
        return canvas

    global current_wave_buffer

    h, w, _ = canvas.shape

    xs_raw = joint_px[:, 0].astype(np.float32)
    pts = list(zip(xs_raw, wave))
    pts_sorted = sorted(pts, key=lambda p: p[0])

    xs_sorted = np.array([p[0] for p in pts_sorted], dtype=np.float32)
    wave_sorted = np.array([p[1] for p in pts_sorted], dtype=np.float32)

    x0 = xs_sorted[0]
    x5 = xs_sorted[-1]
    dist = max(x5 - x0, 1.0)
    relative_x = (xs_sorted - x0) / dist

    left_x = w * 0.05
    right_x = w * 0.95
    width = right_x - left_x

    num_samples = 512
    target_norm = np.linspace(0, 1, num_samples, dtype=np.float32)
    wave_resampled = np.interp(target_norm, relative_x, wave_sorted)

    xs_fixed = left_x + target_norm * width

    rms = np.sqrt(np.mean(wave_resampled ** 2)) + 1e-6
    wave_norm = wave_resampled / (rms * 3.0)
    wave_final = np.clip(wave_norm, -1.0, 1.0)

    current_wave_buffer = wave_final.copy()

    band_h = h * WAVE_SCALE
    center_y = h * 0.5
    ys = center_y - wave_final * (band_h * 0.5)

    draw_pts = np.stack([xs_fixed, ys], axis=1).astype(np.int32)

    # bottom wave line only
    cv2.polylines(canvas, [draw_pts], False, (0, 255, 255), 3)

    return canvas


def audio_callback(outdata, frames, time_info, status):
    global phase, play_wave_buffer, current_wave_buffer, OSC_FREQ

    out = np.zeros(frames, dtype=np.float32)

    if play_wave_buffer is None:
        if current_wave_buffer is not None:
            play_wave_buffer = current_wave_buffer.copy()
        else:
            outdata[:] = out.reshape(-1, 1)
            return

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


def update_freq_slider_from_x(x):
    global freq_slider_pos_norm, freq_slider_rect, BASE_FREQ

    x0, y0, x1, y1 = freq_slider_rect
    if x1 <= x0:
        return

    t = (x - x0) / float(x1 - x0)
    t = max(0.0, min(1.0, t))
    freq_slider_pos_norm = t

    BASE_FREQ = SLIDER_MIN_FREQ + t * (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ)


def update_tilt_slider_from_x(x):
    global tilt_slider_pos_norm, tilt_slider_rect

    x0, y0, x1, y1 = tilt_slider_rect
    if x1 <= x0:
        return

    t = (x - x0) / float(x1 - x0)
    t = max(0.0, min(1.0, t))
    tilt_slider_pos_norm = t


def mouse_callback(event, x, y, flags, param):
    global slider_dragging, freq_slider_rect, tilt_slider_rect, toggle_rect, tilt_invert

    if event == cv2.EVENT_LBUTTONDOWN:
        fx0, fy0, fx1, fy1 = freq_slider_rect
        tx0, ty0, tx1, ty1 = tilt_slider_rect
        bx0, by0, bx1, by1 = toggle_rect

        if fx0 <= x <= fx1 and fy0 <= y <= fy1:
            slider_dragging = "freq"
            update_freq_slider_from_x(x)
        elif tx0 <= x <= tx1 and ty0 <= y <= ty1:
            slider_dragging = "tilt"
            update_tilt_slider_from_x(x)
        elif bx0 <= x <= bx1 and by0 <= y <= by1:
            tilt_invert = not tilt_invert

    elif event == cv2.EVENT_MOUSEMOVE:
        if slider_dragging == "freq":
            update_freq_slider_from_x(x)
        elif slider_dragging == "tilt":
            update_tilt_slider_from_x(x)

    elif event == cv2.EVENT_LBUTTONUP:
        slider_dragging = None


def main():
    global current_wave_buffer, play_wave_buffer
    global BASE_FREQ, BASE_FREQ_SMOOTH, OSC_FREQ
    global freq_slider_pos_norm, tilt_slider_pos_norm
    global freq_slider_rect, tilt_slider_rect, toggle_rect
    global pitch_factor, tilt_raw_smooth, tilt_invert

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

    BASE_FREQ = 440.0
    BASE_FREQ_SMOOTH = BASE_FREQ
    freq_slider_pos_norm = (BASE_FREQ - SLIDER_MIN_FREQ) / (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ)
    tilt_slider_pos_norm = 0.25

    pitch_factor = 1.0
    tilt_raw_smooth = 0.0
    OSC_FREQ = BASE_FREQ * pitch_factor

    with audio_stream, mp_pose.Pose(model_complexity=1, smooth_landmarks=False) as pose:
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

            tilt_norm = 0.0
            semitone_offset = 0.0

            if result.pose_landmarks:
                lm = result.pose_landmarks.landmark

                pts_px_raw, raw_wave = extract_ordered_points(lm, w, h)
                pts_px = smooth_joints(pts_px_raw)

                # draw only the six used joints and polyline
                for (x, y) in pts_px:
                    cv2.circle(frame, (x, y), 8, (0, 255, 255), -1)
                cv2.polylines(frame, [pts_px], False, (0, 255, 255), 2)

                smoothed_wave = smooth_wave(prev_wave, raw_wave)
                prev_wave = smoothed_wave

                left_ear = lm[LEFT_EAR]
                right_ear = lm[RIGHT_EAR]

                # --- ear visualizer ---
                lx, ly = int(left_ear.x * w), int(left_ear.y * h)
                rx, ry = int(right_ear.x * w), int(right_ear.y * h)

                # line between ears
                cv2.line(frame, (lx, ly), (rx, ry), (255, 180, 50), 2)

                # decide which ear is "controlling" based on geometry + invert
                if not tilt_invert:
                    controlling = "right" if right_ear.y > left_ear.y else "left"
                else:
                    controlling = "left" if right_ear.y > left_ear.y else "right"

                if controlling == "left":
                    # left active
                    cv2.circle(frame, (lx, ly), 12, (255, 150, 0), -1)
                    cv2.circle(frame, (lx, ly), 16, (0, 0, 0), 3)
                    cv2.circle(frame, (rx, ry), 10, (160, 120, 60), -1)
                else:
                    # right active
                    cv2.circle(frame, (rx, ry), 12, (255, 150, 0), -1)
                    cv2.circle(frame, (rx, ry), 16, (0, 0, 0), 3)
                    cv2.circle(frame, (lx, ly), 10, (160, 120, 60), -1)
                # --- end ear visualizer ---

                head_tilt = right_ear.y - left_ear.y

                tilt_raw_smooth = (
                    (1.0 - TILT_SMOOTH_ALPHA) * tilt_raw_smooth
                    + TILT_SMOOTH_ALPHA * head_tilt
                )
                head_tilt = tilt_raw_smooth

                if abs(head_tilt) < TILT_DEADZONE:
                    head_tilt = 0.0

                tilt_norm = max(min(head_tilt / MAX_TILT, 1.0), -1.0)

                tilt_shaped = tilt_norm * 0.55 + (tilt_norm ** 3) * 0.45

                if tilt_invert:
                    tilt_shaped = -tilt_shaped

                max_semitones = tilt_slider_pos_norm * TILT_MAX_RANGE_SEMITONES
                semitone_offset = tilt_shaped * max_semitones

                target_pitch_factor = 2 ** (semitone_offset / 12.0)
                pitch_factor = (
                    (1.0 - PITCH_ALPHA_TRACK) * pitch_factor
                    + PITCH_ALPHA_TRACK * target_pitch_factor
                )

            else:
                pitch_factor = (
                    (1.0 - PITCH_ALPHA_RELAX) * pitch_factor
                    + PITCH_ALPHA_RELAX * 1.0
                )

            pitch_factor = float(np.clip(pitch_factor, PITCH_FACTOR_MIN, PITCH_FACTOR_MAX))

            BASE_FREQ_SMOOTH = (
                (1.0 - BASE_FREQ_SMOOTH_ALPHA) * BASE_FREQ_SMOOTH
                + BASE_FREQ_SMOOTH_ALPHA * BASE_FREQ
            )

            OSC_FREQ = BASE_FREQ_SMOOTH * pitch_factor

            total_h = h + WAVE_HEIGHT
            total_w = w + DEBUG_SIDEBAR_WIDTH
            combined = np.zeros((total_h, total_w, 3), dtype=np.uint8)

            combined[:h, :w] = frame

            wave_canvas = combined[h:h + WAVE_HEIGHT, :w]
            wave_canvas[:] = (0, 0, 0)
            draw_wave_overlay(wave_canvas, pts_px, smoothed_wave)

            sidebar = combined[:, w:w + DEBUG_SIDEBAR_WIDTH]
            sidebar[:] = (20, 20, 20)

            sh, sw, _ = sidebar.shape
            scope_h = sh // 2
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

            slider_area = sidebar[scope_h:, :]
            sa_h, sa_w, _ = slider_area.shape
            slider_margin = 30

            freq_line_y = scope_h + sa_h // 3
            tilt_line_y = scope_h + 2 * sa_h // 3
            x0 = w + slider_margin
            x1 = w + DEBUG_SIDEBAR_WIDTH - slider_margin

            freq_slider_rect = (x0, freq_line_y - 15, x1, freq_line_y + 15)
            tilt_slider_rect = (x0, tilt_line_y - 15, x1, tilt_line_y + 15)

            cv2.line(combined, (x0, freq_line_y), (x1, freq_line_y), (80, 80, 80), 3)
            freq_knob_x = int(x0 + freq_slider_pos_norm * (x1 - x0))
            cv2.circle(combined, (freq_knob_x, freq_line_y), 10, (0, 255, 255), -1)

            cv2.putText(
                combined,
                f"freq: {OSC_FREQ:.1f} Hz",
                (w + 20, freq_line_y - 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (200, 200, 200),
                2,
            )

            cv2.line(combined, (x0, tilt_line_y), (x1, tilt_line_y), (80, 80, 80), 3)
            tilt_knob_x = int(x0 + tilt_slider_pos_norm * (x1 - x0))
            cv2.circle(combined, (tilt_knob_x, tilt_line_y), 10, (0, 200, 255), -1)

            max_semitones = tilt_slider_pos_norm * TILT_MAX_RANGE_SEMITONES
            cv2.putText(
                combined,
                f"tilt range: {max_semitones:.2f} st",
                (w + 20, tilt_line_y - 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (200, 200, 200),
                2,
            )

            toggle_x0 = x0
            toggle_x1 = x0 + 180
            toggle_y0 = tilt_line_y + 25
            toggle_y1 = toggle_y0 + 40
            toggle_rect = (toggle_x0, toggle_y0, toggle_x1, toggle_y1)

            color = (0, 140, 255) if tilt_invert else (0, 80, 200)
            cv2.rectangle(combined, (toggle_x0, toggle_y0), (toggle_x1, toggle_y1), color, -1)

            label = "invert: on" if tilt_invert else "invert: off"
            cv2.putText(
                combined,
                label,
                (toggle_x0 + 10, toggle_y0 + 27),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (255, 255, 255),
                2,
            )

            cv2.putText(
                combined,
                f"tilt_norm {tilt_norm:.2f} semi {semitone_offset:.2f} pf {pitch_factor:.3f}",
                (w + 20, toggle_y1 + 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (200, 200, 200),
                1,
            )

            cv2.imshow(WINDOW_NAME, combined)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
