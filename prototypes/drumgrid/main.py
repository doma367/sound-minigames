"""
Gesture-controlled drum machine
================================
Controls
--------
Right hand
  • Index tip height           → low-pass filter cutoff
  • Pinch (thumb ↔ index)      → bit-crush on/off

Left hand
  • Wrist height               → delay feedback
  • Hand tilt                  → delay echo time
  • Peace sign ✌               → toggle delay lock

Mouse / UI
  • Sequencer cells            → toggle steps on/off
  • 8 / 16 / 32 buttons        → set active step count
  • + Add voice                → open file browser (WAV/MP3)
  • ✕ on custom row            → remove that voice
  • Per-row volume sliders     → mix each drum independently
  • REVERB SIZE / WET sliders  → reverb amount and room size
  • SET BPM                    → tap-tempo modal

Keyboard
  • Space                      → pause / resume
  • ↑ / ↓                      → nudge BPM ±5
"""

import threading
import time
import subprocess
import sys
import os

import cv2
import numpy as np
import pygame
import sounddevice as sd

from audio import DrumMachine, SAMPLE_RATE
from vision import HandTracker
from ui import UI, STEP_OPTIONS

# ── Constants ──────────────────────────────────────────────────────────
BPM_DEFAULT  = 120
MAX_STEPS    = 32
INITIAL_ROWS = 3

# ── Shared grid (rows × MAX_STEPS) ────────────────────────────────────
grid = np.zeros((INITIAL_ROWS, MAX_STEPS), dtype=bool)
grid[0, 0] = grid[0, 4] = True
grid[1, 2] = grid[1, 6] = True

# ── Shared state ───────────────────────────────────────────────────────
_state = {
    "bpm":            BPM_DEFAULT,
    "hand_y":         0.5,
    "is_fist":        False,
    "delay_time_val": 0.1,
    "delay_feedback": 0.0,
    "delay_locked":   False,
    "is_paused":      False,
    "reverb_size":    1.0,
    "reverb_wet":     0.0,
    "active_steps":   8,
    "row_volumes":    [0.8] * INITIAL_ROWS,
}
_state_lock = threading.Lock()


def get_state():
    with _state_lock:
        return dict(_state)


def update_state(**kwargs):
    with _state_lock:
        _state.update(kwargs)


# ── File browser ───────────────────────────────────────────────────────
def open_file_dialog() -> str | None:
    """
    Open a native file picker without touching the pygame run loop.
    On macOS uses osascript (no tkinter/SDL conflict).
    On other platforms falls back to tkinter in a thread.
    """
    if sys.platform == "darwin":
        script = (
            'tell application "System Events"\n'
            '  activate\n'
            '  set f to choose file with prompt "Select drum sample" '
            'of type {"wav", "mp3", "WAV", "MP3"}\n'
            '  return POSIX path of f\n'
            'end tell'
        )
        try:
            result = subprocess.run(
                ["osascript", "-e", script],
                capture_output=True, text=True, timeout=60,
            )
            path = result.stdout.strip()
            return path if path else None
        except Exception:
            return None
    else:
        # Windows / Linux: run tkinter in its own thread so it doesn't
        # conflict with pygame's event loop
        result_holder: list[str | None] = [None]
        done = threading.Event()

        def _pick():
            try:
                import tkinter as tk
                from tkinter import filedialog
                root = tk.Tk()
                root.withdraw()
                root.attributes("-topmost", True)
                path = filedialog.askopenfilename(
                    title="Select drum sample",
                    filetypes=[("Audio", "*.wav *.mp3"), ("WAV", "*.wav"), ("MP3", "*.mp3")],
                )
                root.destroy()
                result_holder[0] = path if path else None
            except Exception:
                pass
            finally:
                done.set()

        t = threading.Thread(target=_pick, daemon=True)
        t.start()
        done.wait(timeout=60)
        return result_holder[0]


# ── Grid helpers ───────────────────────────────────────────────────────
def add_grid_row():
    global grid
    new_row = np.zeros((1, MAX_STEPS), dtype=bool)
    grid = np.vstack([grid, new_row])


def remove_grid_row(row: int):
    global grid
    grid = np.delete(grid, row, axis=0)


# ── Drag-and-drop helper ───────────────────────────────────────────────
def _path_from_drop(event) -> str | None:
    """Extract a file path from a pygame DROPFILE event."""
    try:
        return event.file
    except AttributeError:
        return None


