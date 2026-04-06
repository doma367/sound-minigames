#include "DualcastPage.h"
#include "MainComponent.h"

// ██████████████████████████████████████████████████████████
//  BUILD TAG — if you do NOT see "FIXED-V3" in large red
//  text centred in the top bar, you are NOT running this file.
// ██████████████████████████████████████████████████████████
static const char* BUILD_TAG = "FIXED-V3";

// ============================================================
//  Colour palette
// ============================================================
const juce::Colour DualcastPage::DC_BG    { 0xf2050505 };
const juce::Colour DualcastPage::DC_DRONE { 0xff3c8cff };
const juce::Colour DualcastPage::DC_LEAD  { 0xff32d282 };
const juce::Colour DualcastPage::DC_DIM   { 0x885a5a64 };
const juce::Colour DualcastPage::DC_TEXT  { 0xffe1e1e1 };

// ============================================================
//  DcVoice::generate
//  FIX: envelope uses exp(-k) per sample, not a linear lerp.
// ============================================================
void DcVoice::generate (float* out, int frames, double sr)
{
    double tgt = on ? 1.0 : 0.0;

    // ---- Karplus-Strong pluck ----
    if (soundIdx == 3)
    {
        if (on && !prevOn) ks.trigger (freq, sr);
        if (!on && prevOn) ks.release();
        prevOn = on;
        for (int i = 0; i < frames; ++i) out[i] = ks.tick();
        return;
    }

    // ---- Soft Pad: k=0.0025 ----
    if (soundIdx == 2)
    {
        double inc  = juce::MathConstants<double>::twoPi * freq / sr;
        double expK = std::exp (-0.0025);
        for (int i = 0; i < frames; ++i)
        {
            phase   += inc;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            padGain  = tgt + (padGain - tgt) * expK;
            out[i]   = (float)((std::sin (phase) + 0.3 * std::sin (2.0 * phase)) * padGain);
        }
        prevOn = on;
        return;
    }

    // ---- Sine / Organ: k=0.004 ----
    double inc  = juce::MathConstants<double>::twoPi * freq / sr;
    double expK = std::exp (-0.004);
    for (int i = 0; i < frames; ++i)
    {
        phase   += inc;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
        envGain  = tgt + (envGain - tgt) * expK;

        float sig;
        if (soundIdx == 1)
        {
            sig = 0.0f;
            float denom = 0.0f;
            double nyq  = sr * 0.45;
            for (int h = 1; freq * h < nyq && h < 20; h += 2)
            {
                sig   += (1.0f / h) * (float) std::sin (h * phase);
                denom += 1.0f / h;
            }
            if (denom > 0.0f) sig /= denom;
        }
        else
        {
            sig = (float) std::sin (phase);
        }
        out[i] = sig * (float) envGain;
    }
    prevOn = on;
}

// ============================================================
//  DualcastAudioEngine
// ============================================================
DualcastAudioEngine::DualcastAudioEngine()
{
    drone.soundIdx = 1;
    lead.soundIdx  = 1;
    drone.freq     = DualcastTheory::stepToFreq (2, { 0,2,4,7,9 }, 2);
    lead.freq      = DualcastTheory::stepToFreq (2, { 0,2,4,7,9 }, 3);
}

void DualcastAudioEngine::setDroneOn  (bool v)   { const juce::ScopedLock sl(lock); drone.on       = v; }
void DualcastAudioEngine::setDroneFreq(double f)  { const juce::ScopedLock sl(lock); drone.freq     = f; }
void DualcastAudioEngine::setDroneSnd (int i)     { const juce::ScopedLock sl(lock); drone.soundIdx = i; }
void DualcastAudioEngine::setLeadOn   (bool v)    { const juce::ScopedLock sl(lock); lead.on        = v; }
void DualcastAudioEngine::setLeadFreq (double f)  { const juce::ScopedLock sl(lock); lead.freq      = f; }
void DualcastAudioEngine::setLeadSnd  (int i)     { const juce::ScopedLock sl(lock); lead.soundIdx  = i; }
void DualcastAudioEngine::setMasterVol(double v)  { const juce::ScopedLock sl(lock); masterVol      = v; }

