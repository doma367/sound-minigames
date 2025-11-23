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

LEFT_EAR = 7
RIGHT_EAR = 8

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
OSC_FREQ = 440.0   # actual oscillator freq (base * pitch_factor)
AUDIO_BLOCKSIZE = 256

# base freq controlled by slider only
BASE_FREQ = 440.0
BASE_FREQ_SMOOTH = 440.0
BASE_FREQ_SMOOTH_ALPHA = 0.07  # how quickly base freq follows slider

# slider settings
SLIDER_MIN_FREQ = 55.0      # low A-ish
SLIDER_MAX_FREQ = 1760.0    # high A-ish

# sliders
freq_slider_pos_norm = 0.5        # 0..1 -> maps to freq
tilt_slider_pos_norm = 0.25       # 0..1 -> maps to tilt depth

freq_slider_rect = (0, 0, 0, 0)   # x0, y0, x1, y1
tilt_slider_rect = (0, 0, 0, 0)
# rectangle toggle under tilt slider
toggle_rect = (0, 0, 0, 0)
slider_dragging = None            # None or "freq" or "tilt"

tilt_invert = False  # False: tilt left = pitch up, True: tilt left = pitch down

WINDOW_NAME = "VTW - Body-shaped Wave"

# globals
current_wave_buffer = None      # live 512 sample wave from body
play_wave_buffer = None         # smoothed buffer used by oscillator
phase = 0.0                     # oscillator phase

# how fast the oscillator morphs toward new shapes per audio block
MORPH_ALPHA = 0.08   # between 0 and 1 smaller = slower smoother morph

# pitch bend smoothing
pitch_factor = 1.0           # 1 = no bend
PITCH_ALPHA_TRACK = 0.12     # while tracking
PITCH_ALPHA_RELAX = 0.05     # when no landmarks
PITCH_FACTOR_MIN = 0.2
PITCH_FACTOR_MAX = 5.0

# tilt semitone range max value controlled by tilt slider
TILT_MAX_RANGE_SEMITONES = 4.0   # slider 0..1 maps to 0..this many semitones

# tilt smoothing
tilt_raw_smooth = 0.0
TILT_SMOOTH_ALPHA = 0.18
TILT_DEADZONE = 0.008
MAX_TILT = 0.07  # how much head tilt for full range

# joint smoothing
JOINT_SMOOTH_ALPHA = 0.18
joint_smooth_prev = None


def smooth_joints(pts_px: np.ndarray) -> np.ndarray:
    """Exponential smoothing on joint positions to reduce jitter."""
    global joint_smooth_prev
    if pts_px is None:
        return None
    pts_px = pts_px.astype(np.float32)
    if joint_smooth_prev is None:
        joint_smooth_prev = pts_px.copy()
    else:
        joint_smooth_prev = (
            (1.0 - JOINT_SMOOTH_ALPHA) * joint_smooth_prev
            + JOINT_SMOOTH_ALPHA * pts_px
        )
    return joint_smooth_prev.astype(np.int32)


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
    alpha = (
        SMOOTH_LOW_MOVEMENT_ALPHA
        if movement < MOVEMENT_THRESHOLD
        else SMOOTH_HIGH_MOVEMENT_ALPHA
    )
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


