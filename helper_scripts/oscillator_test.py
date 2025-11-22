import numpy as np
import sounddevice as sd

sample_rate = 44100
buffer_size = 512

# fake waveform for testing (simple sine)
test_wave = np.sin(2 * np.pi * np.linspace(0, 1, buffer_size, endpoint=False)).astype(np.float32)

frequency = 440.0
phase = 0.0

def audio_callback(outdata, frames, time, status):
    global phase

    wave = test_wave  # (your real current_wave_buffer will go here)

    out = np.zeros(frames, dtype=np.float32)
    phase_inc = (frequency * buffer_size) / sample_rate

    for i in range(frames):
        # linear interpolation
        idx = int(phase) % buffer_size
        frac = phase - int(phase)
        next_idx = (idx + 1) % buffer_size

        sample = wave[idx] * (1 - frac) + wave[next_idx] * frac
        out[i] = sample

        phase += phase_inc
        if phase >= buffer_size:
            phase -= buffer_size

    outdata[:] = out.reshape(-1, 1)

stream = sd.OutputStream(
    channels=1,
    samplerate=sample_rate,
    blocksize=256,
    callback=audio_callback
)

print("playing 440hz… ctrl+C to stop")
with stream:
    import time
    while True:
        time.sleep(0.1)