void DualcastAudioEngine::audioDeviceIOCallbackWithContext (
    const float* const*, int,
    float* const* outData, int numCh, int frames,
    const juce::AudioIODeviceCallbackContext&)
{
    std::vector<float> dBuf ((size_t) frames, 0.0f);
    std::vector<float> lBuf ((size_t) frames, 0.0f);
    double mv, sr;
    {
        const juce::ScopedLock sl (lock);
        mv = masterVol;
        sr = sampleRate;
        drone.generate (dBuf.data(), frames, sr);
        lead.generate  (lBuf.data(), frames, sr);
    }

    // FIX: volume uses exp(-k) decay, not linear lerp
    const double expK = std::exp (-0.006);
    for (int i = 0; i < frames; ++i)
    {
        volGain = mv + (volGain - mv) * expK;
        float mixed = (dBuf[(size_t)i] * 0.45f + lBuf[(size_t)i] * 0.30f) * (float) volGain;
        mixed = juce::jlimit (-1.0f, 1.0f, mixed);
        for (int ch = 0; ch < numCh; ++ch)
            outData[ch][i] = mixed;
    }
}

// ============================================================
//  Constructor / destructor
// ============================================================
DualcastPage::DualcastPage (MainComponent& mc) : mainComponent (mc)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (cameraView);
    // FIX: without this, cameraView swallows all mouse clicks in the camera
    // area, so dropdown items can never be clicked.
    cameraView.setInterceptsMouseClicks (false, false);

    backButton.onClick = [this] { stop(); mainComponent.showLanding(); };
    addAndMakeVisible (backButton);

    if (osc.connect (9000))
        osc.addListener (this);
}

DualcastPage::~DualcastPage()
{
    stop();
    setLookAndFeel (nullptr);
}

// ============================================================
//  start / stop
// ============================================================
void DualcastPage::start()
{
    videoReceiver.startReceiver();
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&audioEngine);
    startTimerHz (40);
}

void DualcastPage::stop()
{
    stopTimer();
    videoReceiver.stopReceiver();
    deviceManager.removeAudioCallback (&audioEngine);
    deviceManager.closeAudioDevice();
    audioEngine.setDroneOn (false);
    audioEngine.setLeadOn  (false);
}

// ============================================================
//  OSC receive
// ============================================================
void DualcastPage::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto addr = m.getAddressPattern().toString();
    if (addr == "/pose")
    {
        int n = juce::jmin (m.size(), kNumLmF);
        for (int i = 0; i < n; ++i)
            if (m[i].isFloat32())
                oscPose[i].store (m[i].getFloat32(), std::memory_order_relaxed);
    }
    else if (addr == "/hands")
    {
        int n = juce::jmin (m.size(), kHandFloats);
        for (int i = 0; i < n; ++i)
            if (m[i].isFloat32())
                oscHands[i].store (m[i].getFloat32(), std::memory_order_relaxed);
    }
}

// ============================================================
//  parseHandsOSC
//  FIX: Python only EMA-smooths hx/hy (palm position for display).
//  spread and pinch are RAW — smoothing spread was preventing
//  open-hand from ever crossing the 0.20 threshold.
// ============================================================
void DualcastPage::parseHandsOSC()
{
    numHands = juce::jlimit (0, kMaxHands,
                             (int) oscHands[0].load (std::memory_order_relaxed));
    const int stride = 1 + kHandLmCount * 2;

    for (int h = 0; h < numHands; ++h)
    {
        int base     = 1 + h * stride;
        HandData& hd = hands[h];
        hd.valid     = true;

        float label = oscHands[base].load (std::memory_order_relaxed);
        hd.isLeft   = (label < 0.5f);

        float rawPalmX = oscHands[base + 1 + 9*2    ].load (std::memory_order_relaxed);
        float rawPalmY = oscHands[base + 1 + 9*2 + 1].load (std::memory_order_relaxed);

        // EMA only on position (channels 0-3 for two hands)
        hd.palmX = ema (h*2,     rawPalmX);
        hd.palmY = ema (h*2 + 1, rawPalmY);

        // Spread: RAW, no EMA
        const int tips[5] = { 4, 8, 12, 16, 20 };
        float spread = 0.0f;
        for (int t : tips)
        {
            float tx = oscHands[base + 1 + t*2    ].load (std::memory_order_relaxed);
            float ty = oscHands[base + 1 + t*2 + 1].load (std::memory_order_relaxed);
            spread  += std::hypot (tx - rawPalmX, ty - rawPalmY);
        }
        hd.spread = spread / 5.0f;

        // Pinch: RAW, no EMA
        float t4x = oscHands[base + 1 + 4*2    ].load (std::memory_order_relaxed);
        float t4y = oscHands[base + 1 + 4*2 + 1].load (std::memory_order_relaxed);
        float t8x = oscHands[base + 1 + 8*2    ].load (std::memory_order_relaxed);
        float t8y = oscHands[base + 1 + 8*2 + 1].load (std::memory_order_relaxed);
        hd.pinch  = std::hypot (t4x - t8x, t4y - t8y) < 0.055f;
    }

    for (int h = numHands; h < kMaxHands; ++h)
        hands[h].valid = false;
}

