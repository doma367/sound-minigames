import pygame
import numpy as np
from vision import GestureState

# ── Palette ────────────────────────────────────────────────────────────
C_BG        = (10,  10,  15)
C_PANEL     = (16,  16,  22)
C_TRACK_BG  = (28,  28,  38)
C_ACTIVE    = (0,   230, 110)
C_INACTIVE  = (38,  38,  48)
C_PLAYHEAD  = (255, 255, 255)
C_FILTER    = (0,   220, 255)
C_TIME      = (255, 175,  0)
C_TIME_LOCK = (255, 255,  0)
C_FEEDBACK  = (255,  50,  50)
C_BTN       = (180,  35,  35)
C_BTN_TEXT  = (255, 255, 255)
C_MODAL_BTN = (0,   190,  70)
C_REVERB    = (150,  90, 255)
C_WAVEFORM  = (0,   210, 255)
C_ENVELOPE  = (0,   255, 110)
C_HIST      = (80,  140, 255)
C_VOL       = (255, 155,  35)
C_STEPS_OFF = (50,  50,  68)
C_STEPS_ON  = (85,  85, 160)
C_ADD_BTN   = (25,  130,  55)
C_DEL_BTN   = (150,  28,  28)
C_BORDER    = (35,  35,  50)
C_LABEL     = (110, 110, 130)

FONT_NAME    = "Arial"
ROW_H        = 48
STEP_OPTIONS = [8, 16, 32]

# Layout split: sequencer uses left 62% of width, right panel gets the rest
SEQ_WIDTH_RATIO = 0.62


# ── Slider ─────────────────────────────────────────────────────────────
class Slider:
    def __init__(self, x, y, w, h, label, colour,
                 min_val=0.0, max_val=1.0, initial=0.0):
        self.rect   = pygame.Rect(x, y, w, h)
        self.label  = label
        self.colour = colour
        self.min    = min_val
        self.max    = max_val
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
        self.value = float(
            np.clip(self.min + r * (self.max - self.min), self.min, self.max)
        )

    def draw(self, screen, font, show_label=True, show_value=False):
        pygame.draw.rect(screen, C_TRACK_BG, self.rect, border_radius=4)
        ratio = (self.value - self.min) / max(1e-9, self.max - self.min)
        fh = int(ratio * self.rect.height)
        if fh > 0:
            fr = pygame.Rect(self.rect.x, self.rect.bottom - fh,
                             self.rect.width, fh)
            pygame.draw.rect(screen, self.colour, fr, border_radius=4)
        # handle line
        hy = self.rect.bottom - fh
        pygame.draw.line(screen, (200, 200, 200),
                         (self.rect.x, hy), (self.rect.right, hy), 2)
        if show_label and self.label:
            lbl = font.render(self.label, True, self.colour)
            screen.blit(lbl, (self.rect.x, self.rect.bottom + 4))
        if show_value:
            val = font.render(f"{self.value:.2f}", True, (140, 140, 160))
            screen.blit(val, (self.rect.x, self.rect.bottom + 18))


