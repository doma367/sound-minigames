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

    // cameraView is a child component — we need to draw the trail and
    // overlay ABOVE it, so we place a transparent overlay component on
    // top.  Simplest approach: just set cameraView to not intercept
    // painting order, and call repaint() on the parent.  The trail /
    // overlay are drawn in the parent's paint() AFTER cameraView has
    // already composited itself — but because cameraView is a child,
    // JUCE paints children on top of the parent.  To get our drawing
    // on top we make cameraView paint into its own layer and we draw
    // our overlays by calling drawTrail / drawOverlay in a component
    // that sits above cameraView in the Z-order.  The cleanest fix
    // without restructuring the component tree: promote cameraView to
    // be NOT a visible JUCE child and instead call cameraView.paint()
    // manually from our paint() — but that breaks mouse/resize.
    //
    // Chosen fix: keep cameraView as child, but add an invisible
    // overlay panel (overlayView) that sits on top and forwards all
    // trail/overlay drawing to us via a lambda.
    addAndMakeVisible (cameraView);
    addAndMakeVisible (overlayView);   // sits above cameraView in Z-order
    overlayView.setInterceptsMouseClicks (false, false);
    overlayView.onPaint = [this](juce::Graphics& g)
    {
        drawTrail    (g);
        drawOverlay  (g);
        drawParticles(g);
    };

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
    PoseFrame pf;
    for (int i = 0; i < kNumLandmarks; ++i)
    {
        pf.lm[i].x = oscLandmarks[i * 2    ].load (std::memory_order_relaxed);
        pf.lm[i].y = oscLandmarks[i * 2 + 1].load (std::memory_order_relaxed);
    }
    pf.valid = (pf.lm[0].x > 0.01f && pf.lm[0].x < 0.99f);

    int cw = cameraRect.getWidth();
    int ch = cameraRect.getHeight();
    if (cw <= 0 || ch <= 0) { repaint(); return; }

    static const int ORDER[6] = {
        PoseFrame::RIGHT_WRIST, PoseFrame::RIGHT_ELBOW, PoseFrame::RIGHT_SHOULDER,
        PoseFrame::LEFT_SHOULDER, PoseFrame::LEFT_ELBOW, PoseFrame::LEFT_WRIST
    };

    if (pf.valid)
    {
        // 1. Smooth all joints for the visual skeleton
        float rawX[6], rawY[6], sX[6], sY[6];
        for (int i = 0; i < 6; ++i)
        {
            rawX[i] = pf.lm[ORDER[i]].x * (float) cw;
            rawY[i] = pf.lm[ORDER[i]].y * (float) ch;
        }
        smoothJoints (rawX, rawY, sX, sY);

        // 2. Count joints actually inside the frame boundaries
        int nodesInFrame = 0;
        for (int i = 0; i < 6; ++i)
        {
            jointPx[i].x = (int) sX[i];
            jointPx[i].y = (int) sY[i];
            if (pf.lm[ORDER[i]].x >= 0.0f && pf.lm[ORDER[i]].x <= 1.0f &&
                pf.lm[ORDER[i]].y >= 0.0f && pf.lm[ORDER[i]].y <= 1.0f) {
                nodesInFrame++;
            }
        }
        hasJoints = true;

        earPx[0] = { (int)(pf.lm[PoseFrame::LEFT_EAR ].x * cw), (int)(pf.lm[PoseFrame::LEFT_EAR ].y * ch) };
        earPx[1] = { (int)(pf.lm[PoseFrame::RIGHT_EAR].x * cw), (int)(pf.lm[PoseFrame::RIGHT_EAR].y * ch) };

        // 3. Audio logic: 3+ nodes = play, <3 nodes = stop
        if (nodesInFrame >= 3)
        {
            buildWaveFromPose (pf, cw, ch);
            
            // Push frequency + waveform snapshot to audio state
            const juce::ScopedLock sl (audio.lock);
            audio.oscFreq = oscFreq;
            audio.snapBuf = currentWave; // Refill the buffer to resume sound
        }
        else
        {
            currentWave.assign (512, 0.0f);
            const juce::ScopedLock sl (audio.lock);
            audio.snapBuf.clear(); // Signal silence to audio thread
        }

        // ---- Tilt -> pitch ----
        float headTilt = pf.lm[PoseFrame::RIGHT_EAR].y - pf.lm[PoseFrame::LEFT_EAR].y;
        tiltRawSmooth = (1.0f - TILT_SMOOTH_ALPHA) * tiltRawSmooth + TILT_SMOOTH_ALPHA * headTilt;
        float tilt = tiltRawSmooth;
        if (std::abs (tilt) < TILT_DEADZONE) tilt = 0.0f;
        float tiltNorm = juce::jlimit (-1.0f, 1.0f, tilt / MAX_TILT);
        float tiltShaped = tiltNorm * 0.55f + (tiltNorm * tiltNorm * tiltNorm) * 0.45f;
        if (tiltInvert) tiltShaped = -tiltShaped;

        float maxSemitones = tiltSliderNorm * TILT_MAX_RANGE_ST;
        pitchFactor = (1.0f - PITCH_ALPHA_TRACK) * pitchFactor + PITCH_ALPHA_TRACK * std::pow (2.0f, (tiltShaped * maxSemitones) / 12.0f);
    }
    else
    {
        hasJoints = false;
        pitchFactor = (1.0f - PITCH_ALPHA_RELAX) * pitchFactor + PITCH_ALPHA_RELAX * 1.0f;
        
        // Ensure silence if no pose at all is valid
        const juce::ScopedLock sl (audio.lock);
        audio.snapBuf.clear();
    }

    pitchFactor = juce::jlimit (0.2f, 5.0f, pitchFactor);
    baseFreq = SLIDER_MIN_FREQ + freqSliderNorm * (SLIDER_MAX_FREQ - SLIDER_MIN_FREQ);
    baseFreqSmooth = (1.0f - BASE_FREQ_SMOOTH_A) * baseFreqSmooth + BASE_FREQ_SMOOTH_A * baseFreq;
    oscFreq = baseFreqSmooth * pitchFactor;

    // (Note: Removed the 'if (!audio.snapBuf.empty())' block that was here)

    if (hasJoints) pushTrailFrame();
    tickParticles();
    spawnParticlesFromWave();
    updateSpectrum();
    repaint();
}

