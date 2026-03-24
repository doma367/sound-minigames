import numpy as np
import sounddevice as sd
import cv2
import mediapipe as mp
import pygame
import time

# ===== CONFIG =====
bpm = 120
steps, rows = 8, 3
sample_rate = 44100

grid = np.zeros((rows, steps), dtype=bool)
grid[0,0]=grid[0,4]=grid[1,2]=grid[1,6]=True 
visual_step = 0
hand_y = 0.5      
delay_time_val = 0.1 # Gap between echoes (seconds)
delay_feedback = 0.0 
delay_locked = False
is_fist = False   
step_flashes = np.zeros(steps)
tap_mode = False
tap_times = []

# ===== AUDIO ENGINE =====
class DrumMachine:
    def __init__(self):
        self.samples = [self._kick(), self._snare(), self._hat()]
        self.active_sounds = [] 
        self.samples_per_step = 0
        self.total_samples_processed = 0
        self.output_latency_samples = 0
        self.is_paused = False
        
        # 2-second buffer
        self.delay_buf_size = sample_rate * 2
        self.delay_buffer = np.zeros(self.delay_buf_size)
        self.delay_ptr = 0
        self.filter_state = 0.0
        self.update_timing()

    def update_timing(self):
        self.samples_per_step = int(sample_rate * (60 / bpm / 2))
        self.next_trigger = 0 

    def _kick(self):
        t = np.linspace(0, 0.1, int(sample_rate * 0.1), False)
        return (np.sin(2 * np.pi * 150 * (0.5**(t/0.03)) * t) * np.exp(-t*40)).astype(np.float32)

    def _snare(self):
        t = np.linspace(0, 0.1, int(sample_rate * 0.1), False)
        return (np.random.normal(0, 0.2, len(t)) * np.exp(-t*60)).astype(np.float32)

    def _hat(self):
        t = np.linspace(0, 0.03, int(sample_rate * 0.03), False)
        return (np.random.uniform(-0.1, 0.1, len(t)) * np.exp(-t*120)).astype(np.float32)

    def callback(self, outdata, frames, time_info, status):
        global visual_step
        outdata.fill(0)
        if self.is_paused: return
        
        self.output_latency_samples = int((time_info.outputBufferDacTime - time_info.currentTime) * sample_rate)
        
        # Filter smoothness
        clamped_y = np.clip((hand_y - 0.2) / 0.5, 0.0, 1.0)
        alpha = np.clip(np.power(1.0 - clamped_y, 2.0), 0.01, 0.95)

        for i in range(frames):
            if self.next_trigger <= 0:
                logic_step = (self.total_samples_processed // self.samples_per_step) % steps
                for r in range(rows):
                    if grid[r, int(logic_step)]:
                        self.active_sounds.append([self.samples[r], 0])
                self.next_trigger += self.samples_per_step
            
            raw_sample = 0
            for snd in self.active_sounds[:]:
                raw_sample += snd[0][snd[1]]
                snd[1] += 1
                if snd[1] >= len(snd[0]): self.active_sounds.remove(snd)
            
            # Effects chain
            self.filter_state = self.filter_state + alpha * (raw_sample - self.filter_state)
            dry = self.filter_state
            if is_fist: dry = np.round(dry * 5) / 5

            # DELAY CALCULATION
            # delay_time_val is the TIME in seconds between echoes
            delay_samples = int(np.clip(delay_time_val, 0.02, 0.8) * sample_rate)
            read_ptr = (self.delay_ptr - delay_samples) % self.delay_buf_size
            wet = self.delay_buffer[read_ptr]
            
            # Write back to buffer
            self.delay_buffer[self.delay_ptr] = dry + (wet * delay_feedback)
            self.delay_ptr = (self.delay_ptr + 1) % self.delay_buf_size
            
            # LOWER DELAY STRENGTH: Wet signal is mixed at 30% volume
            outdata[i, 0] = np.tanh(dry + (wet * 0.3 if delay_feedback > 0.05 else 0)) 
            
            self.next_trigger -= 1
            self.total_samples_processed += 1

    def get_synced_step(self):
        synced_samples = max(0, self.total_samples_processed - self.output_latency_samples)
        return (synced_samples // self.samples_per_step) % steps

engine = DrumMachine()

# ===== UI & VISION =====
pygame.init()
screen = pygame.display.set_mode((1000, 900), pygame.RESIZABLE)
cap = cv2.VideoCapture(0)
mp_hands = mp.solutions.hands
hands = mp_hands.Hands(max_num_hands=2, min_detection_confidence=0.7)
font = pygame.font.SysFont("Arial", 18, bold=True)

with sd.OutputStream(samplerate=sample_rate, channels=1, callback=engine.callback):
    while True:
        W, H = screen.get_size()
        ret, frame = cap.read()
        if not ret: break
        frame = cv2.flip(frame, 1)
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = hands.process(rgb_frame)
        visual_step = int(engine.get_synced_step())
        
        if results.multi_hand_landmarks and not tap_mode:
            for i, hand_lms in enumerate(results.multi_hand_landmarks):
                label = results.multi_handedness[i].classification[0].label
                lms = hand_lms.landmark
                
                # Pinch Detection (Thumb 4 to Index 8)
                dist = np.sqrt((lms[4].x - lms[8].x)**2 + (lms[4].y - lms[8].y)**2)
                pinched = dist < 0.04

                if label == "Right":
                    hand_y = lms[8].y 
                    is_fist = pinched
                else:
                    delay_locked = pinched
                    # Feedback (Vertical Position)
                    delay_feedback = np.clip(1.1 - (lms[0].y * 1.3), 0.0, 0.85)
                    
                    if not delay_locked:
                        # NEW ROTATION LOGIC: Vertical offset between Index base (5) and Pinky base (17)
                        # When hand is flat, dy is near 0. When tilted, dy increases.
                        dy = abs(lms[5].y - lms[17].y)
                        delay_time_val = np.clip(dy * 4.0, 0.05, 0.8)

        # --- DRAWING ---
        screen.fill((10, 10, 15))
        cam_w_disp = int(W * 0.7); cam_h_disp = int(cam_w_disp * 0.75)
        if cam_h_disp > H * 0.5: cam_h_disp = int(H * 0.5); cam_w_disp = int(cam_h_disp * 1.33)
        
        f_surf = pygame.surfarray.make_surface(np.rot90(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)))
        screen.blit(pygame.transform.scale(f_surf, (cam_w_disp, cam_h_disp)), (W//2 - cam_w_disp//2, 20))
        
        # Gauges with Labels
        gh = cam_h_disp
        # Filter Gauge
        pygame.draw.rect(screen, (30, 30, 40), (20, 20, 40, gh), border_radius=5)
        fh = int(np.clip(1.0 - ((hand_y-0.2)/0.5), 0, 1) * gh)
        pygame.draw.rect(screen, (0, 255, 255), (20, 20+gh-fh, 40, fh), border_radius=5)
        screen.blit(font.render("FILTER", True, (0, 255, 255)), (15, 30+gh))

        # Time Gauge (Rotation)
        pygame.draw.rect(screen, (30, 30, 40), (W-110, 20, 40, gh), border_radius=5)
        th = int((delay_time_val / 0.8) * gh)
        t_col = (255, 255, 0) if delay_locked else (255, 180, 0)
        pygame.draw.rect(screen, t_col, (W-110, 20+gh-th, 40, th), border_radius=5)
        screen.blit(font.render("TIME", True, t_col), (W-110, 30+gh))

        # Feedback Gauge (Height)
        pygame.draw.rect(screen, (30, 30, 40), (W-60, 20, 40, gh), border_radius=5)
        fbh = int((delay_feedback / 0.85) * gh)
        pygame.draw.rect(screen, (255, 50, 50), (W-60, 20+gh-fbh, 40, fbh), border_radius=5)
        screen.blit(font.render("FEEDBK", True, (255, 50, 50)), (W-65, 30+gh))

        # Sequencer
        pan_y = cam_h_disp + 100
        cw = int(W * 0.85) // steps
        sx = W//2 - (cw * steps)//2
        for r in range(rows):
            for c in range(steps):
                rect = pygame.Rect(sx + c*cw + 4, pan_y + r*50, cw - 8, 45)
                base = np.array([0, 255, 120]) if grid[r,c] else np.array([40, 40, 45])
                if c == visual_step:
                    pygame.draw.rect(screen, (255, 255, 255), rect.inflate(4, 4), 2)
                    step_flashes[c] = 1.0
                pygame.draw.rect(screen, tuple(np.clip(base + (step_flashes[c]*100), 0, 255)), rect, border_radius=4)
        step_flashes *= 0.8
        
        # Tap BPM Button (Separate Modal handled by tap_mode)
        tap_btn = pygame.Rect(W//2 - 100, H - 80, 200, 50)
        pygame.draw.rect(screen, (200, 40, 40), tap_btn, border_radius=10)
        screen.blit(font.render("SET BPM", True, (255, 255, 255)), (tap_btn.centerx-35, tap_btn.y+15))

        if tap_mode:
            modal = pygame.Surface((W, H), pygame.SRCALPHA); modal.fill((0, 0, 0, 240)); screen.blit(modal, (0, 0))
            box = pygame.Rect(W//2-200, H//2-100, 400, 200)
            pygame.draw.rect(screen, (50, 50, 60), box, border_radius=15)
            txt = font.render(f"TAP HERE ({bpm} BPM)", True, (255, 255, 255))
            screen.blit(txt, (box.centerx-txt.get_width()//2, box.y+50))
            ebtn = pygame.Rect(box.centerx-50, box.bottom-50, 100, 35)
            pygame.draw.rect(screen, (0, 200, 80), ebtn, border_radius=5)
            screen.blit(font.render("DONE", True, (255, 255, 255)), (ebtn.x+30, ebtn.y+8))

        for event in pygame.event.get():
            if event.type == pygame.QUIT: pygame.quit(); cap.release(); exit()
            if event.type == pygame.VIDEORESIZE: screen = pygame.display.set_mode((event.w, event.h), pygame.RESIZABLE)
            if event.type == pygame.MOUSEBUTTONDOWN:
                mx, my = event.pos
                if tap_mode:
                    if ebtn.collidepoint(mx, my): tap_mode = False; engine.is_paused = False
                    elif box.collidepoint(mx, my): 
                        now = time.time(); tap_times.append(now)
                        if len(tap_times) > 5: tap_times.pop(0)
                        if len(tap_times) >= 2:
                            bpm = int(60 / ((tap_times[-1] - tap_times[0]) / (len(tap_times)-1)))
                            bpm = max(40, min(240, bpm)); engine.update_timing()
                else:
                    if tap_btn.collidepoint(mx, my): tap_mode = True; engine.is_paused = True; tap_times=[]
                    elif pan_y <= my <= pan_y+150 and sx <= mx <= sx+(cw*steps):
                        c, r = (mx - sx) // cw, (my - pan_y) // 50
                        if 0 <= r < rows and 0 <= c < steps: grid[r, c] = not grid[r, c]

        pygame.display.flip()