// ============================================================
//  timerCallback  40 Hz
// ============================================================
void DualcastPage::timerCallback()
{
    constexpr float dt = 1.0f / 40.0f;
    parseHandsOSC();

    HandData* rightHand = nullptr;
    HandData* leftHand  = nullptr;
    for (int h = 0; h < numHands; ++h)
    {
        if (!hands[h].valid) continue;
        if (!hands[h].isLeft && rightHand == nullptr) rightHand = &hands[h];
        if ( hands[h].isLeft && leftHand  == nullptr) leftHand  = &hands[h];
    }

    // ── RIGHT → volume ───────────────────────────────────────
    if (rightHand != nullptr)
    {
        float raw = juce::jlimit (0.0f, 1.0f, 1.15f - rightHand->palmY * 1.3f);
        masterVol = ema (4, raw);  // channel 4, clear of palm channels 0-3
    }
    audioEngine.setMasterVol ((double) masterVol);

    // ── LEFT → pitch + trigger ───────────────────────────────
    if (leftHand != nullptr)
    {
        bool isDrone  = (leftHand->palmX < 0.5f);
        auto& vs      = isDrone ? droneState    : leadState;
        auto& openTmr = isDrone ? droneOpenTimer : leadOpenTimer;
        auto& fistTmr = isDrone ? droneFistTimer : leadFistTimer;

        // Reset inactive side so hold state doesn't bleed across
        if (isDrone) { leadOpenTimer.reset();  leadFistTimer.reset();  }
        else         { droneOpenTimer.reset(); droneFistTimer.reset(); }

        if (openTmr.update (leftHand->spread > 0.20f, dt))
            isDrone ? audioEngine.setDroneOn (true)  : audioEngine.setLeadOn (true);
        if (fistTmr.update (leftHand->spread < 0.10f, dt))
            isDrone ? audioEngine.setDroneOn (false) : audioEngine.setLeadOn (false);

        double freq = vs.updateStep (leftHand->pinch, leftHand->palmY);
        if (leftHand->pinch)
            isDrone ? audioEngine.setDroneFreq (freq) : audioEngine.setLeadFreq (freq);
    }
    else
    {
        droneOpenTimer.reset(); droneFistTimer.reset();
        leadOpenTimer.reset();  leadFistTimer.reset();
    }

    audioEngine.setDroneSnd (droneState.soundIdx);
    audioEngine.setLeadSnd  (leadState.soundIdx);

    // ── Hand pixel positions for overlay ────────────────────
    for (int h = 0; h < kMaxHands; ++h) handPx[h].valid = false;

    if (rightHand != nullptr)
    {
        int idx = (rightHand == &hands[0]) ? 0 : 1;
        handPx[idx] = { (int)(rightHand->palmX * cameraRect.getWidth())  + cameraRect.getX(),
                         (int)(rightHand->palmY * cameraRect.getHeight()) + cameraRect.getY(),
                         true, rightHand->pinch, false, juce::Colour (0xff3cd250) };
    }
    if (leftHand != nullptr)
    {
        int  idx     = (leftHand == &hands[0]) ? 0 : 1;
        bool isDrone = (leftHand->palmX < 0.5f);
        bool on      = isDrone ? audioEngine.isDroneOn() : audioEngine.isLeadOn();
        handPx[idx]  = { (int)(leftHand->palmX * cameraRect.getWidth())  + cameraRect.getX(),
                          (int)(leftHand->palmY * cameraRect.getHeight()) + cameraRect.getY(),
                          true, leftHand->pinch, on, isDrone ? DC_DRONE : DC_LEAD };
    }

    repaint();
}

// ============================================================
//  resized
// ============================================================
void DualcastPage::resized()
{
    int w = getWidth(), h = getHeight();
    backButton.setBounds (w - 100, 8, 80, 26);
    cameraRect = { 0, 40, w, h - 40 - 72 };
    hudRect    = { 0, h - 72, w, 72 };
    cameraView.setBounds (cameraRect);
    buildButtonRects();
}

