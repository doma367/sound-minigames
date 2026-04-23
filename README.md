# Somatun

**Gesture-Driven Audio Playground**

Somatun is a real-time gesture-controlled audio system that lets you create and 
manipulate sound through body movement. Your webcam is the only hardware you need.

Built with JUCE (C++) and MediaPipe (Python).

---

## Modes

### Fleshsynth
A body-controlled wavetable synthesizer. Six tracked body joints — your wrists, 
elbows, and shoulders — form a polyline that is resampled into a live waveform 
buffer. Move to sculpt sound. Head tilt controls pitch via vibrato.

### Pulsefield
A gesture-controlled step sequencer with a real-time effects chain. Program beats 
on a grid, then warp them with your hands — right hand controls a low-pass filter 
and bit-crusher, left hand controls delay time and feedback. Ships with kick, snare, 
and hi-hat voices. Load your own samples in WAV, AIFF, or MP3.

### Dualcast
A two-voice gesture synthesizer. Your left hand triggers and steps through scale 
notes across two independent voices (Drone and Lead). Your right hand controls 
master volume. Pinch and drag to select pitches. Open hand to trigger, fist to 
release. Four synthesis algorithms per voice: Sine, Organ, Soft Pad, and 
Karplus-Strong Pluck.

---

## Requirements

- macOS 12 or later
- Python 3.11 ([python.org](https://www.python.org/downloads/))
- A webcam

---

## Installation

### 1. Download Somatun
Download `Somatun.dmg` from the [latest release](https://github.com/doma367/sound-minigames/releases/latest).

Open the DMG and drag `somatun_juce` into your Applications folder.

### 2. Install Python dependencies
Open Terminal and run:

```bash
pip3 install mediapipe==0.10.9 opencv-python pythonosc numpy
```

### 3. Allow the app to open
Because Somatun is not signed with an Apple Developer certificate, macOS will 
block it on first launch. To open it:

1. Right-click `somatun_juce` in Applications
2. Select **Open**
3. Click **Open** in the dialog that appears

You only need to do this once.

### 4. Grant camera and microphone access
On first launch, macOS will ask for camera and microphone permission. Grant both.

---

## Building from source

### Prerequisites
- Xcode 14 or later
- JUCE 7 ([juce.com](https://juce.com))
- Python 3.11 with dependencies above

### Steps
1. Clone the repository:
```bash
   git clone https://github.com/doma367/sound-minigames.git
```
2. Open `somatun_JUCE/somatun_juce/somatun_juce.jucer` in Projucer
3. Save and open in Xcode
4. Build and run

---

## Architecture

Somatun uses a hybrid architecture:

- **Python process** — captures webcam frames, runs MediaPipe pose and hand 
  tracking, sends normalized landmark data over OSC (UDP port 9000) and JPEG 
  frames over TCP (port 9001)
- **JUCE application** — receives OSC data, runs the audio engine, renders the UI

The Python process is launched automatically by the app at startup and terminated 
when you return to the landing page.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

*Domokos Márk — Apáczai Csere János High School, Cluj-Napoca, 2026*  
*Dissertation project, Mathematics-Informatics class*