def update_freq_slider_from_x(x):
    global freq_slider_pos_norm, freq_slider_rect, BASE_FREQ

    x0, y0, x1, y1 = freq_slider_rect
    if x1 <= x0:
        return

    t = (x - x0) / float(x1 - x0)
    t = max(0.0, min(1.0, t))
    freq_slider_pos_norm = t

    # map to base frequency only slider sets the neutral pitch
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
            # click on inversion toggle rectangle
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

    # init slider positions based on default base freq
    BASE_FREQ = 440.0
    BASE_FREQ_SMOOTH = BASE_FREQ
    freq_slider_pos_norm = (BASE_FREQ - SLIDER_MIN_FREQ) / (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ)
    tilt_slider_pos_norm = 0.25  # default quarter of max tilt depth

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

                mp_draw.draw_landmarks(frame, result.pose_landmarks, mp_pose.POSE_CONNECTIONS)

                pts_px_raw, raw_wave = extract_ordered_points(lm, w, h)
                pts_px = smooth_joints(pts_px_raw)

                for (x, y) in pts_px:
                    cv2.circle(frame, (x, y), 8, (0, 255, 255), -1)

                cv2.polylines(frame, [pts_px], False, (0, 255, 255), 2)

                smoothed_wave, movement = hybrid_smooth(prev_wave, raw_wave)
                prev_wave = smoothed_wave

                cv2.putText(frame, f"movement: {movement:.3f}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                            0.7, (255, 255, 255), 2)

                # head tilt pitch bend
                left_ear = lm[LEFT_EAR]
                right_ear = lm[RIGHT_EAR]

                # positive when tilting head left negative when tilting right
                head_tilt = right_ear.y - left_ear.y

                # smooth raw tilt
                tilt_raw_smooth = (1.0 - TILT_SMOOTH_ALPHA) * tilt_raw_smooth + TILT_SMOOTH_ALPHA * head_tilt
                head_tilt = tilt_raw_smooth

                # deadzone
                if abs(head_tilt) < TILT_DEADZONE:
                    head_tilt = 0.0

                # normalize and curve
                tilt_norm = max(min(head_tilt / MAX_TILT, 1.0), -1.0)

                # blended curve: mostly linear in center but curved at edges
                tilt_shaped = tilt_norm * 0.55 + (tilt_norm ** 3) * 0.45

                if tilt_invert:
                    tilt_shaped = -tilt_shaped

                # how many semitones max from slider
                max_semitones = tilt_slider_pos_norm * TILT_MAX_RANGE_SEMITONES

                # semitone offset symmetric around zero
                semitone_offset = tilt_shaped * max_semitones

                # target factor from tilt
                target_pitch_factor = 2 ** (semitone_offset / 12.0)

                # smooth toward target while tracking
                pitch_factor = (1.0 - PITCH_ALPHA_TRACK) * pitch_factor + PITCH_ALPHA_TRACK * target_pitch_factor

            else:
                # no landmarks relax pitch back toward 1 smoothly
                pitch_factor = (1.0 - PITCH_ALPHA_RELAX) * pitch_factor + PITCH_ALPHA_RELAX * 1.0

            # clamp pitch factor to avoid crazy values
            pitch_factor = float(np.clip(pitch_factor, PITCH_FACTOR_MIN, PITCH_FACTOR_MAX))

            # smooth base frequency from slider
            BASE_FREQ_SMOOTH = (
                (1.0 - BASE_FREQ_SMOOTH_ALPHA) * BASE_FREQ_SMOOTH
                + BASE_FREQ_SMOOTH_ALPHA * BASE_FREQ
            )

            # final oscillator freq
            OSC_FREQ = BASE_FREQ_SMOOTH * pitch_factor

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

            # top half scope
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

            # bottom half sliders and toggle
            slider_area = sidebar[scope_h:, :]
            sa_h, sa_w, _ = slider_area.shape
            slider_margin = 30

            # two slider lines
            freq_line_y = scope_h + sa_h // 3
            tilt_line_y = scope_h + 2 * sa_h // 3
            x0 = w + slider_margin
            x1 = w + DEBUG_SIDEBAR_WIDTH - slider_margin

            # update rects
            freq_slider_rect = (x0, freq_line_y - 15, x1, freq_line_y + 15)
            tilt_slider_rect = (x0, tilt_line_y - 15, x1, tilt_line_y + 15)

            # draw freq slider
            cv2.line(combined, (x0, freq_line_y), (x1, freq_line_y), (80, 80, 80), 3)
            freq_knob_x = int(x0 + freq_slider_pos_norm * (x1 - x0))
            cv2.circle(combined, (freq_knob_x, freq_line_y), 10, (0, 255, 255), -1)

            cv2.putText(combined, f"freq: {OSC_FREQ:.1f} Hz",
                        (w + 20, freq_line_y - 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (200, 200, 200), 2)

            # draw tilt depth slider
            cv2.line(combined, (x0, tilt_line_y), (x1, tilt_line_y), (80, 80, 80), 3)
            tilt_knob_x = int(x0 + tilt_slider_pos_norm * (x1 - x0))
            cv2.circle(combined, (tilt_knob_x, tilt_line_y), 10, (0, 200, 255), -1)

            max_semitones = tilt_slider_pos_norm * TILT_MAX_RANGE_SEMITONES
            cv2.putText(combined, f"tilt range: {max_semitones:.2f} st",
                        (w + 20, tilt_line_y - 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (200, 200, 200), 2)

            # rectangle toggle just under the tilt text
            toggle_x0 = x0
            toggle_x1 = x0 + 180
            toggle_y0 = tilt_line_y + 25
            toggle_y1 = toggle_y0 + 40
            toggle_rect = (toggle_x0, toggle_y0, toggle_x1, toggle_y1)

            color = (0, 140, 255) if tilt_invert else (0, 80, 200)
            cv2.rectangle(combined, (toggle_x0, toggle_y0), (toggle_x1, toggle_y1), color, -1)

            label = "invert: on" if tilt_invert else "invert: off"
            cv2.putText(combined, label,
                        (toggle_x0 + 10, toggle_y0 + 27),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 255), 2)

            # extra debug
            cv2.putText(combined,
                        f"tilt_norm {tilt_norm:.2f} semi {semitone_offset:.2f} pf {pitch_factor:.3f}",
                        (w + 20, toggle_y1 + 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5,
                        (200, 200, 200), 1)

            cv2.imshow(WINDOW_NAME, combined)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