void DualcastPage::buildButtonRects()
{
    int w = getWidth(), bw = 148, bh = 30, topY = 8;
    int dCx = w/4, lCx = 3*w/4;
    droneScaleBtn.r = { dCx - bw/2, topY,          bw, bh };
    droneSoundBtn.r = { dCx - bw/2, topY + bh + 4, bw, bh };
    leadScaleBtn.r  = { lCx - bw/2, topY,          bw, bh };
    leadSoundBtn.r  = { lCx - bw/2, topY + bh + 4, bw, bh };
}

// ============================================================
//  mouseDown
// ============================================================
void DualcastPage::mouseDown (const juce::MouseEvent& e)
{
    auto pt = e.getPosition();

    auto hitDropdown = [&](int count, int& selected,
                            juce::Rectangle<int> anchor, bool isDrone) -> bool
    {
        const int iw = 155, ih = 26;
        int ax = anchor.getX(), ay = anchor.getBottom() + 3;
        for (int i = 0; i < count; ++i)
        {
            juce::Rectangle<int> ir { ax, ay + i*(ih+2), iw, ih };
            if (ir.contains (pt))
            {
                selected = i;
                if (isDrone) droneState.resetStep();
                else         leadState.resetStep();
                openMenu = MenuState::None;
                repaint();
                return true;
            }
        }
        return false;
    };

    if (openMenu == MenuState::DroneScale)
    {
        hitDropdown (DualcastTheory::NUM_SCALES, droneState.scaleIdx, droneScaleBtn.r, true);
        openMenu = MenuState::None; repaint(); return;
    }
    if (openMenu == MenuState::DroneSound)
    {
        hitDropdown (DualcastTheory::NUM_SOUNDS, droneState.soundIdx, droneSoundBtn.r, true);
        openMenu = MenuState::None; repaint(); return;
    }
    if (openMenu == MenuState::LeadScale)
    {
        hitDropdown (DualcastTheory::NUM_SCALES, leadState.scaleIdx, leadScaleBtn.r, false);
        openMenu = MenuState::None; repaint(); return;
    }
    if (openMenu == MenuState::LeadSound)
    {
        hitDropdown (DualcastTheory::NUM_SOUNDS, leadState.soundIdx, leadSoundBtn.r, false);
        openMenu = MenuState::None; repaint(); return;
    }

    if      (droneScaleBtn.r.contains (pt)) openMenu = (openMenu == MenuState::DroneScale) ? MenuState::None : MenuState::DroneScale;
    else if (droneSoundBtn.r.contains (pt)) openMenu = (openMenu == MenuState::DroneSound) ? MenuState::None : MenuState::DroneSound;
    else if (leadScaleBtn.r.contains  (pt)) openMenu = (openMenu == MenuState::LeadScale)  ? MenuState::None : MenuState::LeadScale;
    else if (leadSoundBtn.r.contains  (pt)) openMenu = (openMenu == MenuState::LeadSound)  ? MenuState::None : MenuState::LeadSound;
    else                                     openMenu = MenuState::None;
    repaint();
}