# ── UI ─────────────────────────────────────────────────────────────────
class UI:
    def __init__(self, steps: int, rows: int):
        self.steps        = steps
        self.rows         = rows
        self.step_flashes = np.zeros(steps)
        self.font    = pygame.font.SysFont(FONT_NAME, 16, bold=True)
        self.font_sm = pygame.font.SysFont(FONT_NAME, 12)
        self.font_xs = pygame.font.SysFont(FONT_NAME, 11)

        # Reverb sliders (positioned in right panel)
        self.slider_size = Slider(0, 0, 28, 90, "SIZE", C_REVERB, 0.5, 3.0, 1.0)
        self.slider_wet  = Slider(0, 0, 28, 90, "WET",  C_REVERB, 0.0, 1.0, 0.0)
        self.sliders = [self.slider_size, self.slider_wet]

        # Per-row volume sliders
        self.vol_sliders: list[Slider] = []
        self._rebuild_vol_sliders(rows)

        # Hit-test rects (set each frame in draw)
        self._step_btn_rects: list[pygame.Rect] = []
        self._add_btn_rect:   pygame.Rect | None = None
        self._del_btn_rects:  list[tuple[int, pygame.Rect | None]] = []
        self._seq_top = 0
        self._seq_sx  = 0
        self._seq_cw  = 0
        self._tap_btn: pygame.Rect | None = None

    # ── Volume slider management ───────────────────────────────────────
    def _rebuild_vol_sliders(self, n_rows: int):
        while len(self.vol_sliders) < n_rows:
            self.vol_sliders.append(
                Slider(0, 0, 14, ROW_H - 10, "", C_VOL, 0.0, 1.0, 0.8)
            )
        while len(self.vol_sliders) > n_rows:
            self.vol_sliders.pop()

    def get_row_volumes(self) -> list[float]:
        return [s.value for s in self.vol_sliders]

    # ── Main draw ──────────────────────────────────────────────────────
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

        # ── Two-column split ──────────────────────────────────────────
        # Left column: camera + sequencer
        # Right column: visualiser + reverb + BPM
        SEQ_COL_W  = int(W * SEQ_WIDTH_RATIO)
        RIGHT_X    = SEQ_COL_W + 8
        RIGHT_W    = W - RIGHT_X - 8

        MARGIN     = 10
        CAM_Y      = MARGIN

        # ── Camera (left column, centred) ─────────────────────────────
        cam_w, cam_h = self._camera_dims(SEQ_COL_W, H)
        cam_x = (SEQ_COL_W - cam_w) // 2

        surf = pygame.surfarray.make_surface(np.rot90(cam_rgb))
        screen.blit(pygame.transform.scale(surf, (cam_w, cam_h)), (cam_x, CAM_Y))
        self._draw_gesture_labels(screen, cam_x, CAM_Y, cam_w, cam_h, gesture)

        # Gauges flush left and right of camera
        gh = cam_h
        self._draw_gauge(screen, MARGIN, CAM_Y, gh, gesture.hand_y,
                         C_FILTER, "FILT",
                         lambda v: np.clip(1.0 - (v - 0.2) / 0.5, 0, 1))
        tc = C_TIME_LOCK if gesture.delay_locked else C_TIME
        gauge_r1 = cam_x + cam_w + 4
        gauge_r2 = gauge_r1 + 42
        self._draw_gauge(screen, gauge_r1, CAM_Y, gh, gesture.delay_time_val,
                         tc, "TIME", lambda v: v / 0.8)
        self._draw_gauge(screen, gauge_r2, CAM_Y, gh, gesture.delay_feedback,
                         C_FEEDBACK, "FB", lambda v: v / 0.85)

        cam_bottom = CAM_Y + cam_h + MARGIN

        # ── BPM + step buttons (below camera, left col) ───────────────
        bpm_s = self.font.render(f"BPM  {bpm}", True, (170, 170, 195))
        screen.blit(bpm_s, (MARGIN, cam_bottom + 2))

        self._step_btn_rects = []
        bw, bh = 42, 22
        bx = MARGIN + bpm_s.get_width() + 14
        for i, opt in enumerate(STEP_OPTIONS):
            r = pygame.Rect(bx + i * (bw + 5), cam_bottom + 1, bw, bh)
            col = C_STEPS_ON if opt == active_steps else C_STEPS_OFF
            pygame.draw.rect(screen, col, r, border_radius=4)
            lbl = self.font_sm.render(str(opt), True, C_BTN_TEXT)
            screen.blit(lbl, (r.centerx - lbl.get_width() // 2,
                               r.centery - lbl.get_height() // 2))
            self._step_btn_rects.append(r)

        seq_top = cam_bottom + bh + 8

        # ── Sequencer grid ────────────────────────────────────────────
        # Space budget: SEQ_COL_W minus left margin, label col, vol col
        LABEL_W = 52   # name + del button
        VOL_W   = 20   # volume slider width
        VOL_GAP =  4
        CELL_PAD = 4   # total horizontal padding inside cell area

        grid_x  = MARGIN + LABEL_W + VOL_W + VOL_GAP
        grid_w  = SEQ_COL_W - grid_x - MARGIN
        cw      = max(8, grid_w // active_steps)

        self._seq_top = seq_top
        self._seq_sx  = grid_x
        self._seq_cw  = cw
        self._del_btn_rects = []

        for r in range(n_rows):
            ry = seq_top + r * ROW_H

            # Row background stripe (alternating)
            stripe_col = (18, 18, 26) if r % 2 == 0 else (14, 14, 20)
            pygame.draw.rect(screen, stripe_col,
                             (MARGIN, ry, SEQ_COL_W - MARGIN, ROW_H - 2),
                             border_radius=3)

            # Voice name — clipped to LABEL_W
            name = (voice_names[r] if r < len(voice_names) else f"R{r}")[:9]
            lbl  = self.font_xs.render(name, True, C_LABEL)
            screen.blit(lbl, (MARGIN + 2, ry + 4))

            # Delete button — all rows (including built-ins)
            del_r = pygame.Rect(MARGIN + 2, ry + ROW_H - 16, 14, 12)
            pygame.draw.rect(screen, C_DEL_BTN, del_r, border_radius=2)
            x_s = self.font_xs.render("✕", True, (255, 255, 255))
            screen.blit(x_s, (del_r.x + 2, del_r.y))
            self._del_btn_rects.append((r, del_r))

            # Volume slider
            vs = self.vol_sliders[r]
            vs.rect = pygame.Rect(MARGIN + LABEL_W, ry + 5, VOL_W, ROW_H - 10)
            vs.draw(screen, self.font_xs, show_label=False, show_value=False)

            # Step cells
            for c in range(active_steps):
                cx_pos = grid_x + c * cw
                rect   = pygame.Rect(cx_pos + 2, ry + 4, cw - 4, ROW_H - 10)
                is_on  = bool(grid[r, c]) if c < grid.shape[1] else False
                base   = np.array(C_ACTIVE if is_on else C_INACTIVE, dtype=float)
                flash  = self.step_flashes[c] * 80 if c < len(self.step_flashes) else 0
                colour = tuple(np.clip(base + flash, 0, 255).astype(int))

                if c == visual_step:
                    pygame.draw.rect(screen, C_PLAYHEAD, rect.inflate(2, 2), 2)
                    if c < len(self.step_flashes):
                        self.step_flashes[c] = 1.0

                pygame.draw.rect(screen, colour, rect, border_radius=3)

                # Beat dividers every 4
                if c > 0 and c % 4 == 0:
                    pygame.draw.line(screen, (50, 50, 65),
                                     (cx_pos, ry + 2), (cx_pos, ry + ROW_H - 4))

        self.step_flashes *= 0.8

        seq_bottom = seq_top + n_rows * ROW_H

        # Add voice button
        add_r = pygame.Rect(MARGIN, seq_bottom + 6, 100, 24)
        pygame.draw.rect(screen, C_ADD_BTN, add_r, border_radius=4)
        screen.blit(self.font_xs.render("+ Add voice", True, C_BTN_TEXT),
                    (add_r.x + 6, add_r.y + 6))
        self._add_btn_rect = add_r

        # ── Right panel ───────────────────────────────────────────────
        # Divider line
        pygame.draw.line(screen, C_BORDER,
                         (RIGHT_X - 4, MARGIN), (RIGHT_X - 4, H - MARGIN), 1)

        panel_y = MARGIN

        # ── Visualiser panel (top of right col) ───────────────────────
        VIS_H = max(160, H // 3)
        self._draw_vis_panel(screen, RIGHT_X, panel_y, RIGHT_W, VIS_H,
                             waveform, hit_envs)
        panel_y += VIS_H + 12

        # ── Reverb section ────────────────────────────────────────────
        pygame.draw.rect(screen, C_PANEL,
                         (RIGHT_X, panel_y, RIGHT_W, 130), border_radius=6)
        rev_lbl = self.font.render("REVERB", True, C_REVERB)
        screen.blit(rev_lbl, (RIGHT_X + 8, panel_y + 6))

        SL_H, SL_W, SL_GAP = 72, 26, 10
        sl_y = panel_y + 26
        self.slider_size.rect = pygame.Rect(RIGHT_X + 10,              sl_y, SL_W, SL_H)
        self.slider_wet.rect  = pygame.Rect(RIGHT_X + 10 + SL_W + SL_GAP, sl_y, SL_W, SL_H)
        for sl in self.sliders:
            sl.draw(screen, self.font_xs, show_label=True, show_value=True)

        panel_y += 130 + 10

        # ── SET BPM button ────────────────────────────────────────────
        tap_btn = pygame.Rect(RIGHT_X + (RIGHT_W - 130) // 2, panel_y, 130, 32)
        pygame.draw.rect(screen, C_BTN, tap_btn, border_radius=8)
        screen.blit(self.font.render("SET BPM", True, C_BTN_TEXT),
                    (tap_btn.centerx - 30, tap_btn.y + 8))
        self._tap_btn = tap_btn

        # ── Tap modal ─────────────────────────────────────────────────
        ebtn = None
        if tap_mode:
            ebtn = self._draw_modal(screen, W, H, bpm)

        return tap_btn, ebtn

    # ── Visualiser panel ──────────────────────────────────────────────
    def _draw_vis_panel(self, screen, x, y, w, h, waveform, hit_envs):
        """
        Top half: oscilloscope (live output waveform).
        Bottom half: amplitude histogram (bar per frequency bucket).
        Overlaid: per-hit envelope traces.
        """
        pygame.draw.rect(screen, C_PANEL, (x, y, w, h), border_radius=6)
        pygame.draw.rect(screen, C_BORDER, (x, y, w, h), border_radius=6, width=1)

        half_h  = h // 2
        osc_y   = y + 4
        hist_y  = y + half_h + 4
        osc_h   = half_h - 8
        hist_h  = half_h - 8
        inner_w = w - 8

        # ── Labels ────────────────────────────────────────────────────
        screen.blit(self.font_xs.render("OSCILLOSCOPE", True, (60, 80, 90)),
                    (x + 6, osc_y))
        screen.blit(self.font_xs.render("AMPLITUDE", True, (60, 80, 90)),
                    (x + 6, hist_y))

        osc_y  += 14
        hist_y += 14
        osc_h  -= 14
        hist_h -= 14

        # Divider between osc and hist
        pygame.draw.line(screen, C_BORDER,
                         (x + 4, y + half_h), (x + w - 4, y + half_h), 1)

        # ── Oscilloscope ──────────────────────────────────────────────
        mid_osc = osc_y + osc_h // 2
        pygame.draw.line(screen, (30, 35, 45),
                         (x + 4, mid_osc), (x + w - 4, mid_osc), 1)

        if len(waveform) > 1:
            n_pts = inner_w
            idxs  = np.linspace(0, len(waveform) - 1, n_pts).astype(int)
            vals  = np.clip(waveform[idxs], -1, 1)
            pts   = [
                (x + 4 + i, int(mid_osc - vals[i] * (osc_h // 2 - 2)))
                for i in range(n_pts)
            ]
            if len(pts) > 1:
                pygame.draw.lines(screen, C_WAVEFORM, False, pts, 2)

        # Per-hit envelope overlays on oscilloscope
        for sample_data, pos in hit_envs:
            if pos >= len(sample_data) or len(sample_data) < 2:
                continue
            env_w  = min(inner_w, int(inner_w * 0.6))
            idxs   = np.linspace(0, len(sample_data) - 1, env_w).astype(int)
            vals   = np.clip(sample_data[idxs], -1, 1)
            fade   = max(0.0, 1.0 - pos / len(sample_data))
            col    = tuple(int(c * fade) for c in C_ENVELOPE)
            if sum(col) < 10:
                continue
            pts = [
                (x + 4 + i, int(mid_osc - vals[i] * (osc_h // 2 - 2)))
                for i in range(env_w)
            ]
            if len(pts) > 1:
                pygame.draw.lines(screen, col, False, pts, 1)

        # ── Amplitude histogram ────────────────────────────────────────
        # Bucket the waveform into N bars showing RMS per segment
        N_BARS = min(inner_w // 4, 48)
        if len(waveform) > N_BARS and N_BARS > 0:
            chunks    = np.array_split(waveform, N_BARS)
            rms_vals  = np.array([np.sqrt(np.mean(c ** 2)) for c in chunks])
            rms_max   = rms_vals.max()
            bar_w     = inner_w // N_BARS
            for i, rms in enumerate(rms_vals):
                bar_h_px = int((rms / max(rms_max, 1e-6)) * hist_h)
                if bar_h_px < 1:
                    continue
                bx = x + 4 + i * bar_w
                by = hist_y + hist_h - bar_h_px
                # Colour: cool blue → hot white as amplitude rises
                intensity = rms / max(rms_max, 1e-6)
                r_c = int(C_HIST[0] + (255 - C_HIST[0]) * intensity)
                g_c = int(C_HIST[1] + (255 - C_HIST[1]) * intensity ** 2)
                b_c = int(C_HIST[2])
                bar_col = (
                    min(255, r_c),
                    min(255, g_c),
                    min(255, b_c),
                )
                pygame.draw.rect(screen, bar_col,
                                 (bx, by, max(1, bar_w - 1), bar_h_px),
                                 border_radius=1)

    # ── Hit-testing helpers ────────────────────────────────────────────
    def hit_step(self, mx, my, active_steps, n_rows) -> tuple[int, int] | None:
        sx, cw, st = self._seq_sx, self._seq_cw, self._seq_top
        if cw <= 0:
            return None
        c = (mx - sx) // cw
        r = (my - st) // ROW_H
        if 0 <= r < n_rows and 0 <= c < active_steps:
            rect = pygame.Rect(sx + c * cw + 2, st + r * ROW_H + 4, cw - 4, ROW_H - 10)
            if rect.collidepoint(mx, my):
                return int(r), int(c)
        return None

    def hit_step_btn(self, mx, my) -> int | None:
        for rect, opt in zip(self._step_btn_rects, STEP_OPTIONS):
            if rect.collidepoint(mx, my):
                return opt
        return None

    def hit_add_btn(self, mx, my) -> bool:
        return self._add_btn_rect is not None and self._add_btn_rect.collidepoint(mx, my)

    def hit_del_btn(self, mx, my) -> int | None:
        for row, rect in self._del_btn_rects:
            if rect and rect.collidepoint(mx, my):
                return row
        return None

    # ── Private helpers ────────────────────────────────────────────────
    def _camera_dims(self, col_w: int, H: int):
        """Camera fills most of the left column width."""
        cam_w = col_w - 100   # leave room for gauges on both sides
        cam_h = int(cam_w * 0.75)
        max_h = int(H * 0.32)
        if cam_h > max_h:
            cam_h = max_h
            cam_w = int(cam_h * 1.33)
        return cam_w, cam_h

    def _draw_gauge(self, screen, x, y, gh, value, colour, label, normalise):
        GW = 18
        pygame.draw.rect(screen, C_TRACK_BG, (x, y, GW, gh), border_radius=4)
        fh = int(normalise(value) * gh)
        if fh > 0:
            pygame.draw.rect(screen, colour, (x, y + gh - fh, GW, fh), border_radius=4)
        lbl = self.font_xs.render(label, True, colour)
        screen.blit(lbl, (x, y + gh + 3))

    def _draw_gesture_labels(self, screen, cx, cy, cw, ch, gesture):
        for lx, ly, col, txt in [
            (cx + 5, cy + 5,      C_FILTER, "R: height=filter  pinch=crush"),
            (cx + 5, cy + ch - 20, C_TIME,  "L: tilt=time  height=fb  ✌=lock"),
        ]:
            s  = self.font_xs.render(txt, True, col)
            bg = pygame.Surface((s.get_width() + 6, s.get_height() + 4), pygame.SRCALPHA)
            bg.fill((0, 0, 0, 140))
            screen.blit(bg, (lx - 3, ly - 2))
            screen.blit(s, (lx, ly))
        if gesture.is_fist:
            s = self.font_xs.render("● CRUSH", True, (255, 200, 0))
            screen.blit(s, (cx + cw - 60, cy + 5))
        if gesture.delay_locked:
            s = self.font_xs.render("🔒 LOCKED", True, C_TIME_LOCK)
            screen.blit(s, (cx + cw - 70, cy + ch - 20))

    def _tap_btn_rect(self, W, H):
        return self._tap_btn or pygame.Rect(W - 150, H - 50, 130, 32)

    def _draw_modal(self, screen, W, H, bpm):
        ov = pygame.Surface((W, H), pygame.SRCALPHA)
        ov.fill((0, 0, 0, 220))
        screen.blit(ov, (0, 0))
        box = pygame.Rect(W // 2 - 190, H // 2 - 95, 380, 190)
        pygame.draw.rect(screen, (45, 45, 58), box, border_radius=12)
        pygame.draw.rect(screen, C_BORDER, box, border_radius=12, width=1)
        t = self.font.render(f"TAP HERE   {bpm} BPM", True, (230, 230, 230))
        screen.blit(t, (box.centerx - t.get_width() // 2, box.y + 44))
        h = self.font_sm.render("Click repeatedly to set tempo", True, (130, 130, 145))
        screen.blit(h, (box.centerx - h.get_width() // 2, box.y + 80))
        ebtn = pygame.Rect(box.centerx - 44, box.bottom - 46, 88, 30)
        pygame.draw.rect(screen, C_MODAL_BTN, ebtn, border_radius=5)
        screen.blit(self.font.render("DONE", True, C_BTN_TEXT),
                    (ebtn.x + 22, ebtn.y + 6))
        return ebtn