// ============================================================
//  Build normalised wave from the six body joints
// ============================================================
void FleshSynthPage::buildWaveFromPose (const PoseFrame& pf, int viewW, int viewH)
{
    static const int ORDER[6] = {
        PoseFrame::RIGHT_WRIST, PoseFrame::RIGHT_ELBOW, PoseFrame::RIGHT_SHOULDER,
        PoseFrame::LEFT_SHOULDER, PoseFrame::LEFT_ELBOW, PoseFrame::LEFT_WRIST
    };

    std::vector<float> xs, ys;
    for (int i = 0; i < 6; ++i)
    {
        // Only include nodes inside frame bounds
        if (pf.lm[ORDER[i]].x >= 0.0f && pf.lm[ORDER[i]].x <= 1.0f &&
            pf.lm[ORDER[i]].y >= 0.0f && pf.lm[ORDER[i]].y <= 1.0f)
        {
            xs.push_back (pf.lm[ORDER[i]].x * (float) viewW);
            ys.push_back ((0.5f - pf.lm[ORDER[i]].y) * 2.0f);
        }
    }

    if (xs.size() < 3) return;

    // Sort by x to interpolate left → right
    std::vector<int> idx (xs.size());
    std::iota (idx.begin(), idx.end(), 0);
    std::sort (idx.begin(), idx.end(), [&](int a, int b){ return xs[a] < xs[b]; });

    std::vector<float> sxs, sys;
    for (int i : idx) { sxs.push_back (xs[i]); sys.push_back (ys[i]); }

    float x0 = sxs.front(), xSpan = std::max (sxs.back() - x0, 1.0f);
    const int N = 512;
    std::vector<float> rawWave (N);

    for (int i = 0; i < N; ++i)
    {
        float wx = x0 + ((float) i / (float)(N - 1)) * xSpan;
        float val = sys.front();
        for (size_t j = 0; j < sxs.size() - 1; ++j)
        {
            if (wx >= sxs[j] && wx <= sxs[j+1])
            {
                float lt = (wx - sxs[j]) / (sxs[j+1] - sxs[j]);
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
    for (float& s : rawWave) s = juce::jlimit (-1.0f, 1.0f, s / (rms * 3.0f));

    for (int i = 0; i < N; ++i)
        prevWaveNorm[i] = (1.0f - WAVE_SMOOTH_ALPHA) * prevWaveNorm[i] + WAVE_SMOOTH_ALPHA * rawWave[i];

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
    {
        const juce::ScopedLock sl (audio.lock);
        if (audio.snapBuf.size() == audio.playBuf.size() && ! audio.snapBuf.empty())
        {
            for (size_t i = 0; i < audio.playBuf.size(); ++i)
                audio.playBuf[i] = (1.0f - audio.morphAlpha) * audio.playBuf[i]
                                 +  audio.morphAlpha * audio.snapBuf[i];
        }
        else // Silence if snapBuf is empty (fewer than 3 nodes)
        {
            juce::FloatVectorOperations::clear (audio.playBuf.data(), (int)audio.playBuf.size());
        }
    }

    auto& wave = audio.playBuf;
    int   n    = (int) wave.size();

    // Check if the current buffer is effectively silent
    bool isSilent = true;
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (std::abs(wave[i]) > 0.0001f) { isSilent = false; break; }
        }
    }

    if (n == 0 || isSilent)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    double phaseInc = (audio.oscFreq * n) / audio.sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        int    idx  = (int) audio.phase % n;
        double frac = audio.phase - (int) audio.phase;
        float s = wave[idx] * (float)(1.0 - frac) + wave[(idx + 1) % n] * (float) frac;

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
        // NOTE: trail + overlay are drawn by overlayView (child above cameraView)
    }

    // Spectrum panel (replaces waveform)
    if (! wavePanelRect.isEmpty())
        drawSpectrumPanel (g, wavePanelRect);

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
//  NOTE: coordinates here are in overlayView-local space, which
//        is identical to parent space since overlayView covers
//        the whole component.
// ============================================================
void FleshSynthPage::drawOverlay (juce::Graphics& g)
{
    if (! hasJoints) return;

    g.saveState();
    g.reduceClipRegion (cameraRect);

    auto ox = (float)cameraRect.getX();
    auto oy = (float)cameraRect.getY();

    // Count nodes in frame for the error display
    int nodesInFrame = 0;
    static const int ORDER[6] = { 16, 14, 12, 11, 13, 15 }; // Wrist to Wrist
    for (int i = 0; i < 6; ++i) {
        if (jointPx[i].x >= 0 && jointPx[i].x <= cameraRect.getWidth() &&
            jointPx[i].y >= 0 && jointPx[i].y <= cameraRect.getHeight()) nodesInFrame++;
    }

    // --- Draw Red Skeleton ---
    g.setColour (FS_ACCENT);
    g.drawLine (ox + earPx[0].x, oy + earPx[0].y, ox + earPx[1].x, oy + earPx[1].y, 2.0f);

    auto drawEar = [&](JointPixel ep, bool active) {
        float cx = ox + ep.x, cy = oy + ep.y;
        g.setColour (active ? FS_ACCENT : FS_ACCENT.withAlpha(0.4f));
        g.fillEllipse (cx - (active ? 12.0f : 10.0f), cy - (active ? 12.0f : 10.0f), (active ? 24.0f : 20.0f), (active ? 24.0f : 20.0f));
    };
    bool leftLower = (earPx[0].y > earPx[1].y);
    drawEar (earPx[0], tiltInvert ? !leftLower : leftLower);
    drawEar (earPx[1], tiltInvert ? leftLower : !leftLower);

    juce::Path jp;
    for (int i = 0; i < 6; ++i) {
        float px = ox + jointPx[i].x, py = oy + jointPx[i].y;
        if (i == 0) jp.startNewSubPath (px, py); else jp.lineTo (px, py);
        g.setColour (FS_ACCENT);
        g.fillEllipse (px - 8.0f, py - 8.0f, 16.0f, 16.0f);
    }
    g.strokePath (jp, juce::PathStrokeType (2.5f));

    // --- ERROR MESSAGE ---
    if (nodesInFrame < 3)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (cameraRect);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (22.0f, juce::Font::bold));
        g.drawText ("ERROR :: only " + juce::String(nodesInFrame) + " nodes detected", 
                    cameraRect, juce::Justification::centred);
    }

    g.restoreState();
}

