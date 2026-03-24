"""
Gesture Synth — v5
==================
Controls
--------
  RIGHT hand  → volume (raise = louder)
  LEFT hand   → pitch + trigger
      left  half → DRONE voice
      right half → LEAD  voice
      pinch + drag up/down → step through scale notes one at a time
      open hand (0.4 s)    → note ON
      fist      (0.3 s)    → note OFF

Keyboard shortcuts
------------------
  ESC        quit
  Z / X      drone octave ↓ / ↑
  C / V      lead  octave ↓ / ↑
  S / D      drone scale  prev / next
  F / G      lead  scale  prev / next
  1-4        drone sound
  Q-R        lead  sound  (Q=Sine  W=Organ  E=Soft Pad  R=Pluck)
"""

import cv2
import mediapipe as mp
import numpy as np
import sounddevice as sd
import threading
import time

# ══════════════════════════════════════════════════════
#  1. SCALES & MUSIC THEORY
# ══════════════════════════════════════════════════════

SCALES = {
    "Pentatonic":    [0, 2, 4, 7, 9],
    "Major":         [0, 2, 4, 5, 7, 9, 11],
    "Natural Minor": [0, 2, 3, 5, 7, 8, 10],
    "Blues":         [0, 3, 5, 6, 7, 10],
    "Dorian":        [0, 2, 3, 5, 7, 9, 10],
    "Phrygian":      [0, 1, 3, 5, 7, 8, 10],
    "Lydian":        [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian":    [0, 2, 4, 5, 7, 9, 10],
    "Chromatic":     list(range(12)),
}
SCALE_NAMES = list(SCALES.keys())
NOTE_NAMES  = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
SOUND_NAMES = ["Sine", "Organ", "Soft Pad", "Pluck"]

C2_HZ = 65.406

def freq_to_note_name(freq):
    if freq <= 0:
        return "---"
    midi   = 12 * np.log2(freq / C2_HZ) + 24
    midi_i = int(round(midi))
    return f"{NOTE_NAMES[midi_i % 12]}{midi_i // 12 - 1}"

def step_to_freq(step, scale_semitones, octave_offset):
    """Map a discrete step (0..len-1) → frequency. One octave only."""
    sps  = len(scale_semitones)
    step = int(np.clip(step, 0, sps - 1))
    semi = scale_semitones[step]
    return C2_HZ * (2 ** ((semi + octave_offset * 12) / 12.0))


# ══════════════════════════════════════════════════════
#  2. AUDIO ENGINE
# ══════════════════════════════════════════════════════

SAMPLE_RATE = 44100
BLOCK_SIZE  = 256

def _smooth_env(cur, tgt, frames, k=0.004):
    decay = np.exp(-k * np.arange(frames))
    env   = tgt + (cur - tgt) * decay
    return env, float(env[-1])

def _rich_wave(phases, freq):
    nyq = SAMPLE_RATE / 2
    sig = np.zeros(len(phases))
    h   = 1
    while freq * h < nyq:
        sig += (1.0 / h) * np.sin(h * phases)
        h   += 2
    return sig / (np.max(np.abs(sig)) + 1e-9)

class KarplusStrong:
    def __init__(self):
        self._buf    = np.zeros(4096)
        self._pos    = 0
        self._size   = 1
        self._active = False

    def trigger(self, freq):
        self._size   = max(1, int(SAMPLE_RATE / freq))
        self._buf    = (np.random.rand(self._size) * 2 - 1).astype(np.float64)
        self._pos    = 0
        self._active = True

    def release(self):
        self._active = False

    def generate(self, frames):
        out = np.zeros(frames)
        if not self._active:
            return out
        buf, pos, sz = self._buf, self._pos, self._size
        for i in range(frames):
            out[i]   = buf[pos]
            nxt      = (pos + 1) % sz
            buf[pos] = 0.996 * 0.5 * (buf[pos] + buf[nxt])
            pos      = nxt
        self._pos = pos
        return out

class Voice:
    def __init__(self):
        self.on       = False
        self.freq     = 130.81
        self._phase   = 0.0
        self._gain    = 0.0
        self._pad_g   = 0.0
        self._ks      = KarplusStrong()
        self._prev_on = False

    def generate(self, frames, sound_idx):
        tgt = 1.0 if self.on else 0.0

        if sound_idx == 3:
            if self.on and not self._prev_on:
                self._ks.trigger(self.freq)
            if not self.on and self._prev_on:
                self._ks.release()
            self._prev_on = self.on
            return self._ks.generate(frames)

        if sound_idx == 2:
            inc    = 2 * np.pi * self.freq / SAMPLE_RATE
            phases = self._phase + np.arange(frames) * inc
            self._phase = float(phases[-1]) % (2 * np.pi)
            sig = np.sin(phases) + 0.3 * np.sin(2 * phases)
            env, self._pad_g = _smooth_env(self._pad_g, tgt, frames, k=0.0025)
            self._prev_on = self.on
            return sig * env

        inc    = 2 * np.pi * self.freq / SAMPLE_RATE
        phases = self._phase + np.arange(frames) * inc
        self._phase = float(phases[-1]) % (2 * np.pi)
        sig = _rich_wave(phases, self.freq) if sound_idx == 1 else np.sin(phases)
        env, self._gain = _smooth_env(self._gain, tgt, frames)
        self._prev_on = self.on
        return sig * env

class AudioEngine:
    def __init__(self):
        self._lock       = threading.Lock()
        self.drone       = Voice()
        self.lead        = Voice()
        self.master_vol  = 0.0
        self._vol_gain   = 0.0
        self.drone_sound = 1
        self.lead_sound  = 1

    def set(self, **kw):
        with self._lock:
            for k, v in kw.items():
                if   k == "drone_on":    self.drone.on        = v
                elif k == "drone_freq":  self.drone.freq      = v
                elif k == "lead_on":     self.lead.on         = v
                elif k == "lead_freq":   self.lead.freq       = v
                elif k == "master_vol":  self.master_vol      = v
                elif k == "drone_sound": self.drone_sound     = v
                elif k == "lead_sound":  self.lead_sound      = v

    def callback(self, outdata, frames, time_info, status):
        with self._lock:
            mv    = self.master_vol
            d_sig = self.drone.generate(frames, self.drone_sound)
            l_sig = self.lead.generate(frames, self.lead_sound)
        v_env, self._vol_gain = _smooth_env(self._vol_gain, mv, frames, k=0.006)
        mixed = (d_sig * 0.45 + l_sig * 0.30) * v_env
        outdata[:] = np.clip(mixed, -1.0, 1.0).reshape(-1, 1)

engine = AudioEngine()
stream = sd.OutputStream(
    channels=1, callback=engine.callback,
    samplerate=SAMPLE_RATE, blocksize=BLOCK_SIZE, dtype="float32",
)
stream.start()


# ══════════════════════════════════════════════════════
#  3. GESTURE HELPERS
# ══════════════════════════════════════════════════════

mp_hands       = mp.solutions.hands
mp_draw        = mp.solutions.drawing_utils
HAND_CONN      = mp_hands.HAND_CONNECTIONS

hands_detector = mp_hands.Hands(
    max_num_hands=2,
    min_detection_confidence=0.80,
    min_tracking_confidence=0.80,
    model_complexity=1,
)

def lm_dist(lms, a, b):
    p, q = lms.landmark[a], lms.landmark[b]
    return np.hypot(p.x - q.x, p.y - q.y)

def hand_metrics(lms):
    pinch  = lm_dist(lms, 4, 8) < 0.055
    palm   = lms.landmark[9]
    spread = float(np.mean([
        np.hypot(lms.landmark[t].x - palm.x, lms.landmark[t].y - palm.y)
        for t in [4, 8, 12, 16, 20]
    ]))
    return pinch, spread, palm.x, palm.y

class HoldTimer:
    def __init__(self, threshold=0.35):
        self.threshold = threshold
        self._start    = None

    def update(self, condition):
        now = time.monotonic()
        if condition:
            if self._start is None:
                self._start = now
            return (now - self._start) >= self.threshold
        self._start = None
        return False

class EMA:
    def __init__(self, alpha=0.25):
        self.alpha = alpha
        self._v    = {}

    def __call__(self, key, val):
        if key not in self._v:
            self._v[key] = val
        self._v[key] = self.alpha * val + (1 - self.alpha) * self._v[key]
        return self._v[key]


# ══════════════════════════════════════════════════════
#  4. STEP CONTROLLER  (one octave, all notes reachable)
# ══════════════════════════════════════════════════════

class StepController:
    """
    Pinch + drag UP   → higher note (step +1)
    Pinch + drag DOWN → lower  note (step -1)
    ZONE: normalised-Y distance per step (tune to taste).
    """
    ZONE = 0.055

    def __init__(self):
        self._step      = {}
        self._anchor_y  = {}
        self._was_pinch = {}

    def update(self, key, pinch, hy, scale, octave):
        total = len(scale)   # one octave only

        if key not in self._step:
            self._step[key]      = total // 2
            self._anchor_y[key]  = hy
            self._was_pinch[key] = False

        was = self._was_pinch[key]

        if pinch:
            if not was:
                self._anchor_y[key] = hy
            else:
                delta       = self._anchor_y[key] - hy   # up = positive
                steps_moved = int(delta / self.ZONE)
                if steps_moved != 0:
                    self._step[key] = int(
                        np.clip(self._step[key] + steps_moved, 0, total - 1)
                    )
                    self._anchor_y[key] -= steps_moved * self.ZONE

        self._was_pinch[key] = pinch
        return step_to_freq(self._step[key], scale, octave)

    def current_step(self, key, scale):
        return self._step.get(key, len(scale) // 2)

    def reset_step(self, key):
        """Call when scale changes so step stays in bounds."""
        self._step.pop(key, None)

stepper = StepController()

ema    = EMA(alpha=0.30)
timers = {
    "drone_open": HoldTimer(0.40),
    "drone_fist": HoldTimer(0.30),
    "lead_open":  HoldTimer(0.40),
    "lead_fist":  HoldTimer(0.30),
}


# ══════════════════════════════════════════════════════
#  5. APPLICATION STATE  (per-voice scale & sound)
# ══════════════════════════════════════════════════════

class VoiceState:
    def __init__(self, scale_idx=0, sound_idx=1, octave=2):
        self.scale_idx = scale_idx
        self.sound_idx = sound_idx
        self.octave    = octave
        self.show_scale_menu = False
        self.show_sound_menu = False

    @property
    def scale(self):
        return SCALES[SCALE_NAMES[self.scale_idx]]

    @property
    def scale_name(self):
        return SCALE_NAMES[self.scale_idx]

    @property
    def sound_name(self):
        return SOUND_NAMES[self.sound_idx]

drone_state = VoiceState(scale_idx=0, sound_idx=1, octave=2)
lead_state  = VoiceState(scale_idx=0, sound_idx=1, octave=3)


# ══════════════════════════════════════════════════════
#  6. COLOURS & DRAWING
# ══════════════════════════════════════════════════════

FONT      = cv2.FONT_HERSHEY_SIMPLEX
C_PANEL   = (32,  32,  40)
C_ACCENT1 = (50, 140, 255)    # drone  BGR
C_ACCENT2 = (50, 210, 130)    # lead   BGR
C_TEXT    = (225, 225, 225)
C_DIM     = (90,  90, 100)
C_GREEN   = (60,  210,  80)
C_HOVER   = (55,  55,  70)
C_WHITE   = (255, 255, 255)
C_BG_BAR  = (20,  20,  26)

mouse_pos   = [0, 0]
mouse_click = [False]

def mouse_cb(event, x, y, flags, param):
    mouse_pos[0], mouse_pos[1] = x, y
    if event == cv2.EVENT_LBUTTONDOWN:
        mouse_click[0] = True

def draw_btn(frame, label, rect, active, hovered, accent):
    x1, y1, x2, y2 = rect
    bg = accent if active else (C_HOVER if hovered else C_PANEL)
    cv2.rectangle(frame, (x1,y1), (x2,y2), bg, -1, cv2.LINE_AA)
    cv2.rectangle(frame, (x1,y1), (x2,y2), accent if active else C_DIM, 1, cv2.LINE_AA)
    tw, th = cv2.getTextSize(label, FONT, 0.40, 1)[0]
    cv2.putText(frame, label,
                (x1 + (x2-x1-tw)//2, y1 + (y2-y1+th)//2),
                FONT, 0.40, C_BG_BAR if active else C_TEXT, 1, cv2.LINE_AA)

def build_rects(items, ax, ay, iw=155, ih=26):
    return [(ax, ay+i*(ih+2), ax+iw, ay+i*(ih+2)+ih) for i in range(len(items))]

def draw_dropdown(frame, items, rects, sel, accent):
    for i, (x1,y1,x2,y2) in enumerate(rects):
        hov = x1<=mouse_pos[0]<=x2 and y1<=mouse_pos[1]<=y2
        bg  = accent if i==sel else (C_HOVER if hov else C_PANEL)
        cv2.rectangle(frame,(x1,y1),(x2,y2), bg, -1, cv2.LINE_AA)
        cv2.rectangle(frame,(x1,y1),(x2,y2), accent if i==sel else C_DIM, 1, cv2.LINE_AA)
        tw, th = cv2.getTextSize(items[i], FONT, 0.40, 1)[0]
        cv2.putText(frame, items[i],
                    (x1+8, y1+(y2-y1+th)//2), FONT, 0.40,
                    C_BG_BAR if i==sel else C_TEXT, 1, cv2.LINE_AA)

def draw_volume_bar(frame, vol, cx, ytop, bh=100, bw=10):
    x1, x2 = cx-bw//2, cx+bw//2
    cv2.rectangle(frame,(x1,ytop),(x2,ytop+bh), C_PANEL, -1, cv2.LINE_AA)
    cv2.rectangle(frame,(x1,ytop),(x2,ytop+bh), C_DIM, 1, cv2.LINE_AA)
    f = int(vol*bh)
    if f > 0:
        cv2.rectangle(frame,(x1,ytop+bh-f),(x2,ytop+bh), C_GREEN, -1, cv2.LINE_AA)
    cv2.putText(frame, f"{int(vol*100)}%", (cx-14, ytop+bh+14),
                FONT, 0.36, C_GREEN, 1, cv2.LINE_AA)


# ── Note guidelines ────────────────────────────────────────────────────────────
# Horizontal lines, one per scale note, spanning only that voice's half.
# Active note line glows; inactive lines are dim.

def note_y_positions(scale, top_y, bot_y):
    """Return list of pixel Y for each note in scale (top=high, bottom=low)."""
    n = len(scale)
    if n == 1:
        return [(top_y + bot_y) // 2]
    return [int(bot_y - i * (bot_y - top_y) / (n - 1)) for i in range(n)]

def draw_note_guidelines(frame, vs: VoiceState, step_key, x_left, x_right,
                          top_y, bot_y, accent, voice_on, t_now):
    scale = vs.scale
    ys    = note_y_positions(scale, top_y, bot_y)
    cur   = stepper.current_step(step_key, scale)

    for i, y in enumerate(ys):
        note_label = NOTE_NAMES[scale[i] % 12]
        is_cur     = (i == cur)
        is_playing = is_cur and voice_on

        if is_playing:
            # Animated glow: pulsing alpha overlay
            pulse = 0.55 + 0.45 * np.sin(t_now * 8)
            col   = tuple(int(c * pulse) for c in accent)
            thick = 2
            # Glow halo (wider, dimmer line behind)
            halo = tuple(int(c * pulse * 0.35) for c in accent)
            cv2.line(frame, (x_left, y), (x_right, y), halo, 7, cv2.LINE_AA)
            cv2.line(frame, (x_left, y), (x_right, y), col,  2, cv2.LINE_AA)
        elif is_cur:
            # Current step but not playing — medium brightness
            col   = tuple(int(c * 0.55) for c in accent)
            thick = 1
            cv2.line(frame, (x_left, y), (x_right, y), col, thick, cv2.LINE_AA)
        else:
            cv2.line(frame, (x_left, y), (x_right, y), C_DIM, 1, cv2.LINE_AA)

        # Note label on the outer edge
        label_x = x_left - 32 if x_left > 10 else x_right + 6
        font_sz  = 0.46 if is_cur else 0.36
        font_wt  = 2    if is_playing else 1
        label_c  = accent if is_cur else C_DIM
        cv2.putText(frame, note_label, (label_x, y+5),
                    FONT, font_sz, label_c, font_wt, cv2.LINE_AA)

def draw_hand_glow(frame, px, py, accent, on, pinch, t_now):
    """Pulsing ring on the hand when a note is playing."""
    if on:
        pulse = 0.6 + 0.4 * np.sin(t_now * 8)
        r     = int(28 + 10 * pulse)
        col   = tuple(int(c * pulse) for c in accent)
        cv2.circle(frame, (px, py), r+6, tuple(int(c*0.3) for c in accent), 4, cv2.LINE_AA)
        cv2.circle(frame, (px, py), r,   col, 2, cv2.LINE_AA)
    if pinch:
        cv2.circle(frame, (px, py), 18, C_WHITE, 1, cv2.LINE_AA)

def draw_hud(frame, w, h):
    with engine._lock:
        d_on, d_freq = engine.drone.on, engine.drone.freq
        l_on, l_freq = engine.lead.on,  engine.lead.freq
    by = h - 50
    cv2.rectangle(frame, (0,by), (w,h), C_BG_BAR, -1, cv2.LINE_AA)
    cv2.line(frame, (0,by), (w,by), C_DIM, 1, cv2.LINE_AA)
    dc = C_ACCENT1 if d_on else C_DIM
    lc = C_ACCENT2 if l_on else C_DIM
    cv2.putText(frame, f"DRONE  {freq_to_note_name(d_freq)}  {d_freq:.1f} Hz",
                (12, by+18), FONT, 0.46, dc, 1, cv2.LINE_AA)
    cv2.putText(frame, "● ON" if d_on else "○ OFF",
                (12, by+38), FONT, 0.38, dc, 1, cv2.LINE_AA)
    cv2.putText(frame, f"LEAD   {freq_to_note_name(l_freq)}  {l_freq:.1f} Hz",
                (w-290, by+18), FONT, 0.46, lc, 1, cv2.LINE_AA)
    cv2.putText(frame, "● ON" if l_on else "○ OFF",
                (w-290, by+38), FONT, 0.38, lc, 1, cv2.LINE_AA)

def draw_hints(frame, w, h):
    hints = [
        "ESC quit   S/D drone scale   F/G lead scale   1-4 drone sound   Q-R lead sound",
        "Z/X drone oct   C/V lead oct",
        "Left hand: pinch+drag = step notes   open=play   fist=stop   Right=volume",
    ]
    for i, txt in enumerate(hints):
        cv2.putText(frame, txt, (w//2-310, h-108-i*15),
                    FONT, 0.30, C_DIM, 1, cv2.LINE_AA)

def draw_divider(frame, w, h, top_y, bot_y):
    cv2.line(frame, (w//2, top_y), (w//2, bot_y), (55,55,65), 1, cv2.LINE_AA)


# ══════════════════════════════════════════════════════
#  7. MENU GEOMETRY  (per-voice buttons)
# ══════════════════════════════════════════════════════

def voice_buttons(w):
    """Returns (scale_btn, sound_btn) rects for drone (left) and lead (right)."""
    bw, bh = 148, 30
    pad    = 6
    # Drone: centred in left half
    dl = w//4 - bw - pad//2
    db = (w//4 - bw//2, 6, w//4 + bw//2, 6+bh)
    ds = (w//4 - bw//2, 6+bh+3, w//4 + bw//2, 6+bh+3+bh)
    # Lead: centred in right half
    lb = (3*w//4 - bw//2, 6, 3*w//4 + bw//2, 6+bh)
    ls = (3*w//4 - bw//2, 6+bh+3, 3*w//4 + bw//2, 6+bh+3+bh)
    return db, ds, lb, ls

def in_rect(rect, mx, my):
    return rect[0]<=mx<=rect[2] and rect[1]<=my<=rect[3]


# ══════════════════════════════════════════════════════
#  8. MAIN LOOP
# ══════════════════════════════════════════════════════

cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH,  1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

WIN = "Gesture Synth"
cv2.namedWindow(WIN)
cv2.setMouseCallback(WIN, mouse_cb)

TOP_Y = 65    # top of the playable area (below buttons)
BOT_Y_OFF = 58  # offset from bottom (above HUD)

while cap.isOpened():
    ok, frame = cap.read()
    if not ok:
        break

    frame = cv2.flip(frame, 1)
    h, w  = frame.shape[:2]
    bot_y = h - BOT_Y_OFF
    t_now = time.monotonic()

    frame = cv2.addWeighted(frame, 0.72, np.zeros_like(frame), 0.28, 0)

    rgb     = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = hands_detector.process(rgb)

    db_rect, ds_rect, lb_rect, ls_rect = voice_buttons(w)

    # ── Draw note guidelines (before hands so hands draw on top) ──
    with engine._lock:
        d_on = engine.drone.on
        l_on = engine.lead.on

    draw_note_guidelines(frame, drone_state, "drone",
                         2, w//2 - 2, TOP_Y, bot_y,
                         C_ACCENT1, d_on, t_now)
    draw_note_guidelines(frame, lead_state, "lead",
                         w//2 + 2, w - 2, TOP_Y, bot_y,
                         C_ACCENT2, l_on, t_now)
    draw_divider(frame, w, h, TOP_Y, bot_y)

    # ── Mouse clicks ──
    if mouse_click[0]:
        mx, my = mouse_pos
        def handle_voice_click(vs: VoiceState, scale_btn, sound_btn,
                               other_vs: VoiceState, step_key):
            clicked = False
            if in_rect(scale_btn, mx, my):
                vs.show_scale_menu = not vs.show_scale_menu
                vs.show_sound_menu = False
                other_vs.show_scale_menu = False
                other_vs.show_sound_menu = False
                clicked = True
            elif in_rect(sound_btn, mx, my):
                vs.show_sound_menu = not vs.show_sound_menu
                vs.show_scale_menu = False
                other_vs.show_scale_menu = False
                other_vs.show_sound_menu = False
                clicked = True
            elif vs.show_scale_menu:
                rects = build_rects(SCALE_NAMES, scale_btn[0], scale_btn[3]+3)
                for i,(x1,y1,x2,y2) in enumerate(rects):
                    if x1<=mx<=x2 and y1<=my<=y2:
                        vs.scale_idx = i
                        stepper.reset_step(step_key)
                        break
                vs.show_scale_menu = False
                clicked = True
            elif vs.show_sound_menu:
                rects = build_rects(SOUND_NAMES, sound_btn[0], sound_btn[3]+3)
                for i,(x1,y1,x2,y2) in enumerate(rects):
                    if x1<=mx<=x2 and y1<=my<=y2:
                        vs.sound_idx = i
                        engine.set(**{("drone_sound" if step_key=="drone"
                                       else "lead_sound"): i})
                        break
                vs.show_sound_menu = False
                clicked = True
            return clicked

        c1 = handle_voice_click(drone_state, db_rect, ds_rect, lead_state,  "drone")
        c2 = handle_voice_click(lead_state,  lb_rect, ls_rect, drone_state, "lead")
        if not c1 and not c2:
            drone_state.show_scale_menu = False
            drone_state.show_sound_menu = False
            lead_state.show_scale_menu  = False
            lead_state.show_sound_menu  = False
        mouse_click[0] = False

    # ── Hands ──
    if results.multi_hand_landmarks and results.multi_handedness:
        for i, lms in enumerate(results.multi_hand_landmarks):
            raw   = results.multi_handedness[i].classification[0].label
            label = "Right" if raw == "Left" else "Left"

            pinch, spread, hx, hy = hand_metrics(lms)
            hx = ema(f"{i}_x", hx)
            hy = ema(f"{i}_y", hy)
            px, py = int(hx * w), int(hy * h)

            # ── RIGHT → volume ──
            if label == "Right":
                vol = float(np.clip(1.15 - hy * 1.3, 0.0, 1.0))
                vol = ema("vol", vol)
                engine.set(master_vol=vol)
                draw_volume_bar(frame, vol, px, max(10, py - 70))
                cv2.putText(frame, "VOL", (px-12, max(8, py-80)),
                            FONT, 0.36, C_GREEN, 1, cv2.LINE_AA)

            # ── LEFT → pitch + trigger ──
            if label == "Left":
                is_drone = hx < 0.5
                vs       = drone_state if is_drone else lead_state
                accent   = C_ACCENT1   if is_drone else C_ACCENT2
                ok_key   = "drone_open" if is_drone else "lead_open"
                off_key  = "drone_fist" if is_drone else "lead_fist"
                on_key   = "drone_on"   if is_drone else "lead_on"
                fr_key   = "drone_freq" if is_drone else "lead_freq"
                step_key = "drone"      if is_drone else "lead"

                # Trigger
                if timers[ok_key].update(spread > 0.20):
                    engine.set(**{on_key: True})
                if timers[off_key].update(spread < 0.10):
                    engine.set(**{on_key: False})

                # Pitch (step-based, pinch only)
                freq = stepper.update(step_key, pinch, hy, vs.scale, vs.octave)
                if pinch:
                    engine.set(**{fr_key: freq})

                with engine._lock:
                    cur_on = engine.drone.on if is_drone else engine.lead.on

                draw_hand_glow(frame, px, py, accent, cur_on, pinch, t_now)

            mp_draw.draw_landmarks(
                frame, lms, HAND_CONN,
                mp_draw.DrawingSpec(color=(170,170,200), thickness=1, circle_radius=2),
                mp_draw.DrawingSpec(color=(85, 85, 110),  thickness=1),
            )

    # ── UI buttons & dropdowns ──
    for vs, scale_btn, sound_btn, accent, step_key in [
        (drone_state, db_rect, ds_rect, C_ACCENT1, "drone"),
        (lead_state,  lb_rect, ls_rect, C_ACCENT2, "lead"),
    ]:
        hov_sc = in_rect(scale_btn, *mouse_pos)
        hov_sn = in_rect(sound_btn, *mouse_pos)
        draw_btn(frame, f"◀ {vs.scale_name} ▶", scale_btn,
                 vs.show_scale_menu, hov_sc, accent)
        draw_btn(frame, f"♪ {vs.sound_name}",   sound_btn,
                 vs.show_sound_menu, hov_sn, accent)
        if vs.show_scale_menu:
            rects = build_rects(SCALE_NAMES, scale_btn[0], scale_btn[3]+3)
            draw_dropdown(frame, SCALE_NAMES, rects, vs.scale_idx, accent)
        if vs.show_sound_menu:
            rects = build_rects(SOUND_NAMES, sound_btn[0], sound_btn[3]+3)
            draw_dropdown(frame, SOUND_NAMES, rects, vs.sound_idx, accent)

    draw_hud(frame, w, h)
    draw_hints(frame, w, h)

    # Zone labels
    cv2.putText(frame, "DRONE", (w//4 - 30, bot_y + 8),
                FONT, 0.48, C_ACCENT1, 1, cv2.LINE_AA)
    cv2.putText(frame, "LEAD",  (3*w//4 - 22, bot_y + 8),
                FONT, 0.48, C_ACCENT2, 1, cv2.LINE_AA)
    cv2.putText(frame, f"Oct {drone_state.octave}", (16, TOP_Y + 16),
                FONT, 0.36, C_ACCENT1, 1, cv2.LINE_AA)
    cv2.putText(frame, f"Oct {lead_state.octave}",  (w//2 + 8, TOP_Y + 16),
                FONT, 0.36, C_ACCENT2, 1, cv2.LINE_AA)

    cv2.imshow(WIN, frame)

    # ── Keyboard ──
    key = cv2.waitKey(1) & 0xFF
    if key == 27:
        break
    # Drone octave
    elif key == ord('z'): drone_state.octave = max(0, drone_state.octave - 1)
    elif key == ord('x'): drone_state.octave = min(6, drone_state.octave + 1)
    # Lead octave
    elif key == ord('c'): lead_state.octave  = max(0, lead_state.octave  - 1)
    elif key == ord('v'): lead_state.octave  = min(6, lead_state.octave  + 1)
    # Drone scale  S=prev  D=next
    elif key == ord('s'):
        drone_state.scale_idx = (drone_state.scale_idx - 1) % len(SCALE_NAMES)
        stepper.reset_step("drone")
    elif key == ord('d'):
        drone_state.scale_idx = (drone_state.scale_idx + 1) % len(SCALE_NAMES)
        stepper.reset_step("drone")
    # Lead scale  F=prev  G=next
    elif key == ord('f'):
        lead_state.scale_idx = (lead_state.scale_idx - 1) % len(SCALE_NAMES)
        stepper.reset_step("lead")
    elif key == ord('g'):
        lead_state.scale_idx = (lead_state.scale_idx + 1) % len(SCALE_NAMES)
        stepper.reset_step("lead")
    # Drone sound  1-4
    elif ord('1') <= key <= ord('4'):
        idx = key - ord('1')
        drone_state.sound_idx = idx
        engine.set(drone_sound=idx)
    # Lead sound  Q=0 W=1 E=2 R=3
    elif key == ord('q'): lead_state.sound_idx = 0; engine.set(lead_sound=0)
    elif key == ord('w'): lead_state.sound_idx = 1; engine.set(lead_sound=1)
    elif key == ord('e'): lead_state.sound_idx = 2; engine.set(lead_sound=2)
    elif key == ord('r'): lead_state.sound_idx = 3; engine.set(lead_sound=3)

cap.release()
stream.stop()
cv2.destroyAllWindows()