# ── Main ───────────────────────────────────────────────────────────────
def main():
    global grid

    pygame.init()
    pygame.display.set_caption("Gesture Drum Machine")
    screen = pygame.display.set_mode((1060, 820), pygame.RESIZABLE)
    clock  = pygame.time.Clock()

    # Enable drag-and-drop
    pygame.event.set_allowed([
        pygame.QUIT, pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP,
        pygame.MOUSEMOTION, pygame.KEYDOWN, pygame.VIDEORESIZE,
        pygame.DROPFILE,
    ])

    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Could not open camera (index 0).")

    tracker = HandTracker()
    ui      = UI(steps=MAX_STEPS, rows=INITIAL_ROWS)
    engine  = DrumMachine(grid=grid, get_state=get_state)

    tap_mode  = False
    tap_times: list[float] = []
    ebtn      = None
    error_msg: str | None = None   # shown briefly after a bad file load
    error_t   = 0.0

    with sd.OutputStream(samplerate=SAMPLE_RATE, channels=1, callback=engine.callback):
        while True:
            W, H = screen.get_size()

            # ── Camera + gesture ──────────────────────────────────────
            ret, frame = cap.read()
            if not ret:
                break
            frame     = cv2.flip(frame, 1)
            rgb_frame, gesture = tracker.process(frame)

            if not tap_mode:
                update_state(
                    hand_y        = gesture.hand_y,
                    is_fist       = gesture.is_fist,
                    delay_time_val= gesture.delay_time_val,
                    delay_feedback= gesture.delay_feedback,
                    delay_locked  = gesture.delay_locked,
                )

            # Sync row volumes from UI sliders
            update_state(
                row_volumes  = ui.get_row_volumes(),
                reverb_size  = ui.slider_size.value,
                reverb_wet   = ui.slider_wet.value,
            )

            # ── Draw ──────────────────────────────────────────────────
            active_steps = _state["active_steps"]
            visual_step  = engine.get_synced_step(active_steps)
            waveform     = engine.get_waveform()
            hit_envs     = engine.get_hit_envelopes()

            tap_btn, ebtn = ui.draw(
                screen       = screen,
                cam_rgb      = rgb_frame,
                grid         = grid,
                visual_step  = visual_step,
                gesture      = gesture,
                bpm          = _state["bpm"],
                tap_mode     = tap_mode,
                active_steps = active_steps,
                waveform     = waveform,
                hit_envs     = hit_envs,
                voice_names  = engine.voice_names,
            )

            # Error toast
            if error_msg and time.time() - error_t < 3.0:
                es = ui.font.render(f"⚠ {error_msg}", True, (255, 80, 80))
                screen.blit(es, (W // 2 - es.get_width() // 2, H - 80))
            else:
                error_msg = None

            # ── Events ────────────────────────────────────────────────
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    _cleanup(cap, tracker)
                    return

                # Always forward to sliders (reverb + volume)
                for sl in ui.sliders + ui.vol_sliders:
                    sl.handle_event(event)

                # Drag-and-drop file onto window
                if event.type == pygame.DROPFILE:
                    path = _path_from_drop(event)
                    if path:
                        err = _load_voice(path, engine, ui)
                        if err:
                            error_msg = err
                            error_t   = time.time()

                if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    mx, my = event.pos

                    if tap_mode:
                        box = pygame.Rect(W // 2 - 200, H // 2 - 100, 400, 200)
                        if ebtn and ebtn.collidepoint(mx, my):
                            tap_mode = False
                            update_state(is_paused=False)
                        elif box.collidepoint(mx, my):
                            now = time.time()
                            tap_times.append(now)
                            if len(tap_times) > 5:
                                tap_times.pop(0)
                            if len(tap_times) >= 2:
                                ivls = [tap_times[k+1] - tap_times[k]
                                        for k in range(len(tap_times) - 1)]
                                new_bpm = int(np.clip(60 / (sum(ivls) / len(ivls)), 40, 240))
                                update_state(bpm=new_bpm)
                    else:
                        if tap_btn.collidepoint(mx, my):
                            tap_mode = True
                            tap_times = []
                            update_state(is_paused=True)

                        elif (opt := ui.hit_step_btn(mx, my)) is not None:
                            update_state(active_steps=opt)

                        elif ui.hit_add_btn(mx, my):
                            path = open_file_dialog()
                            if path:
                                err = _load_voice(path, engine, ui)
                                if err:
                                    error_msg = err
                                    error_t   = time.time()

                        elif (row := ui.hit_del_btn(mx, my)) is not None:
                            try:
                                engine.remove_voice(row)
                                remove_grid_row(row)
                                engine.grid = grid
                                ui.rows = engine.rows
                            except ValueError as e:
                                error_msg = str(e)
                                error_t   = time.time()

                        elif (cell := ui.hit_step(mx, my, active_steps, grid.shape[0])) is not None:
                            r, c = cell
                            grid[r, c] = not grid[r, c]

                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_SPACE:
                        update_state(is_paused=not _state["is_paused"])
                    elif event.key == pygame.K_UP:
                        update_state(bpm=min(240, _state["bpm"] + 5))
                    elif event.key == pygame.K_DOWN:
                        update_state(bpm=max(40, _state["bpm"] - 5))

            pygame.display.flip()
            clock.tick(60)


def _load_voice(path: str, engine: DrumMachine, ui: UI) -> str | None:
    """
    Try to load a voice file. Returns error string on failure, None on success.
    """
    global grid
    try:
        engine.add_voice(path)
        add_grid_row()
        engine.grid = grid
        ui.rows     = engine.rows
        ui._rebuild_vol_sliders(engine.rows)
        with _state_lock:
            _state["row_volumes"] = ui.get_row_volumes()
        return None
    except Exception as exc:
        return str(exc)


def _cleanup(cap, tracker):
    tracker.release()
    cap.release()
    pygame.quit()


if __name__ == "__main__":
    main()