// ============================================================
//  Ghost trail — ring buffer of skeleton frames drawn on camera
// ============================================================
void FleshSynthPage::pushTrailFrame()
{
    auto& f = trailBuf[trailHead];
    for (int i = 0; i < 6; ++i) f.joints[i] = jointPx[i];
    f.ears[0] = earPx[0];
    f.ears[1] = earPx[1];
    f.valid   = true;
    trailHead = (trailHead + 1) % kTrailLen;
}

void FleshSynthPage::drawTrail (juce::Graphics& g)
{
    if (cameraRect.isEmpty()) return;

    // Save clip and restrict to camera bounds
    g.saveState();
    g.reduceClipRegion (cameraRect);

    int ox = cameraRect.getX();
    int oy = cameraRect.getY();

    // Draw oldest → newest so newer frames paint on top
    for (int age = kTrailLen - 1; age >= 1; --age)
    {
        int idx = (trailHead - 1 - age + kTrailLen * 2) % kTrailLen;
        auto& f = trailBuf[idx];
        if (! f.valid) continue;

        // age=1 is freshest ghost, age=kTrailLen-1 is oldest
        float t     = 1.0f - (float)(age) / (float) kTrailLen;  // 0=old, 1=fresh
        float alpha = t * t * 0.55f;   // quad ease, max ~0.55 on freshest ghost

        juce::Colour lineCol  = FS_ACCENT.withAlpha (alpha * 0.7f);
        juce::Colour dotCol   = FS_ACCENT.withAlpha (alpha);
        float        dotR     = 4.0f + t * 4.0f;   // 4..8 px radius

        // Polyline through the 6 joints
        juce::Path jp;
        for (int i = 0; i < 6; ++i)
        {
            float px = (float)(ox + f.joints[i].x);
            float py = (float)(oy + f.joints[i].y);
            if (i == 0) jp.startNewSubPath (px, py);
            else        jp.lineTo          (px, py);
        }
        g.setColour (lineCol);
        g.strokePath (jp, juce::PathStrokeType (1.0f + t * 1.5f));

        // Joint dots
        g.setColour (dotCol);
        for (int i = 0; i < 6; ++i)
        {
            float px = (float)(ox + f.joints[i].x);
            float py = (float)(oy + f.joints[i].y);
            g.fillEllipse (px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
        }

        // Ear dots (smaller)
        float earR = 2.5f + t * 3.0f;
        for (int i = 0; i < 2; ++i)
        {
            float px = (float)(ox + f.ears[i].x);
            float py = (float)(oy + f.ears[i].y);
            g.fillEllipse (px - earR, py - earR, earR * 2.0f, earR * 2.0f);
        }
    }

    g.restoreState();
}

// ============================================================
//  Particle system
//  Particles live in PIXEL space relative to the full component,
//  spawning from the wave contour drawn inside cameraRect.
// ============================================================
void FleshSynthPage::tickParticles()
{
    constexpr float dt    = 1.0f / 40.0f;
    constexpr float grav  = 0.18f;
    constexpr float drag  = 0.96f;

    for (auto& p : particles)
    {
        p.x    += p.vx * dt;
        p.y    += p.vy * dt;
        p.vy   += grav * dt;
        p.vx   *= drag;
        p.life -= dt * 0.9f;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(),
                     [](const Particle& p){ return p.life <= 0.0f; }),
                     particles.end());
}

