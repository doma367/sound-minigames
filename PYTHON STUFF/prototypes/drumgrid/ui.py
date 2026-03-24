"""
Layout
------
┌─────────────────────────────────────────────────────────────┐
│  CAMERA FEED  (centred, ~45% of height)                     │
├──────────────────────────────────────────────────────────────┤
│  TOOLBAR: [gauges] [BPM] [8|16|32] [REVERB sliders] [BPM btn]│
├──────────────────────────────────────────────────────────────┤
│  SEQUENCER ROWS  (label | vol | steps …)                    │
│  …                                                           │
│  [+ Add voice]                                               │
├─────────────────────────────────────┬────────────────────────┤
│  (padding)                          │  WAVEFORM PANEL        │
└─────────────────────────────────────┴────────────────────────┘

All sections are computed top-down; nothing is pinned to the
bottom of the window so nothing can overlap.
"""

import pygame
import numpy as np
from vision import GestureState

# ── Palette ─────────────────────────────────────────────────────────────
C_BG        = (11,  11,  17)
C_PANEL     = (17,  17,  25)
C_ROW_A     = (20,  20,  30)
C_ROW_B     = (15,  15,  22)
C_TRACK_BG  = (30,  30,  42)
C_ACTIVE    = (0,   220, 105)
C_INACTIVE  = (36,  36,  50)
C_PLAYHEAD  = (255, 255, 255)
C_FILTER    = (0,   215, 255)
C_TIME      = (255, 170,   0)
C_TIME_LOCK = (255, 255,   0)
C_FEEDBACK  = (255,  50,  50)
C_BTN       = (175,  32,  32)
C_BTN_HOV   = (210,  50,  50)
C_BTN_TEXT  = (255, 255, 255)
C_MODAL_BTN = (0,   185,  65)
C_REVERB    = (145,  85, 255)
C_WAVEFORM  = (0,   200, 255)
C_VOL       = (255, 150,  30)
C_STEPS_OFF = (45,  45,  65)
C_STEPS_ON  = (80,  80, 155)
C_ADD       = (22,  125,  52)
C_DEL       = (160,  28,  28)
C_DEL_HOV   = (210,  50,  50)
C_BORDER    = (38,  38,  55)
C_LABEL     = (100, 100, 122)
C_TOOLBAR   = (14,  14,  20)

FONT        = "Arial"
ROW_H       = 54           # sequencer row height
TOOLBAR_H   = 52           # height of the toolbar strip
STEP_OPTIONS = [8, 16, 32]
GAUGE_W     = 22
GAUGE_GAP   = 8


# ── Slider ──────────────────────────────────────────────────────────────
class Slider:
    def __init__(self, x, y, w, h, label, colour,
                 min_val=0.0, max_val=1.0, initial=0.0):
        self.rect   = pygame.Rect(x, y, w, h)
        self.label  = label
        self.colour = colour
        self.min, self.max = min_val, max_val
        self.value  = float(initial)
        self._drag  = False

    def handle_event(self, event):
        if (event.type == pygame.MOUSEBUTTONDOWN
                and event.button == 1
                and self.rect.collidepoint(event.pos)):
            self._drag = True
            self._set(event.pos[1])
        elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
            self._drag = False
        elif event.type == pygame.MOUSEMOTION and self._drag:
            self._set(event.pos[1])

    def _set(self, mouse_y):
        r = 1.0 - (mouse_y - self.rect.top) / max(1, self.rect.height)
        self.value = float(np.clip(
            self.min + r * (self.max - self.min), self.min, self.max
        ))

    def draw(self, screen, font):
        pygame.draw.rect(screen, C_TRACK_BG, self.rect, border_radius=4)
        ratio = (self.value - self.min) / max(1e-9, self.max - self.min)
        fh = int(ratio * self.rect.height)
        if fh > 0:
            fr = pygame.Rect(self.rect.x, self.rect.bottom - fh,
                             self.rect.width, fh)
            pygame.draw.rect(screen, self.colour, fr, border_radius=4)
        hy = self.rect.bottom - fh
        pygame.draw.line(screen, (190, 190, 190),
                         (self.rect.x, hy), (self.rect.right, hy), 2)
        if self.label:
            lbl = font.render(self.label, True, self.colour)
            screen.blit(lbl, (self.rect.centerx - lbl.get_width() // 2,
                               self.rect.bottom + 4))
        val = font.render(f"{self.value:.2f}", True, (120, 120, 140))
        screen.blit(val, (self.rect.centerx - val.get_width() // 2,
                           self.rect.bottom + 17))