// ============================================================
//  paint
// ============================================================
void DualcastPage::paint (juce::Graphics& g)
{
    drawBackground (g);

    // ── WATERMARK: large red text — if missing, wrong file ──
    g.setColour (juce::Colours::red);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (20.0f)));
    g.drawText  (juce::String (BUILD_TAG), 0, 0, getWidth(), 40, juce::Justification::centred);

    if (!cameraRect.isEmpty())
    {
        g.setColour (juce::Colour (0x33ffffff));
        g.drawRect  (cameraRect.toFloat(), 1.0f);

        drawDivider (g);

        float nowSec = (float)(juce::Time::getMillisecondCounterHiRes() * 0.001);
        int   cx     = cameraRect.getX() + cameraRect.getWidth() / 2;

        drawNoteGuidelines (g, droneState, cameraRect.getX() + 2, cx - 2,
                            DC_DRONE, audioEngine.isDroneOn(), nowSec);
        drawNoteGuidelines (g, leadState,  cx + 2, cameraRect.getRight() - 2,
                            DC_LEAD,  audioEngine.isLeadOn(),  nowSec);

        for (auto& hp : handPx)
            if (hp.valid)
                drawHandGlow (g, hp.x, hp.y, hp.accent, hp.on, hp.pinch);

        for (int h = 0; h < kMaxHands; ++h)
            if (handPx[h].valid && handPx[h].accent == juce::Colour (0xff3cd250))
                drawVolumeBar (g, masterVol, handPx[h].x, juce::jmax (10, handPx[h].y - 110));
    }

    if (!hudRect.isEmpty()) drawHUD (g);

    g.setColour (juce::Colour (0xffff3333));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.drawText  ("// DUALCAST", backButton.getRight() + 8, 8, 200, 24, juce::Justification::centredLeft);

    drawVoiceControls (g, true);
    drawVoiceControls (g, false);
    drawZoneLabel     (g, true);
    drawZoneLabel     (g, false);

    // Dropdowns last — float above everything
    if (openMenu == MenuState::DroneScale)
        drawDropdown (g, DualcastTheory::NUM_SCALES, droneState.scaleIdx, droneScaleBtn.r, DC_DRONE, true);
    if (openMenu == MenuState::DroneSound)
        drawDropdown (g, DualcastTheory::NUM_SOUNDS, droneState.soundIdx, droneSoundBtn.r, DC_DRONE, false);
    if (openMenu == MenuState::LeadScale)
        drawDropdown (g, DualcastTheory::NUM_SCALES, leadState.scaleIdx,  leadScaleBtn.r,  DC_LEAD,  true);
    if (openMenu == MenuState::LeadSound)
        drawDropdown (g, DualcastTheory::NUM_SOUNDS, leadState.soundIdx,  leadSoundBtn.r,  DC_LEAD,  false);
}

// ============================================================
//  Drawing helpers
// ============================================================
void DualcastPage::drawBackground (juce::Graphics& g)
{
    g.fillAll (DC_BG);
    g.setColour (juce::Colour (0x08ff3333));
    for (int x = 0; x < getWidth();  x += 40) g.drawVerticalLine   (x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40) g.drawHorizontalLine (y, 0.0f, (float)getWidth());

    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xaaff3333));
    g.drawLine (b.getX(),          b.getY(),           b.getX()+20,     b.getY(),          1.5f);
    g.drawLine (b.getX(),          b.getY(),           b.getX(),         b.getY()+20,       1.5f);
    g.drawLine (b.getRight()-20,   b.getY(),           b.getRight(),     b.getY(),          1.5f);
    g.drawLine (b.getRight(),      b.getY(),            b.getRight(),    b.getY()+20,       1.5f);
    g.drawLine (b.getX(),          b.getBottom(),       b.getX()+20,    b.getBottom(),      1.5f);
    g.drawLine (b.getX(),          b.getBottom()-20,   b.getX(),         b.getBottom(),     1.5f);
    g.drawLine (b.getRight()-20,   b.getBottom(),       b.getRight(),    b.getBottom(),     1.5f);
    g.drawLine (b.getRight(),      b.getBottom()-20,   b.getRight(),     b.getBottom(),     1.5f);

    juce::ColourGradient gr (juce::Colours::transparentBlack, b.getX(), b.getY(),
                              juce::Colours::transparentBlack, b.getRight(), b.getY(), false);
    gr.addColour (0.2, juce::Colour (0x55ff3333));
    gr.addColour (0.5, juce::Colour (0xffff3333));
    gr.addColour (0.8, juce::Colour (0x55ff3333));
    g.setGradientFill (gr);
    g.fillRect (b.getX(), b.getY(), b.getWidth(), 1.5f);
}

void DualcastPage::drawDivider (juce::Graphics& g)
{
    int cx   = cameraRect.getX() + cameraRect.getWidth() / 2;
    int topY = cameraRect.getY() + TOP_Y_OFFSET;
    int botY = cameraRect.getBottom() - BOT_Y_OFFSET;
    g.setColour (juce::Colour (0x66373744));
    g.drawLine ((float)cx, (float)topY, (float)cx, (float)botY, 1.0f);
}

