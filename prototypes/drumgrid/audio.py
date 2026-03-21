import threading
import numpy as np

try:
    import scipy.io.wavfile as wavfile
    _HAVE_SCIPY = True
except ImportError:
    _HAVE_SCIPY = False

try:
    from pydub import AudioSegment
    _HAVE_PYDUB = True
except ImportError:
    _HAVE_PYDUB = False

SAMPLE_RATE = 44100
WAVEFORM_BUF_SAMPLES = 2048   # samples kept for oscilloscope display


def load_sample(path: str) -> np.ndarray:
    """
    Load WAV or MP3 → mono float32, resampled to SAMPLE_RATE if needed.
    Raises RuntimeError with a user-readable message on failure.
    """
    path = str(path)
    ext  = path.rsplit(".", 1)[-1].lower()

    if ext == "wav":
        if not _HAVE_SCIPY:
            raise RuntimeError("pip install scipy  (needed for WAV loading)")
        sr, data = wavfile.read(path)
        if data.dtype == np.int16:
            data = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            data = data.astype(np.float32) / 2147483648.0
        elif data.dtype == np.uint8:
            data = (data.astype(np.float32) - 128) / 128.0
        else:
            data = data.astype(np.float32)
        if data.ndim == 2:
            data = data.mean(axis=1)
        if sr != SAMPLE_RATE:
            new_len = int(len(data) * SAMPLE_RATE / sr)
            data = np.interp(
                np.linspace(0, len(data) - 1, new_len),
                np.arange(len(data)), data,
            ).astype(np.float32)
        return data.astype(np.float32)

    elif ext == "mp3":
        if not _HAVE_PYDUB:
            raise RuntimeError("pip install pydub  (needed for MP3 loading)")
        seg  = AudioSegment.from_mp3(path).set_channels(1).set_frame_rate(SAMPLE_RATE)
        data = np.array(seg.get_array_of_samples(), dtype=np.float32)
        data /= 2 ** (seg.sample_width * 8 - 1)
        return data

    else:
        raise RuntimeError(f"Unsupported format .{ext} — use WAV or MP3")


