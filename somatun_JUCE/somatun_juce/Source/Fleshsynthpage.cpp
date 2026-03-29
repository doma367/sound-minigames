#include "FleshSynthPage.h"
#include "MainComponent.h"

// ============================================================
//  Colour palette (matching SomatunLookAndFeel / other pages)
// ============================================================
static const juce::Colour FS_ACCENT     { 0xffff3333 };
static const juce::Colour FS_ACCENT_DIM { 0x55ff3333 };
static const juce::Colour FS_BORDER_DIM { 0x33ff3333 };
static const juce::Colour FS_BG_DARK    { 0xf2050505 };
static const juce::Colour FS_TEXT_PRI   { 0xffe8e8e8 };
static const juce::Colour FS_TEXT_DIM   { 0x99e8e8e8 };
static const juce::Colour FS_WAVE_COL   { 0xff00ffff };

// ============================================================
//  FleshSynthPage  — constructor / destructor
// ============================================================
FleshSynthPage::FleshSynthPage (MainComponent& mc)
    : mainComponent (mc)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (cameraView);

    backButton.onClick = [this]
    {
        stop();
        mainComponent.showLanding();
    };
    addAndMakeVisible (backButton);

    // Pre-size wave buffers
    currentWave.assign (512, 0.0f);
    prevWaveNorm.assign (512, 0.0f);

    audio.playBuf.assign (512, 0.0f);
    audio.snapBuf.assign (512, 0.0f);

    // ---- OSC receiver: listen on port 9000 ----
    if (osc.connect (9000))
    {
        osc.addListener (this);
        DBG ("OSC receiver connected on port 9000");
    }
    else
    {
        DBG ("[ERROR] could not bind OSC port 9000");
    }
}

FleshSynthPage::~FleshSynthPage()
{
    stop();
    setLookAndFeel (nullptr);
}

// ============================================================
//  start / stop
// ============================================================
void FleshSynthPage::start()
{
    // --- Video receiver (TCP frames from Python) ---
    videoReceiver.startReceiver();
 
    // --- Audio ---
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
 
    startTimerHz (40);
}