# ── UI ───────────────────────────────────────────────────────────────────
class UI:
    def __init__(self, steps: int, rows: int):
        self.steps        = steps
        self.rows         = rows
        self.step_flashes = np.zeros(steps)

        self.font    = pygame.font.SysFont(FONT, 15, bold=True)
        self.font_sm = pygame.font.SysFont(FONT, 13)
        self.font_xs = pygame.font.SysFont(FONT, 11)

        # Reverb sliders
        self.slider_size = Slider(0, 0, 28, 36, "SIZE", C_REVERB, 0.5, 3.0, 1.0)
        self.slider_wet  = Slider(0, 0, 28, 36, "WET",  C_REVERB, 0.0, 1.0, 0.0)
        self.sliders     = [self.slider_size, self.slider_wet]

        # Per-row volume sliders
        self.vol_sliders: list[Slider] = []
        self._rebuild_vol_sliders(rows)

        # Hit-test rects (populated in draw())
        self._step_btn_rects: list[pygame.Rect]             = []
        self._add_btn_rect:   pygame.Rect | None            = None
        self._del_btn_rects:  list[tuple[int, pygame.Rect]] = []
        self._seq_top = 0
        self._seq_sx  = 0
        self._seq_cw  = 0
        self._tap_btn: pygame.Rect | None = None

        # Delete-confirm state
        self._confirm_row:  int | None = None   # row awaiting confirmation
        self._confirm_rect: pygame.Rect | None = None

    # ── Volume slider helpers ────────────────────────────────────────────
    def _rebuild_vol_sliders(self, n: int):
        while len(self.vol_sliders) < n:
            self.vol_sliders.append(
                Slider(0, 0, 16, ROW_H - 14, "", C_VOL, 0.0, 1.0, 0.8)
            )
        while len(self.vol_sliders) > n:
            self.vol_sliders.pop()

    def get_row_volumes(self) -> list[float]:
        return [s.value for s in self.vol_sliders]

    # ── Main draw ────────────────────────────────────────────────────────
    def draw(
        self,
        screen:       pygame.Surface,
        cam_rgb:      np.ndarray,
        grid:         np.ndarray,
        visual_step:  int,
        gesture:      GestureState,
        bpm:          int,
        tap_mode:     bool,
        active_steps: int,
        waveform:     np.ndarray,
        hit_envs:     list,
        voice_names:  list[str],
    ) -> tuple[pygame.Rect, pygame.Rect | None]:

        W, H = screen.get_size()
        screen.fill(C_BG)

        n_rows = grid.shape[0]
        self._rebuild_vol_sliders(n_rows)

        PAD = 14   # global breathing room

        # ── 1. Camera strip ──────────────────────────────────────────────
        # Gauges flank the camera on both sides — same height as the feed.
        # Left:  FILTER gauge
        # Right: TIME + FB gauges
        GAUGE_W_SIDE = 28   # wider than toolbar gauges — they have space now
        GAUGE_GAP_SIDE = 6
        SIDE_RESERVED = GAUGE_W_SIDE * 2 + GAUGE_GAP_SIDE + PAD * 2 + 8

        cam_h = min(int(H * 0.36), 280)
        cam_w = int(cam_h * (4 / 3))
        if cam_w > W - SIDE_RESERVED:
            cam_w = W - SIDE_RESERVED
            cam_h = int(cam_w * 0.75)
        cam_x = (W - cam_w) // 2
        cam_y = PAD

        # Left gauge: FILTER
        lg_x = cam_x - GAUGE_W_SIDE - 8
        self._draw_side_gauge(screen, lg_x, cam_y, GAUGE_W_SIDE, cam_h,
                              gesture.hand_y, C_FILTER, "FILTER",
                              lambda v: np.clip(1.0 - (v - 0.2) / 0.5, 0, 1))

        # Right gauges: TIME and FB
        rg1_x = cam_x + cam_w + 8
        rg2_x = rg1_x + GAUGE_W_SIDE + GAUGE_GAP_SIDE
        tc = C_TIME_LOCK if gesture.delay_locked else C_TIME
        self._draw_side_gauge(screen, rg1_x, cam_y, GAUGE_W_SIDE, cam_h,
                              gesture.delay_time_val, tc, "TIME",
                              lambda v: v / 0.8)
        self._draw_side_gauge(screen, rg2_x, cam_y, GAUGE_W_SIDE, cam_h,
                              gesture.delay_feedback, C_FEEDBACK, "FB",
                              lambda v: v / 0.85)

        # Camera frame
        mirrored = np.ascontiguousarray(cam_rgb[:, ::-1, :])
        surf = pygame.surfarray.make_surface(np.rot90(mirrored))
        screen.blit(pygame.transform.scale(surf, (cam_w, cam_h)), (cam_x, cam_y))

        # Skeleton overlay — drawn on top of camera
        self._draw_skeleton(screen, cam_x, cam_y, cam_w, cam_h, gesture)

        # Text overlays
        self._draw_cam_overlays(screen, cam_x, cam_y, cam_w, cam_h, gesture)

        cam_bottom = cam_y + cam_h

        # ── 2. Toolbar strip ─────────────────────────────────────────────
        tb_y = cam_bottom + PAD // 2
        pygame.draw.rect(screen, C_TOOLBAR,
                         (0, tb_y, W, TOOLBAR_H), border_radius=0)
        # subtle top/bottom border lines
        pygame.draw.line(screen, C_BORDER, (0, tb_y), (W, tb_y))
        pygame.draw.line(screen, C_BORDER,
                         (0, tb_y + TOOLBAR_H - 1), (W, tb_y + TOOLBAR_H - 1))

        cursor_x = PAD
        tb_mid   = tb_y + TOOLBAR_H // 2

        # BPM label
        bpm_s = self.font.render(f"{bpm} BPM", True, (180, 180, 205))
        screen.blit(bpm_s, (cursor_x, tb_mid - bpm_s.get_height() // 2))
        cursor_x += bpm_s.get_width() + 10

        # Step-count buttons
        self._step_btn_rects = []
        sbw, sbh = 38, 24
        for opt in STEP_OPTIONS:
            r = pygame.Rect(cursor_x, tb_mid - sbh // 2, sbw, sbh)
            col = C_STEPS_ON if opt == active_steps else C_STEPS_OFF
            pygame.draw.rect(screen, col, r, border_radius=4)
            ls = self.font_sm.render(str(opt), True, C_BTN_TEXT)
            screen.blit(ls, (r.centerx - ls.get_width() // 2,
                              r.centery - ls.get_height() // 2))
            self._step_btn_rects.append(r)
            cursor_x += sbw + 5

        cursor_x += 16  # gap before reverb

        # REVERB label + sliders (horizontal in toolbar)
        rev_lbl = self.font_xs.render("REVERB", True, C_REVERB)
        screen.blit(rev_lbl, (cursor_x, tb_y + 4))
        sl_y = tb_y + 7
        sl_h = H
        self.slider_size.rect = pygame.Rect(cursor_x,              sl_y, 28, sl_h)
        self.slider_wet.rect  = pygame.Rect(cursor_x + 28 + 6,     sl_y, 28, sl_h)
        # Labels below sliders
        for sl, name in [(self.slider_size, "SZ"), (self.slider_wet, "WET")]:
            sl.draw(screen, self.font_xs)
        cursor_x += 28 + 6 + 28 + 20

        # SET BPM button (right side of toolbar)
        tap_btn = pygame.Rect(W - PAD - 110, tb_mid - 16, 110, 32)
        pygame.draw.rect(screen, C_BTN, tap_btn, border_radius=7)
        tb_s = self.font.render("SET BPM", True, C_BTN_TEXT)
        screen.blit(tb_s, (tap_btn.centerx - tb_s.get_width() // 2,
                            tap_btn.centery - tb_s.get_height() // 2))
        self._tap_btn = tap_btn

        toolbar_bottom = tb_y + TOOLBAR_H

        # ── 3. Sequencer ─────────────────────────────────────────────────
        seq_y = toolbar_bottom + PAD

        # Column geometry
        #   [PAD] [DEL btn] [GAP] [name label] [GAP] [vol slider] [GAP] [cells…]
        DEL_W   = 30
        NAME_W  = 70
        VOL_W   = 18
        LEFT_W  = PAD + DEL_W + 6 + NAME_W + 6 + VOL_W + 8

        # Waveform panel will sit bottom-right; reserve its width
        WAVE_W  = max(180, int(W * 0.26))
        WAVE_H  = max(120, n_rows * ROW_H // 2)
        grid_w  = W - LEFT_W - PAD - WAVE_W - PAD
        cw      = max(8, grid_w // active_steps)

        self._seq_top = seq_y
        self._seq_sx  = LEFT_W
        self._seq_cw  = cw
        self._del_btn_rects = []

        for r in range(n_rows):
            ry = seq_y + r * ROW_H

            # Row stripe
            stripe = C_ROW_A if r % 2 == 0 else C_ROW_B
            pygame.draw.rect(screen, stripe,
                             (PAD, ry, W - PAD * 2, ROW_H - 3), border_radius=4)

            # ── Delete button ──────────────────────────────────────────
            del_r = pygame.Rect(PAD, ry + (ROW_H - 26) // 2, DEL_W, 26)
            pygame.draw.rect(screen, C_DEL, del_r, border_radius=5)
            del_lbl = self.font_xs.render("DEL", True, (255, 255, 255))
            screen.blit(del_lbl, (del_r.centerx - del_lbl.get_width() // 2,
                                   del_r.centery - del_lbl.get_height() // 2))
            self._del_btn_rects.append((r, del_r))

            # ── Voice name ────────────────────────────────────────────
            name = (voice_names[r] if r < len(voice_names) else f"R{r}")[:10]
            name_s = self.font_sm.render(name, True, C_LABEL)
            name_x = PAD + DEL_W + 6
            screen.blit(name_s, (name_x, ry + (ROW_H - name_s.get_height()) // 2))

            # ── Volume slider ─────────────────────────────────────────
            vs = self.vol_sliders[r]
            vol_x = PAD + DEL_W + 6 + NAME_W + 6
            vs.rect = pygame.Rect(vol_x, ry + 7, VOL_W, ROW_H - 14)
            vs.draw(screen, self.font_xs)

            # ── Step cells ────────────────────────────────────────────
            for c in range(active_steps):
                cx = LEFT_W + c * cw
                cell = pygame.Rect(cx + 2, ry + 5, cw - 4, ROW_H - 12)
                is_on = bool(grid[r, c]) if c < grid.shape[1] else False
                base  = np.array(C_ACTIVE if is_on else C_INACTIVE, dtype=float)
                flash = self.step_flashes[c] * 70 if c < len(self.step_flashes) else 0
                col   = tuple(np.clip(base + flash, 0, 255).astype(int))

                if c == visual_step:
                    pygame.draw.rect(screen, C_PLAYHEAD, cell.inflate(2, 2), 2)
                    if c < len(self.step_flashes):
                        self.step_flashes[c] = 1.0

                pygame.draw.rect(screen, col, cell, border_radius=3)

                # Beat divider every 4 steps
                if c > 0 and c % 4 == 0:
                    pygame.draw.line(screen, (55, 55, 72),
                                     (cx, ry + 4), (cx, ry + ROW_H - 6))

        self.step_flashes *= 0.8

        seq_bottom = seq_y + n_rows * ROW_H

        # ── Add voice button ──────────────────────────────────────────────
        add_r = pygame.Rect(LEFT_W, seq_bottom + 10, 120, 30)
        pygame.draw.rect(screen, C_ADD, add_r, border_radius=6)
        add_s = self.font_sm.render("+ Add voice", True, C_BTN_TEXT)
        screen.blit(add_s, (add_r.centerx - add_s.get_width() // 2,
                             add_r.centery - add_s.get_height() // 2))
        self._add_btn_rect = add_r

        # ── 4. Waveform panel (bottom-right) ─────────────────────────────
        wave_x = W - WAVE_W - PAD
        wave_y = toolbar_bottom + PAD
        self._draw_waveform_panel(screen, wave_x, wave_y, WAVE_W,
                                  n_rows * ROW_H + 10 + 30 + PAD,
                                  waveform, hit_envs)

        # ── 5. Confirm-delete overlay ─────────────────────────────────────
        confirm_yes = None
        confirm_no  = None
        if self._confirm_row is not None:
            confirm_yes, confirm_no = self._draw_confirm(screen, W, H,
                                                          self._confirm_row,
                                                          voice_names)

        # ── 6. Tap-tempo modal ────────────────────────────────────────────
        ebtn = None
        if tap_mode:
            ebtn = self._draw_tap_modal(screen, W, H, bpm)

        return tap_btn, ebtn, confirm_yes, confirm_no

    # ── Waveform panel ───────────────────────────────────────────────────
    def _draw_waveform_panel(self, screen, x, y, w, h, waveform, hit_envs):
        pygame.draw.rect(screen, C_PANEL, (x, y, w, h), border_radius=8)
        pygame.draw.rect(screen, C_BORDER, (x, y, w, h), border_radius=8, width=1)

        pad  = 10
        lbl  = self.font_xs.render("LIVE WAVEFORM", True, (70, 90, 105))
        screen.blit(lbl, (x + pad, y + pad - 2))

        inner_y = y + pad + 14
        inner_h = h - pad - 14 - pad
        inner_w = w - pad * 2
        mid     = inner_y + inner_h // 2

        # Zero line
        pygame.draw.line(screen, (35, 40, 55),
                         (x + pad, mid), (x + pad + inner_w, mid), 1)

        # Waveform line — thicker, brighter
        if len(waveform) > 1:
            n = inner_w
            idxs = np.linspace(0, len(waveform) - 1, n).astype(int)
            vals = np.clip(waveform[idxs], -1.0, 1.0)
            pts  = [
                (x + pad + i, int(mid - vals[i] * (inner_h // 2 - 3)))
                for i in range(n)
            ]
            if len(pts) > 1:
                pygame.draw.lines(screen, C_WAVEFORM, False, pts, 2)

        # Hit envelope flashes (green, fade out)
        for sample_data, pos in hit_envs:
            if pos >= len(sample_data) or len(sample_data) < 2:
                continue
            fade = max(0.0, 1.0 - pos / len(sample_data))
            col  = tuple(int(c * fade) for c in (0, 255, 110))
            if sum(col) < 8:
                continue
            env_n = min(inner_w, 160)
            idxs  = np.linspace(0, len(sample_data) - 1, env_n).astype(int)
            vals  = np.clip(sample_data[idxs], -1.0, 1.0)
            pts   = [
                (x + pad + i, int(mid - vals[i] * (inner_h // 2 - 3)))
                for i in range(env_n)
            ]
            if len(pts) > 1:
                pygame.draw.lines(screen, col, False, pts, 1)

    # ── Delete confirmation overlay ──────────────────────────────────────
    def _draw_confirm(self, screen, W, H, row, voice_names):
        """Draw a centred confirmation modal. Returns (yes_rect, no_rect)."""
        ov = pygame.Surface((W, H), pygame.SRCALPHA)
        ov.fill((0, 0, 0, 180))
        screen.blit(ov, (0, 0))

        bw, bh = 340, 160
        box = pygame.Rect(W // 2 - bw // 2, H // 2 - bh // 2, bw, bh)
        pygame.draw.rect(screen, (28, 28, 40), box, border_radius=12)
        pygame.draw.rect(screen, C_BORDER, box, border_radius=12, width=1)

        name = (voice_names[row] if row < len(voice_names) else f"Row {row}")
        title = self.font.render(f'Remove  "{name}" ?', True, (230, 230, 230))
        screen.blit(title, (box.centerx - title.get_width() // 2, box.y + 30))

        sub = self.font_xs.render("This cannot be undone.", True, (120, 120, 140))
        screen.blit(sub, (box.centerx - sub.get_width() // 2, box.y + 58))

        yes_r = pygame.Rect(box.centerx - 90, box.bottom - 50, 80, 30)
        no_r  = pygame.Rect(box.centerx + 10, box.bottom - 50, 80, 30)

        pygame.draw.rect(screen, C_DEL,       yes_r, border_radius=6)
        pygame.draw.rect(screen, C_STEPS_OFF, no_r,  border_radius=6)

        yes_s = self.font_sm.render("Remove", True, (255, 255, 255))
        no_s  = self.font_sm.render("Cancel", True, (200, 200, 200))
        screen.blit(yes_s, (yes_r.centerx - yes_s.get_width() // 2,
                             yes_r.centery - yes_s.get_height() // 2))
        screen.blit(no_s,  (no_r.centerx  - no_s.get_width()  // 2,
                             no_r.centery  - no_s.get_height() // 2))

        return yes_r, no_r

    # ── Side gauge (tall, flanks camera) ────────────────────────────────
    def _draw_side_gauge(self, screen, x, y, w, h, value, colour, label, norm_fn):
        pygame.draw.rect(screen, C_TRACK_BG, (x, y, w, h), border_radius=5)
        fh = int(norm_fn(value) * h)
        if fh > 0:
            # Gradient effect: brighter at top of fill
            pygame.draw.rect(screen, colour, (x, y + h - fh, w, fh), border_radius=5)
            # Bright tip line
            tip_y = y + h - fh
            pygame.draw.line(screen, (255, 255, 255), (x, tip_y), (x + w, tip_y), 2)
        lbl_s = self.font_xs.render(label, True, colour)
        screen.blit(lbl_s, (x + w // 2 - lbl_s.get_width() // 2, y + h + 4))

    # ── Hand skeleton overlay ────────────────────────────────────────────
    def _draw_skeleton(self, screen, cam_x, cam_y, cam_w, cam_h, gesture):
        """
        Draw MediaPipe hand landmarks and connections on top of the camera.
        Right hand → cyan connections, left hand → orange.
        Landmark dots are small white circles.
        The skeleton is mirrored to match the display (x = 1 - x).
        """
        from vision import HAND_CONNECTIONS

        for hand in gesture.hand_landmarks:
            label = hand["label"]
            pts   = hand["pts"]

            # Colour coding by hand role
            line_col = C_FILTER if label == "Right" else C_TIME
            dot_col  = (220, 220, 220)

            # Convert normalised coords → pixel coords, mirrored on x
            def lm_px(i):
                x_n, y_n = pts[i]
                px = cam_x + int((1.0 - x_n) * cam_w)   # mirror x
                py = cam_y + int(y_n * cam_h)
                return px, py

            # Connections
            for a, b in HAND_CONNECTIONS:
                ax, ay = lm_px(a)
                bx, by = lm_px(b)
                pygame.draw.line(screen, line_col, (ax, ay), (bx, by), 2)

            # Landmark dots
            for i in range(21):
                px, py = lm_px(i)
                # Fingertips slightly larger
                r = 5 if i in (4, 8, 12, 16, 20) else 3
                pygame.draw.circle(screen, dot_col, (px, py), r)
                pygame.draw.circle(screen, line_col, (px, py), r, 1)

    # ── Camera text overlays ─────────────────────────────────────────────
    def _draw_cam_overlays(self, screen, cx, cy, cw, ch, gesture):
        hints = [
            (cx + 6, cy + 6,       C_FILTER, "R hand  height=filter  pinch=crush"),
            (cx + 6, cy + ch - 20, C_TIME,   "L hand  tilt=echo time  height=feedback  ✌=lock delay"),
        ]
        for lx, ly, col, txt in hints:
            s  = self.font_xs.render(txt, True, col)
            bg = pygame.Surface((s.get_width() + 8, s.get_height() + 5), pygame.SRCALPHA)
            bg.fill((0, 0, 0, 150))
            screen.blit(bg, (lx - 4, ly - 2))
            screen.blit(s,  (lx, ly))

        if gesture.is_fist:
            s = self.font_xs.render("● CRUSH ON", True, (255, 210, 0))
            screen.blit(s, (cx + cw - 82, cy + 6))
        if gesture.delay_locked:
            s = self.font_xs.render("DELAY LOCKED", True, C_TIME_LOCK)
            screen.blit(s, (cx + cw - 98, cy + ch - 20))

    # ── Hit-test helpers ─────────────────────────────────────────────────
    def hit_step(self, mx, my, active_steps, n_rows):
        sx, cw, st = self._seq_sx, self._seq_cw, self._seq_top
        if cw <= 0:
            return None
        c = (mx - sx) // cw
        r = (my - st) // ROW_H
        if 0 <= r < n_rows and 0 <= c < active_steps:
            cell = pygame.Rect(sx + c * cw + 2, st + r * ROW_H + 5, cw - 4, ROW_H - 12)
            if cell.collidepoint(mx, my):
                return int(r), int(c)
        return None

    def hit_step_btn(self, mx, my):
        for rect, opt in zip(self._step_btn_rects, STEP_OPTIONS):
            if rect.collidepoint(mx, my):
                return opt
        return None

    def hit_add_btn(self, mx, my):
        return self._add_btn_rect is not None and self._add_btn_rect.collidepoint(mx, my)

    def hit_del_btn(self, mx, my):
        for row, rect in self._del_btn_rects:
            if rect.collidepoint(mx, my):
                return row
        return None

    # ── Tap-tempo modal ──────────────────────────────────────────────────
    def _draw_tap_modal(self, screen, W, H, bpm):
        ov = pygame.Surface((W, H), pygame.SRCALPHA)
        ov.fill((0, 0, 0, 210))
        screen.blit(ov, (0, 0))
        box = pygame.Rect(W // 2 - 190, H // 2 - 95, 380, 190)
        pygame.draw.rect(screen, (30, 30, 44), box, border_radius=12)
        pygame.draw.rect(screen, C_BORDER, box, border_radius=12, width=1)
        t = self.font.render(f"TAP HERE   {bpm} BPM", True, (225, 225, 225))
        screen.blit(t, (box.centerx - t.get_width() // 2, box.y + 44))
        h = self.font_sm.render("Click repeatedly to set tempo", True, (120, 120, 138))
        screen.blit(h, (box.centerx - h.get_width() // 2, box.y + 80))
        ebtn = pygame.Rect(box.centerx - 44, box.bottom - 46, 88, 30)
        pygame.draw.rect(screen, C_MODAL_BTN, ebtn, border_radius=5)
        s = self.font.render("DONE", True, C_BTN_TEXT)
        screen.blit(s, (ebtn.centerx - s.get_width() // 2,
                         ebtn.centery - s.get_height() // 2))
        return ebtn