void DualcastPage::drawNoteGuidelines (juce::Graphics& g, const DcVoiceState& vs,
                                        int xLeft, int xRight,
                                        juce::Colour accent, bool voiceOn, float nowSec)
{
    const auto& scale = vs.scale();
    int n = (int) scale.size();
    if (n == 0) return;

    // FIX: topY/botY are offsets inside cameraRect, not from component top
    int topY   = cameraRect.getY() + TOP_Y_OFFSET;
    int botY   = cameraRect.getBottom() - BOT_Y_OFFSET;
    int rangeY = botY - topY;
    if (rangeY <= 0) return;

    g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));

    for (int i = 0; i < n; ++i)
    {
        int y = (n == 1) ? (topY + botY) / 2
                         : botY - (int)((float)i / (float)(n-1) * (float)rangeY);

        bool isCur     = (i == vs.step);
        bool isPlaying = isCur && voiceOn;

        if (isPlaying)
        {
            float pulse = 0.55f + 0.45f * std::sin (nowSec * 8.0f);
            g.setColour (accent.withAlpha (pulse * 0.30f));
            g.drawLine  ((float)xLeft, (float)y, (float)xRight, (float)y, 7.0f);
            g.setColour (accent.withAlpha (pulse));
            g.drawLine  ((float)xLeft, (float)y, (float)xRight, (float)y, 2.0f);
        }
        else if (isCur)
        {
            g.setColour (accent.withAlpha (0.55f));
            g.drawLine  ((float)xLeft, (float)y, (float)xRight, (float)y, 1.0f);
        }
        else
        {
            g.setColour (DC_DIM);
            g.drawLine  ((float)xLeft, (float)y, (float)xRight, (float)y, 1.0f);
        }

        juce::String lbl (DualcastTheory::NOTE_NAMES[scale[(size_t)i] % 12]);
        int labelX = (xLeft > 10) ? xLeft - 28 : xRight + 4;
        g.setColour (isCur ? accent : DC_DIM);
        g.drawText  (lbl, labelX, y - 6, 24, 13, juce::Justification::centred);
    }
}

void DualcastPage::drawHandGlow (juce::Graphics& g, int px, int py,
                                   juce::Colour accent, bool on, bool pinch)
{
    float t = (float)(juce::Time::getMillisecondCounterHiRes() * 0.001);
    if (on)
    {
        float pulse = 0.6f + 0.4f * std::sin (t * 8.0f);
        float r = 28.0f + 10.0f * pulse;
        g.setColour (accent.withAlpha (0.30f * pulse));
        g.drawEllipse ((float)px-r-6, (float)py-r-6, (r+6)*2, (r+6)*2, 4.0f);
        g.setColour (accent.withAlpha (pulse));
        g.drawEllipse ((float)px-r, (float)py-r, r*2, r*2, 2.0f);
    }
    if (pinch)
    {
        g.setColour (juce::Colours::white.withAlpha (0.80f));
        g.drawEllipse ((float)px-18, (float)py-18, 36.0f, 36.0f, 1.0f);
    }
    g.setColour (accent);
    g.fillEllipse ((float)px-5, (float)py-5, 10.0f, 10.0f);
}

void DualcastPage::drawVolumeBar (juce::Graphics& g, float vol, int cx, int ytop)
{
    const int bh = 100, bw = 10, x1 = cx - bw/2;
    g.setColour (juce::Colour (0xff141418)); g.fillRect (x1, ytop, bw, bh);
    g.setColour (DC_DIM);                   g.drawRect (x1, ytop, bw, bh, 1);
    int fill = (int)(vol * bh);
    if (fill > 0) { g.setColour (juce::Colour (0xff3cd250)); g.fillRect (x1, ytop+bh-fill, bw, fill); }
    g.setColour (juce::Colour (0xff3cd250));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (juce::String ((int)(vol*100)) + "%", cx-14, ytop+bh+4, 30, 12, juce::Justification::centred);
}