void FleshSynthPage::spawnParticlesFromWave()
{
    // Use the top half of the existing sidebarRect as the scope area
    auto scope = sidebarRect.withHeight (sidebarRect.getHeight() / 2);

    if (currentWave.empty() || scope.isEmpty()) return;

    // Calculate RMS for intensity
    float rms = 0.0f;
    for (float s : currentWave) rms += s * s;
    rms = std::sqrt (rms / (float) currentWave.size());
    if (rms < 0.05f) return;

    // Scope geometry constants
    const float left_x  = (float) scope.getX() + 10.0f;
    const float right_x = (float) scope.getRight() - 10.0f;
    const float waveW   = right_x - left_x;
    const float centerY = (float) scope.getCentreY();
    const float ampPx   = (float)(scope.getHeight() / 2 - 10);

    float maxA = 1e-6f;
    for (float s : currentWave) maxA = std::max (maxA, std::abs (s));

    int toSpawn = juce::jlimit (0, 6, (int)(rms * 16.0f));

    for (int i = 0; i < toSpawn; ++i)
    {
        int   si   = (int)(rng.nextFloat() * (float)(currentWave.size() - 1));
        float norm = (float) si / (float)(currentWave.size() - 1); 

        // Position particles directly on the scope's waveform contour
        float px = left_x  + norm * waveW;
        float py = centerY - (currentWave[si] / maxA) * ampPx; 

        Particle p;
        p.x    = px;
        p.y    = py;
        p.vx   = (rng.nextFloat() - 0.5f) * 2.0f;
        p.vy   = -(rng.nextFloat() * 2.0f);
        p.life = rng.nextFloat() * 0.6f + 0.4f;
        p.size = rng.nextFloat() * 1.2f + 0.8f; // Made smaller/subtler
        particles.push_back (p);
    }
}