class DrumMachine:
    DEFAULT_NAMES = ["Kick", "Snare", "Hi-hat"]

    def __init__(self, grid, get_state):
        self.grid      = grid          # bool array (rows, MAX_STEPS); rows may grow
        self.get_state = get_state
        self.rows, self.max_steps = grid.shape

        self._sample_lock = threading.Lock()
        self.samples      = [self._kick(), self._snare(), self._hat()]
        self.voice_names  = list(self.DEFAULT_NAMES)

        self.active_sounds: list = []

        self.samples_per_step        = 0
        self.next_trigger            = 0
        self.total_samples_processed = 0
        self.output_latency_samples  = 0

        # Delay
        self.delay_buf_size = SAMPLE_RATE * 2
        self.delay_buffer   = np.zeros(self.delay_buf_size, dtype=np.float32)
        self.delay_ptr      = 0

        self.filter_state = 0.0

        # Smoothed params
        self._smooth_alpha       = 1.0
        self._smooth_delay_time  = 0.1
        self._smooth_delay_fb    = 0.0
        self._smooth_reverb_size = 1.0
        self._smooth_reverb_wet  = 0.0

        # Schroeder reverb
        self._comb_base = np.array([1557, 1617, 1491, 1422], dtype=np.int32)
        self._ap_base   = np.array([225,  556],               dtype=np.int32)
        _cmax = int(self._comb_base.max() * 3.0) + 1
        _amax = int(self._ap_base.max()   * 3.0) + 1
        self._comb_bufs = [np.zeros(_cmax, dtype=np.float32) for _ in range(4)]
        self._ap_bufs   = [np.zeros(_amax, dtype=np.float32) for _ in range(2)]
        self._comb_ptrs = np.zeros(4, dtype=np.int32)
        self._ap_ptrs   = np.zeros(2, dtype=np.int32)

        # Waveform ring buffer (oscilloscope)
        self._waveform_buf = np.zeros(WAVEFORM_BUF_SAMPLES, dtype=np.float32)
        self._waveform_ptr = 0

        # Per-hit envelopes for hit display
        self._env_lock      = threading.Lock()
        self._hit_envelopes: list = []   # each: [sample_array, position]

        self._bpm_cache = None
        self.update_timing(120)

    # ------------------------------------------------------------------
    # Built-in synthesis
    # ------------------------------------------------------------------
    def _kick(self):
        t = np.linspace(0, 0.1, int(SAMPLE_RATE * 0.1), endpoint=False)
        return (np.sin(2 * np.pi * 150 * (0.5 ** (t / 0.03)) * t)
                * np.exp(-t * 40)).astype(np.float32)

    def _snare(self):
        t = np.linspace(0, 0.1, int(SAMPLE_RATE * 0.1), endpoint=False)
        return (np.random.normal(0, 0.2, len(t))
                * np.exp(-t * 60)).astype(np.float32)

    def _hat(self):
        t = np.linspace(0, 0.03, int(SAMPLE_RATE * 0.03), endpoint=False)
        return (np.random.uniform(-0.1, 0.1, len(t))
                * np.exp(-t * 120)).astype(np.float32)

    # ------------------------------------------------------------------
    # Voice management  (main thread only)
    # ------------------------------------------------------------------
    def add_voice(self, path: str) -> str:
        """Load file, append row. Returns display name. Raises on error."""
        data = load_sample(path)
        name = path.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0][:16]
        with self._sample_lock:
            self.samples.append(data)
            self.voice_names.append(name)
            self.rows = len(self.samples)
        return name

    def remove_voice(self, row: int):
        """Remove voice at row index. At least one voice must remain."""
        with self._sample_lock:
            if len(self.samples) <= 1:
                raise ValueError("Cannot remove the last remaining voice.")
            self.samples.pop(row)
            self.voice_names.pop(row)
            self.rows = len(self.samples)

    # ------------------------------------------------------------------
    # Waveform / envelope accessors  (UI thread)
    # ------------------------------------------------------------------
    def get_waveform(self) -> np.ndarray:
        ptr = self._waveform_ptr
        return np.roll(self._waveform_buf.copy(), -ptr)

    def get_hit_envelopes(self) -> list:
        with self._env_lock:
            return [(e[0], e[1]) for e in self._hit_envelopes]

    # ------------------------------------------------------------------
    # Timing
    # ------------------------------------------------------------------
    def update_timing(self, bpm: int):
        if bpm == self._bpm_cache:
            return
        self._bpm_cache       = bpm
        self.samples_per_step = max(1, int(SAMPLE_RATE * (60 / bpm / 2)))
        self.next_trigger     = 0

    def get_synced_step(self, active_steps: int) -> int:
        synced = max(0, self.total_samples_processed - self.output_latency_samples)
        return (synced // self.samples_per_step) % active_steps

    # ------------------------------------------------------------------
    # Audio callback
    # ------------------------------------------------------------------
    def callback(self, outdata: np.ndarray, frames: int, time_info, status):
        state = self.get_state()

        outdata.fill(0)
        if state["is_paused"]:
            return

        self.update_timing(state["bpm"])

        self.output_latency_samples = int(
            (time_info.outputBufferDacTime - time_info.currentTime) * SAMPLE_RATE
        )

        SMOOTH_K = 0.05

        # Smooth all UI-driven params
        for attr, raw in [
            ("_smooth_reverb_size", float(state.get("reverb_size", 1.0))),
            ("_smooth_reverb_wet",  float(state.get("reverb_wet",  0.0))),
            ("_smooth_delay_time",  float(state["delay_time_val"])),
            ("_smooth_delay_fb",    float(state["delay_feedback"])),
        ]:
            cur = getattr(self, attr)
            setattr(self, attr, cur + SMOOTH_K * (raw - cur))

        raw_y        = float(state["hand_y"])
        clamped_y    = np.clip((raw_y - 0.2) / 0.5, 0.0, 1.0)
        target_alpha = float(np.clip((1.0 - clamped_y) ** 2.0, 0.01, 0.95))
        self._smooth_alpha += SMOOTH_K * (target_alpha - self._smooth_alpha)

        alpha          = self._smooth_alpha
        delay_time_val = self._smooth_delay_time
        delay_feedback = self._smooth_delay_fb
        reverb_size    = self._smooth_reverb_size
        reverb_wet     = self._smooth_reverb_wet
        is_fist        = bool(state["is_fist"])
        active_steps   = int(state.get("active_steps", self.max_steps))
        row_volumes    = state.get("row_volumes", [])

        with self._sample_lock:
            samples_snap = list(self.samples)
        n_rows = len(samples_snap)

        # --- Sequencer ---
        buf_start      = self.total_samples_processed
        buf_end        = buf_start + frames
        trigger_sample = buf_start + self.next_trigger

        while trigger_sample < buf_end:
            step   = (trigger_sample // self.samples_per_step) % active_steps
            offset = int(trigger_sample - buf_start)
            for r in range(min(n_rows, self.grid.shape[0])):
                if step < self.grid.shape[1] and self.grid[r, int(step)]:
                    vol = float(row_volumes[r]) if r < len(row_volumes) else 1.0
                    self.active_sounds.append([samples_snap[r], 0, offset, vol])
                    with self._env_lock:
                        self._hit_envelopes.append([samples_snap[r], 0])
            trigger_sample += self.samples_per_step

        self.next_trigger = trigger_sample - buf_end

        # --- Mix ---
        mix          = np.zeros(frames, dtype=np.float32)
        still_active = []
        for snd in self.active_sounds:
            data, pos, start_off, vol = snd
            dst   = max(0, start_off)
            count = min(frames - dst, len(data) - pos)
            if count > 0:
                mix[dst: dst + count] += data[pos: pos + count] * vol
                snd[1] += count
                snd[2]  = 0
            if snd[1] < len(snd[0]):
                still_active.append(snd)
        self.active_sounds = still_active

        # Advance hit envelopes
        with self._env_lock:
            alive = []
            for e in self._hit_envelopes:
                e[1] += frames
                if e[1] < len(e[0]):
                    alive.append(e)
            self._hit_envelopes = alive

        # --- Filter ---
        filtered = np.empty(frames, dtype=np.float32)
        fs = self.filter_state
        for i in range(frames):
            fs += alpha * (mix[i] - fs)
            filtered[i] = fs
        self.filter_state = fs

        # --- Bit-crush ---
        dry = np.round(filtered * 5) / 5 if is_fist else filtered

        # --- Delay ---
        delay_samples = int(np.clip(delay_time_val, 0.02, 0.8) * SAMPLE_RATE)
        out = np.empty(frames, dtype=np.float32)
        dp, db, dbs = self.delay_ptr, self.delay_buffer, self.delay_buf_size
        for i in range(frames):
            rp      = (dp - delay_samples) % dbs
            wet     = db[rp]
            db[dp]  = dry[i] + wet * delay_feedback
            dp      = (dp + 1) % dbs
            out[i]  = dry[i] + (wet * 0.3 if delay_feedback > 0.05 else 0.0)
        self.delay_ptr = dp

        # --- Reverb ---
        if reverb_wet > 0.01:
            comb_fb    = np.clip(0.72 + reverb_size * 0.08, 0.0, 0.92)
            reverb_out = np.zeros(frames, dtype=np.float32)

            for k in range(4):
                dlen     = int(self._comb_base[k] * reverb_size)
                buf      = self._comb_bufs[k]
                ptr      = self._comb_ptrs[k]
                bsz      = len(buf)
                csig     = np.empty(frames, dtype=np.float32)
                for i in range(frames):
                    r         = (ptr - dlen) % bsz
                    d         = buf[r]
                    buf[ptr]  = out[i] + d * comb_fb
                    csig[i]   = d
                    ptr       = (ptr + 1) % bsz
                self._comb_ptrs[k] = ptr
                reverb_out += csig
            reverb_out *= 0.25

            for k in range(2):
                dlen    = int(self._ap_base[k] * reverb_size)
                buf     = self._ap_bufs[k]
                ptr     = self._ap_ptrs[k]
                bsz     = len(buf)
                asig    = np.empty(frames, dtype=np.float32)
                ap_fb   = 0.5
                for i in range(frames):
                    r         = (ptr - dlen) % bsz
                    d         = buf[r]
                    v         = reverb_out[i] - ap_fb * d
                    buf[ptr]  = reverb_out[i] + ap_fb * d
                    asig[i]   = v
                    ptr       = (ptr + 1) % bsz
                self._ap_ptrs[k] = ptr
                reverb_out = asig

            out = np.tanh(out * (1.0 - reverb_wet * 0.5) + reverb_out * reverb_wet)
        else:
            out = np.tanh(out)

        # --- Waveform ring buffer ---
        wp  = self._waveform_ptr
        wbs = WAVEFORM_BUF_SAMPLES
        sp  = wbs - wp
        if frames <= sp:
            self._waveform_buf[wp: wp + frames] = out
            self._waveform_ptr = (wp + frames) % wbs
        else:
            self._waveform_buf[wp:]          = out[:sp]
            self._waveform_buf[:frames - sp] = out[sp:]
            self._waveform_ptr               = frames - sp

        outdata[:, 0] = out
        self.total_samples_processed += frames