import pygame
import numpy as np
import sounddevice as sd

# ===== CONFIG =====
bpm = 120
steps = 8
rows = 3
sample_rate = 44100
buffer_size = 256
cell_size = 50
margin = 5

seconds_per_step = 60 / bpm / 2
samples_per_step = int(sample_rate * seconds_per_step)

# ===== GRID =====
grid = [
    [True, False, False, False, True, False, False, False],
    [False, False, True, False, False, False, True, False],
    [False, True, False, True, False, True, False, True],
]

current_step = 0
active_sounds = []
fx_params = [1.0, 1.0, 1.0]

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

# ===== AUDIO CALLBACK =====
step_counter = samples_per_step

def audio_callback(outdata, frames, time_info, status):
    global step_counter, current_step
    outdata.fill(0)
    step_counter -= frames
    if step_counter <= 0:
        step_counter += samples_per_step
        # trigger sounds for this step
        for r in range(rows):
            if grid[r][current_step]:
                active_sounds.append(samples[r].copy())
        current_step = (current_step+1)%steps

    finished = []
    for i,sound in enumerate(active_sounds):
        chunk = sound[:frames]
        outdata[:len(chunk),0] += chunk*fx_params[i%rows]
        active_sounds[i] = sound[len(chunk):]
        if len(active_sounds[i])==0:
            finished.append(i)
    for i in reversed(finished):
        active_sounds.pop(i)

# ===== PYGAME SETUP =====
pygame.init()
width = steps*(cell_size+margin)+margin
height = rows*(cell_size+margin)+margin
screen = pygame.display.set_mode((width,height))
pygame.display.set_caption("Drum Grid")

font = pygame.font.SysFont(None,24)
labels = ["Kick","Snare","Hi-Hat"]

# ===== DRAW GRID =====
def draw_grid():
    screen.fill((30,30,30))
    highlight_step = (current_step-1)%steps  # fix the yellow line
    for r in range(rows):
        for c in range(steps):
            x = margin + c*(cell_size+margin)
            y = margin + r*(cell_size+margin)
            color = (0,200,0) if grid[r][c] else (70,70,70)
            if c==highlight_step:
                color = (200,200,0) if grid[r][c] else (100,100,0)
            pygame.draw.rect(screen,color,(x,y,cell_size,cell_size))
            label = font.render(labels[r],True,(255,255,255))
            screen.blit(label,(5,r*(cell_size+margin)))
    pygame.display.flip()


# ===== MAIN LOOP =====
running = True
stream = sd.OutputStream(samplerate=sample_rate,blocksize=buffer_size,channels=1,callback=audio_callback)
stream.start()

while running:
    draw_grid()
    for event in pygame.event.get():
        if event.type==pygame.QUIT:
            running=False
        elif event.type==pygame.MOUSEBUTTONDOWN:
            mx,my = event.pos
            c = mx//(cell_size+margin)
            r = my//(cell_size+margin)
            if 0<=r<rows and 0<=c<steps:
                grid[r][c]=not grid[r][c]

stream.stop()
stream.close()
pygame.quit()
