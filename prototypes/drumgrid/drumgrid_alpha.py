import numpy as np
import sounddevice as sd
import cv2
import mediapipe as mp
import threading
import pygame
import time

# ===== CONFIG =====
bpm = 120
bpm_min = 60
bpm_max = 180
steps = 8
rows = 3
sample_rate = 44100
buffer_size = 256
cell_size = 50
margin = 5
cam_width, cam_height = 320, 240
scale_factor = 2

seconds_per_step = 60 / bpm / 2
samples_per_step = int(sample_rate * seconds_per_step)

# ===== GRID =====
grid = [
    [True, False, False, False, True, False, False, False],
    [False, False, True, False, False, False, True, False],
    [False, True, False, True, False, True, False, True],
]

current_step = 0
highlight_step = 0
active_sounds = []

# ===== DRUMS =====
def make_kick(length=0.1):
    t = np.linspace(0,length,int(sample_rate*length),False)
    freq = 150*(0.5**(t/length))
    wave = np.sin(2*np.pi*freq*t)
    env = np.exp(-t*20)
    return (wave*env).astype(np.float32)

def make_snare(length=0.05):
    t = np.linspace(0,length,int(sample_rate*length),False)
    noise = np.random.normal(0,1,len(t))
    mid = np.sin(2*np.pi*800*t)
    env = np.exp(-t*60)
    return (noise*mid*env*0.5).astype(np.float32)

def make_hat(length=0.03):
    t = np.linspace(0,length,int(sample_rate*length),False)
    noise = np.random.uniform(-1,1,len(t))
    high = np.sin(2*np.pi*5000*t)
    env = np.exp(-t*80)
    return (noise*high*env*0.3).astype(np.float32)

samples = [make_kick(), make_snare(), make_hat()]

# ===== AUDIO =====
step_counter = samples_per_step
highlight_queue = []
highlight_delay_frames = int(0.08 * sample_rate / buffer_size)  # ~80ms delay

def audio_callback(outdata, frames, time_info, status):
    global step_counter, current_step, highlight_queue
    outdata.fill(0)
    step_counter -= frames
    if step_counter <= 0:
        step_counter += samples_per_step
        highlight_queue.append(current_step)
        for r in range(rows):
            if grid[r][current_step]:
                active_sounds.append(samples[r].copy())
        current_step = (current_step + 1) % steps

    # pop the highlight after delay
    global highlight_step
    if len(highlight_queue) > highlight_delay_frames:
        highlight_step = highlight_queue.pop(0)

    finished = []
    for i,sound in enumerate(active_sounds):
        chunk = sound[:frames]
        outdata[:len(chunk),0] += chunk
        active_sounds[i] = sound[len(chunk):]
        if len(active_sounds[i])==0:
            finished.append(i)
    for i in reversed(finished):
        active_sounds.pop(i)

# ===== MEDIAPIPE =====
mp_hands = mp.solutions.hands
hands = mp_hands.Hands(max_num_hands=1)
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, cam_width)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, cam_height)

# ===== PYGAME =====
pygame.init()
win_width = cam_width*scale_factor
win_height = cam_height*scale_factor + rows*(cell_size+margin) + 80
screen = pygame.display.set_mode((win_width,win_height))
pygame.display.set_caption("DrumGrid")
font = pygame.font.SysFont(None,24)

camera_surface = pygame.Surface((cam_width*scale_factor, cam_height*scale_factor))
running = True

# ===== TAP BPM =====
tap_mode = False
tap_times = []

tap_panel_rect = pygame.Rect(win_width//2-150, win_height//2-100, 300, 200)
tap_close_rect = pygame.Rect(tap_panel_rect.right-30, tap_panel_rect.top+10, 20, 20)
tap_button_rect = pygame.Rect(50, win_height-40, 60, 20)

bpm_dragging = False

def update_timing():
    global seconds_per_step, samples_per_step
    seconds_per_step = 60 / bpm / 2
    samples_per_step = int(sample_rate * seconds_per_step)

def register_tap():
    global bpm, tap_times
    now = time.time()
    tap_times.append(now)
    if len(tap_times)>5:
        tap_times.pop(0)
    if len(tap_times)>=2:
        intervals = [tap_times[i+1]-tap_times[i] for i in range(len(tap_times)-1)]
        avg = sum(intervals)/len(intervals)
        bpm = int(60/avg)
        bpm = max(bpm_min, min(bpm_max, bpm))
        update_timing()

# ===== TRACKING THREAD =====
def tracking_thread():
    global running
    while running:
        ret, frame = cap.read()
        if not ret:
            continue
        frame = cv2.flip(frame,1)
        frame = cv2.resize(frame,(cam_width*scale_factor, cam_height*scale_factor))
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        frame = np.rot90(frame)
        camera_surface.blit(pygame.surfarray.make_surface(frame),(0,0))

threading.Thread(target=tracking_thread,daemon=True).start()

# ===== DRAW =====
def draw():
    screen.fill((30,30,30))
    screen.blit(camera_surface,(0,0))

    for r in range(rows):
        for c in range(steps):
            x = margin + c*(cell_size+margin)
            y = cam_height*scale_factor + margin + r*(cell_size+margin)
            color = (0,200,0) if grid[r][c] else (70,70,70)
            if c==highlight_step:
                color = (200,200,0)
            s = pygame.Surface((cell_size,cell_size))
            s.set_alpha(153)
            s.fill(color)
            screen.blit(s,(x,y))
        screen.blit(font.render(["Kick","Snare","Hi-Hat"][r],True,(255,255,255)),
                    (5, cam_height*scale_factor+r*(cell_size+margin)))

    pygame.draw.rect(screen,(80,80,80),tap_button_rect)
    screen.blit(font.render("tap",True,(255,255,255)),
                (tap_button_rect.x+15, tap_button_rect.y+2))

    if tap_mode:
        panel = pygame.Surface((tap_panel_rect.width, tap_panel_rect.height))
        panel.fill((40,40,40))
        screen.blit(panel,tap_panel_rect.topleft)
        pygame.draw.rect(screen,(120,120,120),tap_panel_rect,2)
        screen.blit(font.render("tap to set bpm",True,(255,255,255)),
                    (tap_panel_rect.centerx-60, tap_panel_rect.y+80))
        pygame.draw.rect(screen,(150,50,50),tap_close_rect)
        screen.blit(font.render("x",True,(255,255,255)),
                    (tap_close_rect.x+5,tap_close_rect.y))

    pygame.display.flip()

# ===== RUN =====
stream = sd.OutputStream(samplerate=sample_rate,blocksize=buffer_size,channels=1,callback=audio_callback)
stream.start()

while running:
    draw()
    for event in pygame.event.get():
        if event.type==pygame.QUIT:
            running=False

        elif event.type==pygame.MOUSEBUTTONDOWN:
            mx,my=event.pos

            if tap_mode:
                if tap_close_rect.collidepoint(event.pos):
                    tap_mode=False
                    tap_times=[]
                elif tap_panel_rect.collidepoint(event.pos):
                    register_tap()
                continue

            if tap_button_rect.collidepoint(event.pos):
                tap_mode=True
                tap_times=[]
                continue

            c = mx//(cell_size+margin)
            r = (my-cam_height*scale_factor)//(cell_size+margin)
            if 0<=r<rows and 0<=c<steps and not tap_mode:
                grid[r][c]=not grid[r][c]

        elif event.type==pygame.MOUSEBUTTONUP:
            bpm_dragging=False

stream.stop()
stream.close()
cap.release()
hands.close()
pygame.quit()