void FleshSynthPage::stop()
{
    stopTimer();
 
    videoReceiver.stopReceiver();
 
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

// ============================================================
//  OSC callback — receives /pose message from Python
// ============================================================
void FleshSynthPage::oscMessageReceived (const juce::OSCMessage& m)
{
    if (m.getAddressPattern().toString() != "/pose")
        return;

    const int n = juce::jmin (m.size(), kNumFloats);
    for (int i = 0; i < n; ++i)
    {
        if (m[i].isFloat32())
            oscLandmarks[i].store (m[i].getFloat32(), std::memory_order_relaxed);
    }
}

// ============================================================
//  Timer — 40 Hz UI update
// ============================================================
void FleshSynthPage::timerCallback()
{
    // ---- Pull latest pose data from atomic buffer ----
    PoseFrame pf;
    for (int i = 0; i < kNumLandmarks; ++i)
    {
        pf.lm[i].x = oscLandmarks[i * 2    ].load (std::memory_order_relaxed);
        pf.lm[i].y = oscLandmarks[i * 2 + 1].load (std::memory_order_relaxed);
    }
    // Simple validity check: landmark 0 x must be a real normalised coordinate
    pf.valid = (pf.lm[0].x > 0.01f && pf.lm[0].x < 0.99f);

    int cw = cameraRect.getWidth();
    int ch = cameraRect.getHeight();
    if (cw <= 0 || ch <= 0) { repaint(); return; }

    // ---- Ordered body point indices: RW RE RS LS LE LW ----
    static const int ORDER[6] = {
        PoseFrame::RIGHT_WRIST, PoseFrame::RIGHT_ELBOW, PoseFrame::RIGHT_SHOULDER,
        PoseFrame::LEFT_SHOULDER, PoseFrame::LEFT_ELBOW, PoseFrame::LEFT_WRIST
    };

    if (pf.valid)
    {
        // Raw pixel positions
        float rawX[6], rawY[6];
        for (int i = 0; i < 6; ++i)
        {
            rawX[i] = pf.lm[ORDER[i]].x * (float) cw;
            rawY[i] = pf.lm[ORDER[i]].y * (float) ch;
        }

        float sX[6], sY[6];
        smoothJoints (rawX, rawY, sX, sY);

        for (int i = 0; i < 6; ++i)
        {
            jointPx[i].x = (int) sX[i];
            jointPx[i].y = (int) sY[i];
        }
        hasJoints = true;

        earPx[0] = { (int)(pf.lm[PoseFrame::LEFT_EAR ].x * cw),
                     (int)(pf.lm[PoseFrame::LEFT_EAR ].y * ch) };
        earPx[1] = { (int)(pf.lm[PoseFrame::RIGHT_EAR].x * cw),
                     (int)(pf.lm[PoseFrame::RIGHT_EAR].y * ch) };

        // ---- Wave from pose ----
        buildWaveFromPose (pf, cw, ch);

        // ---- Tilt -> pitch ----
        float headTilt = pf.lm[PoseFrame::RIGHT_EAR].y - pf.lm[PoseFrame::LEFT_EAR].y;
        tiltRawSmooth = (1.0f - TILT_SMOOTH_ALPHA) * tiltRawSmooth
                      +  TILT_SMOOTH_ALPHA * headTilt;
        float tilt = tiltRawSmooth;
        if (std::abs (tilt) < TILT_DEADZONE) tilt = 0.0f;
        float tiltNorm = juce::jlimit (-1.0f, 1.0f, tilt / MAX_TILT);

        float tiltShaped = tiltNorm * 0.55f + (tiltNorm * tiltNorm * tiltNorm) * 0.45f;
        if (tiltInvert) tiltShaped = -tiltShaped;

        float maxSemitones = tiltSliderNorm * TILT_MAX_RANGE_ST;
        float semOff       = tiltShaped * maxSemitones;
        float targetPitch  = std::pow (2.0f, semOff / 12.0f);

        pitchFactor = (1.0f - PITCH_ALPHA_TRACK) * pitchFactor
                    +  PITCH_ALPHA_TRACK * targetPitch;
    }
    else
    {
        hasJoints = false;
        pitchFactor = (1.0f - PITCH_ALPHA_RELAX) * pitchFactor
                    +  PITCH_ALPHA_RELAX * 1.0f;
    }

    pitchFactor = juce::jlimit (0.2f, 5.0f, pitchFactor);

    // Update base frequency from slider
    baseFreq = SLIDER_MIN_FREQ + freqSliderNorm * (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ);

    baseFreqSmooth = (1.0f - BASE_FREQ_SMOOTH_A) * baseFreqSmooth
                   +  BASE_FREQ_SMOOTH_A * baseFreq;

    oscFreq = baseFreqSmooth * pitchFactor;

    // Push frequency + waveform snapshot to audio state
    {
        const juce::ScopedLock sl (audio.lock);
        audio.oscFreq = oscFreq;
        if (currentWave.size() == audio.snapBuf.size())
            audio.snapBuf = currentWave;
    }

    repaint();
}

// ============================================================
//  Build normalised wave from the six body joints
// ============================================================
void FleshSynthPage::buildWaveFromPose (const PoseFrame& pf, int viewW, int viewH)
{
    // y normalised in audio space: (0.5 - lm.y) * 2 ∈ [-1, 1]
    static const int ORDER[6] = {
        PoseFrame::RIGHT_WRIST, PoseFrame::RIGHT_ELBOW, PoseFrame::RIGHT_SHOULDER,
        PoseFrame::LEFT_SHOULDER, PoseFrame::LEFT_ELBOW, PoseFrame::LEFT_WRIST
    };

    float xs[6], ys[6];
    for (int i = 0; i < 6; ++i)
    {
        xs[i] = pf.lm[ORDER[i]].x * (float) viewW;
        ys[i] = (0.5f - pf.lm[ORDER[i]].y) * 2.0f;
    }

    // Sort by x so we interpolate left → right
    int idx[6] = {0,1,2,3,4,5};
    std::sort (idx, idx+6, [&](int a, int b){ return xs[a] < xs[b]; });

    float sxs[6], sys[6];
    for (int i = 0; i < 6; ++i) { sxs[i] = xs[idx[i]]; sys[i] = ys[idx[i]]; }

    float x0 = sxs[0], x5 = sxs[5];
    float dist = std::max (x5 - x0, 1.0f);

    const int N = 512;
    std::vector<float> rawWave (N);

    for (int i = 0; i < N; ++i)
    {
        float t = (float) i / (float)(N - 1);
        float wx = x0 + t * dist;
        // Linear interpolation through the sorted control points
        float val = sys[0];
        for (int j = 0; j < 5; ++j)
        {
            if (wx >= sxs[j] && wx <= sxs[j+1])
            {
                float lt = (sxs[j+1] - sxs[j]) > 0.001f
                         ? (wx - sxs[j]) / (sxs[j+1] - sxs[j])
                         : 0.0f;
                val = sys[j] * (1.0f - lt) + sys[j+1] * lt;
                break;
            }
        }
        rawWave[i] = val;
    }

    // RMS normalise
    float rms = 0.0f;
    for (float s : rawWave) rms += s * s;
    rms = std::sqrt (rms / (float) N) + 1e-6f;
    for (float& s : rawWave) s /= (rms * 3.0f);
    for (float& s : rawWave) s = juce::jlimit (-1.0f, 1.0f, s);

    // Smooth with previous wave
    if (prevWaveNorm.size() != (size_t) N)
        prevWaveNorm.assign (N, 0.0f);

    for (int i = 0; i < N; ++i)
        prevWaveNorm[i] = (1.0f - WAVE_SMOOTH_ALPHA) * prevWaveNorm[i]
                        + WAVE_SMOOTH_ALPHA * rawWave[i];

    currentWave = prevWaveNorm;
}

// ============================================================
//  Joint smoothing (exponential moving average)
// ============================================================
void FleshSynthPage::smoothJoints (const float rawX[6], const float rawY[6],
                                    float outX[6], float outY[6])
{
    if (! smoothPts.init)
    {
        for (int i = 0; i < 6; ++i) { smoothPts.x[i] = rawX[i]; smoothPts.y[i] = rawY[i]; }
        smoothPts.init = true;
    }
    else
    {
        for (int i = 0; i < 6; ++i)
        {
            smoothPts.x[i] = (1.0f - JOINT_SMOOTH_ALPHA) * smoothPts.x[i]
                            +  JOINT_SMOOTH_ALPHA * rawX[i];
            smoothPts.y[i] = (1.0f - JOINT_SMOOTH_ALPHA) * smoothPts.y[i]
                            +  JOINT_SMOOTH_ALPHA * rawY[i];
        }
    }
    for (int i = 0; i < 6; ++i) { outX[i] = smoothPts.x[i]; outY[i] = smoothPts.y[i]; }
}

// ============================================================
//  Audio I/O callback — runs on the audio thread
// ============================================================
void FleshSynthPage::audioDeviceIOCallbackWithContext (
    const float* const*, int,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    // Morph playBuf toward the latest snapBuf under lock
    {
        const juce::ScopedLock sl (audio.lock);
        if (audio.snapBuf.size() == audio.playBuf.size() && ! audio.snapBuf.empty())
        {
            for (size_t i = 0; i < audio.playBuf.size(); ++i)
                audio.playBuf[i] = (1.0f - audio.morphAlpha) * audio.playBuf[i]
                                 +  audio.morphAlpha * audio.snapBuf[i];
        }
    }

    auto& wave = audio.playBuf;
    int   n    = (int) wave.size();

    if (n == 0)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    double oscF     = audio.oscFreq;   // double reads are atomic enough here
    double phaseInc = (oscF * n) / audio.sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        int    idx  = (int) audio.phase % n;
        double frac = audio.phase - (int) audio.phase;
        int    nxt  = (idx + 1) % n;

        float s = wave[idx] * (float)(1.0 - frac) + wave[nxt] * (float) frac;

        for (int ch = 0; ch < numOutputChannels; ++ch)
            outputChannelData[ch][i] = s;

        audio.phase += phaseInc;
        if (audio.phase >= n) audio.phase -= n;
    }
}

