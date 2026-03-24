import numpy as np
import sounddevice as sd

# audio settings
SAMPLE_RATE = 48000
DURATION = 2.0
FREQUENCY = 440.0

def main():
    # time axis
    t = np.linspace(0, DURATION, int(SAMPLE_RATE * DURATION), endpoint=False)

    # generate sine wave between -1 and 1
    wave = 0.2 * np.sin(2 * np.pi * FREQUENCY * t)  # 0.2 = volume 20%

    print(f"playing {FREQUENCY} Hz sine for {DURATION} seconds...")
    sd.play(wave, SAMPLE_RATE)
    sd.wait()  # block until finished
    print("done")

if __name__ == "__main__":
    main()