void DualcastPage::drawHUD (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0a0a0e)); g.fillRect (hudRect);
    g.setColour (DC_DIM);
    g.drawLine ((float)hudRect.getX(), (float)hudRect.getY(),
                (float)hudRect.getRight(), (float)hudRect.getY(), 1.0f);

    double dFreq = audioEngine.getDroneFreq(), lFreq = audioEngine.getLeadFreq();
    bool   dOn   = audioEngine.isDroneOn(),    lOn   = audioEngine.isLeadOn();
    int    half  = hudRect.getWidth() / 2;

    g.setFont   (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.setColour (dOn ? DC_DRONE : DC_DIM);
    g.drawText  (juce::String("DRONE  ") + DualcastTheory::freqToNoteName(dFreq) + "  " + juce::String(dFreq,1) + " Hz",
                 hudRect.getX()+12, hudRect.getY()+10, half-20, 16, juce::Justification::centredLeft);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText  (dOn ? "* ON" : "- OFF", hudRect.getX()+12, hudRect.getY()+30, 80, 14, juce::Justification::centredLeft);

    g.setFont   (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.setColour (lOn ? DC_LEAD : DC_DIM);
    g.drawText  (juce::String("LEAD   ") + DualcastTheory::freqToNoteName(lFreq) + "  " + juce::String(lFreq,1) + " Hz",
                 hudRect.getX()+half+10, hudRect.getY()+10, half-20, 16, juce::Justification::centredLeft);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText  (lOn ? "* ON" : "- OFF", hudRect.getX()+half+10, hudRect.getY()+30, 80, 14, juce::Justification::centredLeft);

    g.setColour (DC_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("Left hand: pinch+drag=step  open=play  fist=stop  |  Right hand=volume",
                 hudRect.getX()+12, hudRect.getY()+50, hudRect.getWidth()-24, 14, juce::Justification::centredLeft);
}

void DualcastPage::drawVoiceControls (juce::Graphics& g, bool isDrone)
{
    const auto& vs = isDrone ? droneState : leadState;
    juce::Colour ac = isDrone ? DC_DRONE : DC_LEAD;
    auto& scBtn = isDrone ? droneScaleBtn : leadScaleBtn;
    auto& snBtn = isDrone ? droneSoundBtn : leadSoundBtn;
    bool scAct  = isDrone ? (openMenu==MenuState::DroneScale) : (openMenu==MenuState::LeadScale);
    bool snAct  = isDrone ? (openMenu==MenuState::DroneSound) : (openMenu==MenuState::LeadSound);
    drawButtonWidget (g, "< " + vs.scaleName() + " >", scBtn.r, scAct, ac);
    drawButtonWidget (g, "~ " + vs.soundName(),        snBtn.r, snAct, ac);
}

void DualcastPage::drawZoneLabel (juce::Graphics& g, bool isDrone)
{
    int cx   = cameraRect.getX() + cameraRect.getWidth() / 2;
    int botY = cameraRect.getBottom() - BOT_Y_OFFSET;
    const auto& vs = isDrone ? droneState : leadState;
    juce::Colour ac = isDrone ? DC_DRONE : DC_LEAD;
    g.setColour (ac);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
    if (isDrone)
        g.drawText (juce::String("DRONE  Oct ") + juce::String(vs.octave),
                    cameraRect.getX()+8, botY+4, cx-cameraRect.getX()-10, 16, juce::Justification::centredLeft);
    else
        g.drawText (juce::String("LEAD  Oct ") + juce::String(vs.octave),
                    cx+8, botY+4, cameraRect.getRight()-cx-10, 16, juce::Justification::centredLeft);
}

void DualcastPage::drawButtonWidget (juce::Graphics& g, const juce::String& label,
                                      juce::Rectangle<int> r, bool active, juce::Colour accent)
{
    g.setColour (active ? accent.withAlpha(0.25f) : juce::Colour(0xff0d0d14)); g.fillRect (r);
    g.setColour (active ? accent : juce::Colour(0x55ffffff)); g.drawRect (r.toFloat(), 1.0f);
    g.setColour (active ? accent : DC_TEXT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText  (label, r, juce::Justification::centred);
}

// FIX: signature changed — isScale passed explicitly, no more guessing from count
void DualcastPage::drawDropdown (juce::Graphics& g, int count, int selected,
                                   juce::Rectangle<int> anchor, juce::Colour accent, bool isScale)
{
    const int ih = 26, iw = 155;
    int ax = anchor.getX(), ay = anchor.getBottom() + 3;
    for (int i = 0; i < count; ++i)
    {
        juce::Rectangle<int> ir { ax, ay + i*(ih+2), iw, ih };
        bool sel = (i == selected);
        g.setColour (sel ? accent.withAlpha(0.35f) : juce::Colour(0xff0d0d14)); g.fillRect (ir);
        g.setColour (sel ? accent : juce::Colour(0x55ffffff)); g.drawRect (ir.toFloat(), 1.0f);
        const char* name = isScale ? DualcastTheory::SCALES[i].name : DualcastTheory::SOUND_NAMES[i];
        g.setColour (sel ? accent : DC_TEXT);
        g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText  (juce::String(name), ir.reduced(6,0), juce::Justification::centredLeft);
    }
}