// ============================================================
//  Paint
// ============================================================
void FleshSynthPage::paint (juce::Graphics& g)
{
    g.fillAll (FS_BG_DARK);

    // Grid
    g.setColour (juce::Colour (0x06ff3333));
    for (int x = 0; x < getWidth();  x += 40) g.drawVerticalLine  (x, 0.0f, (float) getHeight());
    for (int y = 0; y < getHeight(); y += 40) g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    // Corner brackets
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xaaff3333));
    g.drawLine (bounds.getX(),          bounds.getY(),           bounds.getX() + 20,    bounds.getY(),          1.5f);
    g.drawLine (bounds.getX(),          bounds.getY(),           bounds.getX(),          bounds.getY() + 20,     1.5f);
    g.drawLine (bounds.getRight() - 20, bounds.getY(),           bounds.getRight(),      bounds.getY(),          1.5f);
    g.drawLine (bounds.getRight(),      bounds.getY(),           bounds.getRight(),      bounds.getY() + 20,     1.5f);
    g.drawLine (bounds.getX(),          bounds.getBottom(),      bounds.getX() + 20,    bounds.getBottom(),     1.5f);
    g.drawLine (bounds.getX(),          bounds.getBottom() - 20, bounds.getX(),          bounds.getBottom(),     1.5f);
    g.drawLine (bounds.getRight() - 20, bounds.getBottom(),      bounds.getRight(),      bounds.getBottom(),     1.5f);
    g.drawLine (bounds.getRight(),      bounds.getBottom() - 20, bounds.getRight(),      bounds.getBottom(),     1.5f);

    // Title bar
    g.setColour (FS_ACCENT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.drawText  ("// FLESHSYNTH", 28, 8, 300, 24, juce::Justification::centredLeft);

    // Top glow line
    juce::ColourGradient topGrad (juce::Colours::transparentBlack, bounds.getX(), bounds.getY(),
                                   juce::Colours::transparentBlack, bounds.getRight(), bounds.getY(), false);
    topGrad.addColour (0.2, juce::Colour (0x55ff3333));
    topGrad.addColour (0.5, juce::Colour (0xffff3333));
    topGrad.addColour (0.8, juce::Colour (0x55ff3333));
    g.setGradientFill (topGrad);
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 1.5f);

    // Camera region border
    if (! cameraRect.isEmpty())
    {
        g.setColour (FS_BORDER_DIM);
        g.drawRect  (cameraRect.toFloat(), 1.0f);
 
        // Overlay drawing disabled — Python draws joints directly onto
        // the camera frame. drawOverlay() still exists for its data logic.
        // drawOverlay (g);
    }

    // Wave panel
    if (! wavePanelRect.isEmpty())
        drawWavePanel (g, wavePanelRect);

    // Sidebar
    if (! sidebarRect.isEmpty())
        drawSidebar (g, sidebarRect);

    // Status bar
    g.setColour (juce::Colour (0x44ff3333));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("SYS:ONLINE   BUILD:v0.1.0-alpha",
                 28, getHeight() - 22, getWidth() - 56, 14,
                 juce::Justification::centredLeft);
}