// ============================================================
//  Spectrum update — DFT against wavetable harmonics
//  The wavetable is 512 samples representing one full cycle, so
//  harmonic k sits at bin k/N cycles-per-sample — independent of
//  playback frequency.  Decay alpha raised for snappier response.
// ============================================================
void FleshSynthPage::updateSpectrum()
{
    if (currentWave.empty()) return;

    const int N = (int) currentWave.size();

    for (int bar = 0; bar < kNumBars; ++bar)
    {
        // Project onto harmonic (bar+1) of the wavetable cycle
        float omega = 2.0f * juce::MathConstants<float>::pi
                    * (float)(bar + 1) / (float) N;

        float re = 0.0f, im = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            re += currentWave[i] * std::cos (omega * (float) i);
            im += currentWave[i] * std::sin (omega * (float) i);
        }

        float mag    = std::sqrt (re * re + im * im) / (float) N * 2.0f;
        float target = juce::jlimit (0.0f, 1.0f, mag * 2.5f);

        // Fast attack, faster decay (was 0.12 — now 0.28 for visible response)
        float alpha  = (target > spectrumBars[bar]) ? 0.55f : 0.28f;
        spectrumBars[bar] = (1.0f - alpha) * spectrumBars[bar] + alpha * target;
    }
}

// ============================================================
//  Spectrum panel — harmonic bars + particles
//  Particles are now in pixel space (spawned from cameraRect wave),
//  so we draw them directly without normalised→pixel conversion.
// ============================================================
void FleshSynthPage::drawSpectrumPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (juce::Colour (0xff000000));
    g.fillRect  (bounds);

    g.setColour (juce::Colour (0x18ff3333));
    for (int x = bounds.getX(); x < bounds.getRight();  x += 40) g.drawVerticalLine  (x, (float)bounds.getY(), (float)bounds.getBottom());
    for (int y = bounds.getY(); y < bounds.getBottom(); y += 40) g.drawHorizontalLine (y, (float)bounds.getX(), (float)bounds.getRight());

    g.setColour (FS_BORDER_DIM);
    g.drawRect  (bounds.toFloat(), 1.0f);

    g.setColour (FS_ACCENT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("HARMONICS", bounds.getX() + 6, bounds.getY() + 4, 80, 12, juce::Justification::centredLeft);

    // Note name
    auto freqToNote = [](float f) -> juce::String
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int midi = (int) std::round (69.0f + 12.0f * std::log2 (f / 440.0f));
        if (midi < 0 || midi > 127) return "---";
        return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
    };

    g.setColour (FS_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (juce::String (oscFreq, 1) + " Hz  " + freqToNote (oscFreq),
                 bounds.getRight() - 110, bounds.getY() + 4, 104, 12,
                 juce::Justification::centredRight);

    // Bars
    const int   margin = 10;
    const float barW   = (float)(bounds.getWidth() - margin * 2) / (float) kNumBars;
    const float maxH   = (float)(bounds.getHeight() - 20);
    const float baseY  = (float)(bounds.getBottom() - 4);

    for (int i = 0; i < kNumBars; ++i)
    {
        float h   = juce::jmax (1.0f, spectrumBars[i] * maxH);
        float bx  = (float)(bounds.getX() + margin) + (float)i * barW + barW * 0.1f;
        float bw  = barW * 0.78f;
        float norm = spectrumBars[i];

        // Bar body
        g.setColour (FS_ACCENT.withAlpha (0.18f + norm * 0.65f));
        g.fillRect  (bx, baseY - h, bw, h);

        // Bright cap
        g.setColour (FS_ACCENT.withAlpha (0.55f + norm * 0.45f));
        g.fillRect  (bx, baseY - h, bw, 2.0f);
    }
}