// ============================================================
//  Overlay: joints + ear indicators + wave polyline
// ============================================================
void FleshSynthPage::drawOverlay (juce::Graphics& g)
{
    if (! hasJoints) return;

    auto ox = cameraRect.getX();
    auto oy = cameraRect.getY();

    // Ear line + indicators
    auto& le = earPx[0]; auto& re = earPx[1];
    g.setColour (juce::Colour (0xffb4640a));
    g.drawLine ((float)(ox + le.x), (float)(oy + le.y),
                (float)(ox + re.x), (float)(oy + re.y), 2.0f);

    // Controlling ear highlight
    bool leftLower  = (le.y > re.y);
    bool leftActive = tiltInvert ? !leftLower : leftLower;
    auto drawEar = [&](JointPixel ep, bool active)
    {
        float cx = (float)(ox + ep.x), cy = (float)(oy + ep.y);
        if (active)
        {
            g.setColour (juce::Colour (0xffff9600));
            g.fillEllipse (cx - 12.0f, cy - 12.0f, 24.0f, 24.0f);
            g.setColour   (juce::Colours::black);
            g.drawEllipse (cx - 12.0f, cy - 12.0f, 24.0f, 24.0f, 3.0f);
        }
        else
        {
            g.setColour (juce::Colour (0xffa0783c));
            g.fillEllipse (cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        }
    };
    drawEar (le, leftActive);
    drawEar (re, !leftActive);

    // Body joint polyline
    juce::Path jp;
    for (int i = 0; i < 6; ++i)
    {
        float px = (float)(ox + jointPx[i].x);
        float py = (float)(oy + jointPx[i].y);
        if (i == 0) jp.startNewSubPath (px, py);
        else        jp.lineTo          (px, py);

        g.setColour (FS_WAVE_COL);
        g.fillEllipse (px - 8.0f, py - 8.0f, 16.0f, 16.0f);
    }
    g.setColour (FS_WAVE_COL);
    g.strokePath (jp, juce::PathStrokeType (2.0f));

    // Wave polyline inside camera view
    if (! currentWave.empty())
    {
        float left_x  = (float) cameraRect.getX() + cameraRect.getWidth() * 0.05f;
        float right_x = (float) cameraRect.getX() + cameraRect.getWidth() * 0.95f;
        float width   = right_x - left_x;
        float centerY = (float) cameraRect.getCentreY();
        float ampPx   = cameraRect.getHeight() * 0.4f * 0.5f;

        juce::Path wp;
        for (int i = 0; i < (int) currentWave.size(); ++i)
        {
            float x = left_x + (float) i / (float)(currentWave.size() - 1) * width;
            float y = centerY - currentWave[i] * ampPx;
            if (i == 0) wp.startNewSubPath (x, y);
            else        wp.lineTo          (x, y);
        }
        g.setColour (FS_WAVE_COL);
        g.strokePath (wp, juce::PathStrokeType (3.0f));
    }
}

// ============================================================
//  Wave panel — oscilloscope / waveform display
// ============================================================
void FleshSynthPage::drawWavePanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (juce::Colour (0xff000000));
    g.fillRect  (bounds);

    g.setColour (juce::Colour (0x22ff3333));
    for (int x = bounds.getX(); x < bounds.getRight();  x += 40) g.drawVerticalLine  (x, (float) bounds.getY(), (float) bounds.getBottom());
    for (int y = bounds.getY(); y < bounds.getBottom(); y += 40) g.drawHorizontalLine (y, (float) bounds.getX(), (float) bounds.getRight());

    g.setColour (FS_BORDER_DIM);
    g.drawRect  (bounds.toFloat(), 1.0f);

    g.setColour (FS_ACCENT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("WAVEFORM", bounds.getX() + 6, bounds.getY() + 4, 80, 12, juce::Justification::centredLeft);

    if (currentWave.empty()) return;

    float cx  = (float) bounds.getX();
    float cy  = (float) bounds.getCentreY();
    float amp = (float) bounds.getHeight() * 0.42f;
    float ww  = (float) bounds.getWidth();

    juce::Path p;
    for (int i = 0; i < (int) currentWave.size(); ++i)
    {
        float x = cx + (float) i / (float)(currentWave.size() - 1) * ww;
        float y = cy - currentWave[i] * amp;
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    // Glow pass
    g.setColour (FS_WAVE_COL.withAlpha (0.2f));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    // Main line
    g.setColour (FS_WAVE_COL);
    g.strokePath (p, juce::PathStrokeType (2.0f));
}

// ============================================================
//  Sidebar — scope + sliders
// ============================================================
void FleshSynthPage::drawSidebar (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (juce::Colour (0xff141414));
    g.fillRect  (bounds);
    g.setColour (FS_BORDER_DIM);
    g.drawRect  (bounds.toFloat(), 1.0f);

    int sh     = bounds.getHeight();
    int sw     = bounds.getWidth();
    int scopeH = sh / 2;

    // ---- Scope (top half) ----
    auto scopeBounds = juce::Rectangle<int> (bounds.getX(), bounds.getY(), sw, scopeH);

    g.setColour (juce::Colour (0xff000000));
    g.fillRect  (scopeBounds);
    g.setColour (juce::Colour (0x22ff3333));
    for (int x = scopeBounds.getX(); x < scopeBounds.getRight();  x += 40) g.drawVerticalLine  (x, (float) scopeBounds.getY(), (float) scopeBounds.getBottom());
    for (int y = scopeBounds.getY(); y < scopeBounds.getBottom(); y += 40) g.drawHorizontalLine (y, (float) scopeBounds.getX(), (float) scopeBounds.getRight());

    g.setColour (FS_ACCENT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("SCOPE", scopeBounds.getX() + 6, scopeBounds.getY() + 4, 60, 12, juce::Justification::centredLeft);

    if (! currentWave.empty())
    {
        float cx  = (float) scopeBounds.getX() + 10.0f;
        float cy  = (float) scopeBounds.getCentreY();
        float amp = (float)(scopeH / 2 - 10);
        float ww  = (float)(sw - 20);

        float maxA = 1e-6f;
        for (float s : currentWave) maxA = std::max (maxA, std::abs (s));

        juce::Path sp;
        for (int i = 0; i < (int) currentWave.size(); ++i)
        {
            float x = cx + (float) i / (float)(currentWave.size() - 1) * ww;
            float y = cy - (currentWave[i] / maxA) * amp;
            if (i == 0) sp.startNewSubPath (x, y);
            else        sp.lineTo          (x, y);
        }
        g.setColour (juce::Colour (0xff00ff64));
        g.strokePath (sp, juce::PathStrokeType (2.0f));
    }

    // ---- Sliders (bottom half) ----
    int sliderAreaY = bounds.getY() + scopeH;
    int sliderAreaH = sh - scopeH;
    int margin      = 20;

    int freqY = sliderAreaY + sliderAreaH / 3;
    int tiltY = sliderAreaY + 2 * sliderAreaH / 3;

    freqTrackRect = { bounds.getX() + margin, freqY - 15, sw - margin * 2, 30 };
    tiltTrackRect = { bounds.getX() + margin, tiltY - 15, sw - margin * 2, 30 };

    drawSlider (g, "FREQ", freqTrackRect, freqSliderNorm, FS_WAVE_COL, oscFreq, " Hz");

    float maxSt = tiltSliderNorm * TILT_MAX_RANGE_ST;
    drawSlider (g, "TILT RANGE", tiltTrackRect, tiltSliderNorm,
                juce::Colour (0xff00c8ff), maxSt, " st");

    // Invert toggle
    int tgX = bounds.getX() + margin;
    int tgY = tiltY + 22;
    toggleRect = { tgX, tgY, 130, 34 };

    g.setColour (tiltInvert ? juce::Colour (0xff008cff) : juce::Colour (0xff00508c));
    g.fillRect  (toggleRect);
    g.setColour (FS_BORDER_DIM);
    g.drawRect  (toggleRect.toFloat(), 1.0f);

    g.setColour (FS_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText  (tiltInvert ? "INVERT: ON" : "INVERT: OFF",
                 toggleRect, juce::Justification::centred);

    // Debug readout
    g.setColour (FS_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (juce::String::formatted ("pf: %.3f  osc: %.1f Hz", pitchFactor, oscFreq),
                 bounds.getX() + margin, tgY + 40, sw - margin * 2, 14,
                 juce::Justification::centredLeft);
}

void FleshSynthPage::drawSlider (juce::Graphics& g,
                                  const juce::String& label,
                                  juce::Rectangle<int> track,
                                  float normVal,
                                  juce::Colour colour,
                                  float displayVal,
                                  const juce::String& unit)
{
    int tx = track.getX(), ty = track.getCentreY();
    int t1 = track.getRight();

    g.setColour (FS_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText  (label + ": " + juce::String (displayVal, 1) + unit,
                 tx, ty - 26, track.getWidth(), 16,
                 juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff505050));
    g.drawLine  ((float) tx, (float) ty, (float) t1, (float) ty, 3.0f);

    int kx = tx + (int)(normVal * (float)(t1 - tx));
    g.setColour (colour);
    g.fillEllipse ((float)(kx - 10), (float)(ty - 10), 20.0f, 20.0f);
    g.setColour (FS_TEXT_PRI);
    g.fillEllipse ((float)(kx - 4),  (float)(ty - 4),  8.0f,  8.0f);
}

// ============================================================
//  resized
// ============================================================
void FleshSynthPage::resized()
{
    int w = getWidth(), h = getHeight();

    backButton.setBounds (w - 100, 8, 80, 26);

    int headerH = 40;
    int statusH = 28;
    int sideW   = 240;
    int waveH   = 120;

    int camX = 0, camY = headerH;
    int camW = w - sideW;
    int camH = h - headerH - statusH - waveH;

    cameraRect    = { camX,        camY,       camW,  camH  };
    wavePanelRect = { camX,        camY + camH, camW, waveH };
    sidebarRect   = { camX + camW, camY,        sideW, h - headerH - statusH };

    cameraView.setBounds (cameraRect);
}

// ============================================================
//  Mouse — slider dragging + toggle
// ============================================================
void FleshSynthPage::mouseDown (const juce::MouseEvent& e)
{
    auto pt = e.getPosition();

    if      (freqTrackRect.contains (pt)) { dragging = DragTarget::Freq; }
    else if (tiltTrackRect.contains (pt)) { dragging = DragTarget::Tilt; }
    else if (toggleRect.contains    (pt)) { tiltInvert = !tiltInvert; repaint(); }
}

void FleshSynthPage::mouseDrag (const juce::MouseEvent& e)
{
    auto updateNorm = [](int x, juce::Rectangle<int> track) -> float
    {
        float t = (float)(x - track.getX()) / (float) track.getWidth();
        return juce::jlimit (0.0f, 1.0f, t);
    };

    if      (dragging == DragTarget::Freq) freqSliderNorm = updateNorm (e.getPosition().x, freqTrackRect);
    else if (dragging == DragTarget::Tilt) tiltSliderNorm = updateNorm (e.getPosition().x, tiltTrackRect);
}

void FleshSynthPage::mouseUp (const juce::MouseEvent&)
{
    dragging = DragTarget::None;
}