// ============================================================
//  Particles drawn in the overlay (above camera view) so they
//  appear to rise off the wave that's drawn over the camera feed.
//  Called from overlayView's onPaint lambda — coordinates are
//  already in full-component pixel space.
// ============================================================
void FleshSynthPage::drawParticles (juce::Graphics& g)
{
    g.saveState();
    
    // Ensure particles only draw inside the top-right scope
    auto scope = sidebarRect.withHeight (sidebarRect.getHeight() / 2);
    g.reduceClipRegion (scope);

    for (auto& p : particles)
    {
        float alpha = juce::jlimit (0.0f, 1.0f, p.life);

        // Subtler "Flesh" accent color for the particles
        g.setColour (FS_ACCENT.withAlpha (alpha * 0.4f));
        g.fillEllipse (p.x - p.size, p.y - p.size, p.size * 2.0f, p.size * 2.0f);

        // Very faint outer glow
        g.setColour (juce::Colours::white.withAlpha (alpha * 0.1f));
        g.fillEllipse (p.x - p.size * 1.5f, p.y - p.size * 1.5f, p.size * 3.0f, p.size * 3.0f);
    }

    g.restoreState();
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
        g.setColour (FS_ACCENT.withAlpha (0.18f));
        g.strokePath (sp, juce::PathStrokeType (3.5f));
        g.setColour (FS_ACCENT);
        g.strokePath (sp, juce::PathStrokeType (1.5f));
    }

    // ---- Sliders (bottom half) ----
    int sliderAreaY = bounds.getY() + scopeH;
    int sliderAreaH = sh - scopeH;
    int margin      = 20;

    int freqY = sliderAreaY + sliderAreaH / 3;
    int tiltY = sliderAreaY + 2 * sliderAreaH / 3;

    freqTrackRect = { bounds.getX() + margin, freqY - 15, sw - margin * 2, 30 };
    tiltTrackRect = { bounds.getX() + margin, tiltY - 15, sw - margin * 2, 30 };

    drawSlider (g, "FREQ", freqTrackRect, freqSliderNorm, FS_ACCENT, oscFreq, " Hz");

    float maxSt = tiltSliderNorm * TILT_MAX_RANGE_ST;
    drawSlider (g, "TILT RANGE", tiltTrackRect, tiltSliderNorm,
                juce::Colour (0xffff6644), maxSt, " st");

    // ============================================================
    //  INVERT TOGGLE — UPDATED (smaller, centered, red, with thumb indicator)
    // ============================================================
    int toggleW = 110;
    int toggleH = 28;
    int tgX     = bounds.getX() + (sw - toggleW) / 2;   // perfectly centered
    int tgY     = tiltY + 35;

    toggleRect = { tgX, tgY, toggleW, toggleH };

    bool isOn = tiltInvert;

    // Dark pill track
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRoundedRectangle (toggleRect.toFloat(), toggleH * 0.5f);

    g.setColour (FS_BORDER_DIM);
    g.drawRoundedRectangle (toggleRect.toFloat(), toggleH * 0.5f, 1.5f);

    // Sliding thumb (red when ON)
    const float thumbMargin = 4.0f;
    const float thumbW      = toggleW * 0.46f;
    const float thumbH      = toggleH - thumbMargin * 2.0f;
    const float thumbX      = isOn ? (float)(tgX + toggleW - thumbW - thumbMargin)
                                   : (float)(tgX + thumbMargin);

    juce::Rectangle<float> thumbRect { thumbX, (float)tgY + thumbMargin, thumbW, thumbH };

    g.setColour (isOn ? FS_ACCENT : juce::Colour (0xff444444));
    g.fillRoundedRectangle (thumbRect, thumbH * 0.5f);

    // Nice highlight on thumb when ON
    if (isOn)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (thumbRect.reduced (2.0f), (thumbH - 4.0f) * 0.5f);
    }

    // Label above toggle
    g.setColour (FS_TEXT_DIM);
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText ("INVERT", toggleRect.getX(), toggleRect.getY() - 18,
                toggleRect.getWidth(), 14, juce::Justification::centred);

    // Debug readout (automatically placed below new toggle)
    g.setColour (FS_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (juce::String::formatted ("pf: %.3f  osc: %.1f Hz", pitchFactor, oscFreq),
                 bounds.getX() + margin, toggleRect.getBottom() + 8, sw - margin * 2, 14,
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

    cameraView.setBounds  (cameraRect);

    // overlayView covers the full component so trail/overlay/particle
    // coordinates stay in the same space as cameraRect
    overlayView.setBounds (getLocalBounds());
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