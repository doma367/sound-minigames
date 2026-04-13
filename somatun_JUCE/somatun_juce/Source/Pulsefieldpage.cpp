#include "Pulsefieldpage.h"
#include "MainComponent.h"

#include <numeric>
#include <algorithm>

// ============================================================
//  Colour palette  (SomatunLookAndFeel / red-on-dark theme)
// ============================================================
static const juce::Colour PF_BG_DARK     { 0xff030303 };
static const juce::Colour PF_BG_CARD     { 0xff0a0608 };
static const juce::Colour PF_BG_ROW_A    { 0xff110a0a };
static const juce::Colour PF_BG_ROW_B    { 0xff0c0707 };
static const juce::Colour PF_ACCENT      { 0xffff2222 };  // hot blood red
static const juce::Colour PF_ACCENT_DIM  { 0x44ff2222 };
static const juce::Colour PF_BORDER_DIM  { 0x2aff2222 };
static const juce::Colour PF_BORDER_HOT  { 0x99ff2222 };
static const juce::Colour PF_TEXT_PRI    { 0xffd8c8c8 };  // warm off-white, slightly red-tinted
static const juce::Colour PF_TEXT_DIM    { 0x55d8c8c8 };
static const juce::Colour PF_TEXT_ACC    { 0xffff4444 };

// Gesture-mapped colours — all in the red/crimson/burgundy/violet family
static const juce::Colour PF_COL_FILTER  { 0xffcc2244 };  // deep crimson / right hand filter
static const juce::Colour PF_COL_TIME    { 0xffaa1133 };  // darker blood red / delay time
static const juce::Colour PF_COL_FB      { 0xffff2222 };  // hot red / delay feedback
static const juce::Colour PF_COL_VOL     { 0xff882233 };  // muted burgundy / per-row volume
static const juce::Colour PF_COL_REVERB  { 0xff661133 };  // very dark wine / reverb
static const juce::Colour PF_COL_ACTIVE  { 0xffff2222 };  // active step — hot red
static const juce::Colour PF_COL_INACT   { 0xff1e0f0f };  // inactive step — near-black red tint
static const juce::Colour PF_COL_WAVE    { 0xffcc1122 };  // waveform — deep red
static const juce::Colour PF_COL_VEC     { 0xff991122 };  // vectorscope — darker red

// ============================================================
//  PFCameraView
// ============================================================
void PFCameraView::pushFrame (const juce::Image& img)
{
    { const juce::ScopedLock sl (lock); latest = img; hasFrame = true; }
    repaint();
}

void PFCameraView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    {
        const juce::ScopedLock sl (lock);
        if (latest.isValid())
            g.drawImage (latest, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }
    if (! hasFrame)
    {
        g.setColour (juce::Colour (0x0aff3333));
        for (int y = 0; y < getHeight(); y += 16)
            g.drawHorizontalLine (y, 0.0f, (float)getWidth());
        g.setColour (juce::Colour (0x66ff3333));
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText ("// CAMERA FEED LOADING...", 12, getHeight() - 24,
                    getWidth() - 24, 18, juce::Justification::centredLeft);
    }
}

// ============================================================
//  PFVideoReceiver
// ============================================================
PFVideoReceiver::PFVideoReceiver (PFCameraView& view)
    : juce::Thread ("PFVideoReceiver"), cameraView (view) {}

void PFVideoReceiver::startReceiver() { startThread(); }

void PFVideoReceiver::stopReceiver()
{
    signalThreadShouldExit();
    if (auto* s = streamSocket.load()) s->close();
    stopThread (2000);
}

bool PFVideoReceiver::readExact (juce::StreamingSocket& sock, void* buf, int numBytes)
{
    auto* p = static_cast<char*>(buf);
    int remaining = numBytes;
    while (remaining > 0)
    {
        int got = sock.read (p, remaining, true);
        if (got <= 0) return false;
        p += got; remaining -= got;
    }
    return true;
}

void PFVideoReceiver::run()
{
    constexpr int kPort          = 9001;
    constexpr int kRetryMs       = 1000;
    constexpr int kMaxFrameBytes = 4 * 1024 * 1024;

    while (! threadShouldExit())
    {
        auto sock = std::make_unique<juce::StreamingSocket>();
        streamSocket = sock.get();

        if (! sock->connect ("127.0.0.1", kPort, 2000))
        {
            streamSocket = nullptr;
            wait (kRetryMs);
            continue;
        }

        while (! threadShouldExit())
        {
            uint8_t hdr[4];
            if (! readExact (*sock, hdr, 4)) break;

            uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
                         | ((uint32_t)hdr[2] <<  8) |  (uint32_t)hdr[3];

            if (len == 0 || len > (uint32_t)kMaxFrameBytes) break;

            juce::MemoryBlock mb (len);
            if (! readExact (*sock, mb.getData(), (int)len)) break;

            juce::MemoryInputStream mis (mb, false);
            juce::JPEGImageFormat   fmt;
            juce::Image img = fmt.decodeImage (mis);

            if (img.isValid())
                juce::MessageManager::callAsync ([this, img]() { cameraView.pushFrame (img); });
        }

        streamSocket = nullptr;
        wait (kRetryMs);
    }
}

// ============================================================
//  PulseFieldPage — constructor / destructor
// ============================================================
PulseFieldPage::PulseFieldPage (MainComponent& mc)
    : mainComponent (mc)
{
    setLookAndFeel (&laf);

    // Default grid pattern (kick / snare / hat)
    std::memset (grid, 0, sizeof (grid));
    grid[0][0] = grid[0][4] = true;
    grid[1][2] = grid[1][6] = true;

    formatManager.registerBasicFormats();

    // Reverb slider defaults
    sliderSize.minV  = 0.5f; sliderSize.maxV = 3.0f; sliderSize.value = 1.0f;
    sliderWet .minV  = 0.0f; sliderWet .maxV = 1.0f; sliderWet .value = 0.0f;

    // Boot with 3 built-in voices (Kick / Snare / Hat)
    {
        const juce::ScopedLock sl (voiceLock);
        Voice kick;  kick.name  = "Kick";  kick.samples  = synthKick().getArrayOfReadPointers()[0] ? std::vector<float>() : std::vector<float>();
        Voice snare; snare.name = "Snare"; snare.samples = std::vector<float>();
        Voice hat;   hat.name   = "Hat";   hat.samples   = std::vector<float>();

        // Build samples inline
        auto buildKick = [&]() {
            const int N = int(44100 * 0.1);
            kick.samples.resize (N);
            for (int i = 0; i < N; ++i)
            {
                float t = (float)i / 44100.0f;
                kick.samples[i] = std::sin (2.0f * juce::MathConstants<float>::pi
                                            * 150.0f * std::pow (0.5f, t / 0.03f) * t)
                                 * std::exp (-t * 40.0f);
            }
        };
        auto buildSnare = [&]() {
            const int N = int(44100 * 0.1);
            snare.samples.resize (N);
            juce::Random rng;
            for (int i = 0; i < N; ++i)
            {
                float t = (float)i / 44100.0f;
                snare.samples[i] = (rng.nextFloat() * 0.4f - 0.2f)
                                  * std::exp (-t * 60.0f);
            }
        };
        auto buildHat = [&]() {
            const int N = int(44100 * 0.03);
            hat.samples.resize (N);
            juce::Random rng;
            for (int i = 0; i < N; ++i)
            {
                float t = (float)i / 44100.0f;
                hat.samples[i] = (rng.nextFloat() * 0.2f - 0.1f)
                                * std::exp (-t * 120.0f);
            }
        };

        buildKick(); buildSnare(); buildHat();
        voices.push_back (std::move (kick));
        voices.push_back (std::move (snare));
        voices.push_back (std::move (hat));
    }

    // Init volume sliders
    for (int i = 0; i < MAX_ROWS; ++i)
        volSliders[i].value = 0.8f;

    waveformSnap.assign (AudioEngine::WAVE_BUF, 0.0f);
    vecSnapL.assign (AudioEngine::VEC_BUF, 0.0f);
    vecSnapR.assign (AudioEngine::VEC_BUF, 0.0f);

    // Camera + overlay setup
    videoReceiver = std::make_unique<PFVideoReceiver> (cameraView);
    addAndMakeVisible (cameraView);
    addAndMakeVisible (overlayView);
    overlayView.setInterceptsMouseClicks (false, false);
    overlayView.onPaint = [this](juce::Graphics& g)
    {
        drawHandSkeleton (g);
        drawCamOverlays  (g);
    };

    backButton.onClick = [this]
    {
        stop();
        mainComponent.showLanding();
    };
    addAndMakeVisible (backButton);
}

PulseFieldPage::~PulseFieldPage()
{
    stop();
    setLookAndFeel (nullptr);
}

// ============================================================
//  start / stop
// ============================================================
void PulseFieldPage::start()
{
    videoReceiver = std::make_unique<PFVideoReceiver> (cameraView);
    videoReceiver->startReceiver();
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
    startTimerHz (40);

    // Register /hands OSC callback with the shared router
    mainComponent.setHandsCallback ([this](const juce::OSCMessage& m)
    {
        handleHandsMessage (m);
    });

    DBG ("[PulseField] started");
}

void PulseFieldPage::stop()
{
    mainComponent.clearOSCCallbacks();

    stopTimer();
    videoReceiver->stopReceiver();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    DBG ("[PulseField] stopped");
}

// ============================================================
//  OSC — /hands  (called on the message thread)
// ============================================================
void PulseFieldPage::handleHandsMessage (const juce::OSCMessage& m)
{
    const int n = juce::jmin (m.size(), (int)MAX_HANDS_FLOATS);
    for (int i = 0; i < n; ++i)
        if (m[i].isFloat32())
            oscHandsData[i].store (m[i].getFloat32(), std::memory_order_relaxed);
}

// ============================================================
//  Gesture extraction — mirrors vision.py HandTracker::process
// ============================================================
static bool isPeaceSign (const HandLandmark lm[HandFrame::NUM_LANDMARKS])
{
    bool indexUp   = lm[8].y  < lm[6].y;
    bool middleUp  = lm[12].y < lm[10].y;
    bool ringDown  = lm[16].y > lm[14].y;
    bool pinkyDown = lm[20].y > lm[18].y;
    float thumbIdx = std::hypot (lm[4].x - lm[8].x, lm[4].y - lm[8].y);
    bool notPinch  = thumbIdx > 0.06f;
    return indexUp && middleUp && ringDown && pinkyDown && notPinch;
}

void PulseFieldPage::updateGesture (const HandFrame& hf)
{
    GestureState& s = gesture;

    for (int hi = 0; hi < hf.numHands; ++hi)
    {
        const auto& h = hf.hands[hi];
        if (! h.valid) continue;

        if (h.isRight)
        {
            // Index tip height → filter cutoff
            float rawY = h.lm[8].y;
            s.rawHandY = rawY;
            s.handY    = s.handY + SMOOTH_K * (rawY - s.handY);

            // Pinch (thumb ↔ index tip) → bit-crush toggle
            float pinchDist = std::hypot (h.lm[4].x - h.lm[8].x,
                                          h.lm[4].y - h.lm[8].y);
            s.isFist = (pinchDist < 0.04f);
        }
        else
        {
            // Peace sign → toggle delay lock
            if (peaceCooldown > 0)
            {
                --peaceCooldown;
                peaceCount = 0;
            }
            else
            {
                if (isPeaceSign (h.lm)) ++peaceCount;
                else                    peaceCount = 0;

                if (peaceCount >= PEACE_CONFIRM_FRAMES)
                {
                    s.delayLocked = ! s.delayLocked;
                    peaceCount    = 0;
                    peaceCooldown = PEACE_COOLDOWN_FRAMES;
                }
            }

            // Wrist height → delay feedback
            float rawFb = juce::jlimit (0.0f, 0.85f, 1.1f - h.lm[0].y * 1.3f);
            s.rawDelayFeedback = rawFb;
            s.delayFeedback    = s.delayFeedback + SMOOTH_K * (rawFb - s.delayFeedback);

            // Hand tilt → delay time (only when unlocked)
            if (! s.delayLocked)
            {
                float dy    = std::abs (h.lm[5].y - h.lm[17].y);
                float rawDt = juce::jlimit (0.05f, 0.8f, dy * 4.0f);
                s.rawDelayTime = rawDt;
                s.delayTimeVal = s.delayTimeVal + SMOOTH_K * (rawDt - s.delayTimeVal);
            }
        }
    }
}

// ============================================================
//  Timer — 40 Hz UI update
// ============================================================
void PulseFieldPage::timerCallback()
{
    // ── Parse /hands OSC data ──────────────────────────────────
    HandFrame hf;
    float numHandsF = oscHandsData[0].load (std::memory_order_relaxed);
    hf.numHands = juce::jlimit (0, 2, (int)numHandsF);

    for (int hi = 0; hi < hf.numHands; ++hi)
    {
        auto& h   = hf.hands[hi];
        int   base = 1 + hi * 43;
        h.valid   = true;
        float labelF = oscHandsData[base].load (std::memory_order_relaxed);
        h.isRight = (labelF > 0.5f);
        for (int li = 0; li < HandFrame::NUM_LANDMARKS; ++li)
        {
            h.lm[li].x = oscHandsData[base + 1 + li * 2    ].load (std::memory_order_relaxed);
            h.lm[li].y = oscHandsData[base + 1 + li * 2 + 1].load (std::memory_order_relaxed);
        }
    }

    // ── Update gesture state ───────────────────────────────────
    updateGesture (hf);

    // ── Push gesture params to audio atomics ──────────────────
    eng.atomicHandY      .store (gesture.handY,         std::memory_order_relaxed);
    eng.atomicDelayTime  .store (gesture.delayTimeVal,  std::memory_order_relaxed);
    eng.atomicDelayFb    .store (gesture.delayFeedback, std::memory_order_relaxed);
    eng.atomicReverbSize .store (sliderSize.value,      std::memory_order_relaxed);
    eng.atomicReverbWet  .store (sliderWet.value,       std::memory_order_relaxed);
    eng.atomicIsFist     .store (gesture.isFist,        std::memory_order_relaxed);
    eng.atomicIsPaused   .store (isPaused,              std::memory_order_relaxed);
    eng.atomicBpm        .store (bpm,                   std::memory_order_relaxed);
    eng.atomicActiveSteps.store (activeSteps,           std::memory_order_relaxed);

    // Push per-row volumes (under voice lock so audio thread is consistent)
    {
        const juce::ScopedLock sl (voiceLock);
        for (int r = 0; r < numRows; ++r)
            eng.rowVolumes[r] = volSliders[r].value;
    }

    // ── Snapshot waveform + vectorscope ring buffers for display ─────────
    {
        int ptr = eng.wavePtr.load (std::memory_order_relaxed);
        std::vector<float> snap (AudioEngine::WAVE_BUF);
        for (int i = 0; i < AudioEngine::WAVE_BUF; ++i)
            snap[i] = eng.waveBuf[(ptr + i) % AudioEngine::WAVE_BUF];
        waveformSnap = std::move (snap);
    }
    {
        int vp = eng.vecPtr.load (std::memory_order_relaxed);
        const int vbs = AudioEngine::VEC_BUF;
        std::vector<float> snapL (vbs), snapR (vbs);
        for (int i = 0; i < vbs; ++i)
        {
            snapL[i] = eng.vecBufL[(vp + i) % vbs];
            snapR[i] = eng.vecBufR[(vp + i) % vbs];
        }
        vecSnapL = std::move (snapL);
        vecSnapR = std::move (snapR);
    }

    // ── Advance hit envelopes ─────────────────────────────────
    {
        const juce::ScopedLock sl (hitEnvLock);
        hitEnvelopes.erase (
            std::remove_if (hitEnvelopes.begin(), hitEnvelopes.end(),
                [this](const HitEnv& e) {
                    const juce::ScopedLock sl2 (voiceLock);
                    if (e.voiceIdx >= (int)voices.size()) return true;
                    return e.pos >= (int)voices[e.voiceIdx].samples.size();
                }),
            hitEnvelopes.end());
        for (auto& e : hitEnvelopes) e.pos += 2048;
    }

    // ── Step flash decay ──────────────────────────────────────
    for (int i = 0; i < MAX_STEPS; ++i)
        stepFlash[i] *= 0.82f;

    repaint();
}

// ============================================================
//  Audio device initialised — store sample rate
// ============================================================
void PulseFieldPage::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    eng.sampleRate = dev->getCurrentSampleRate();
}

// ============================================================
//  Audio I/O callback — runs on the dedicated audio thread
//  Mirrors audio.py  DrumMachine::callback
// ============================================================
void PulseFieldPage::audioDeviceIOCallbackWithContext (
    const float* const*, int,
    float* const* outData, int numOutChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& ctx)
{
    // Silence on pause
    if (eng.atomicIsPaused.load (std::memory_order_relaxed))
    {
        for (int ch = 0; ch < numOutChannels; ++ch)
            juce::FloatVectorOperations::clear (outData[ch], numSamples);
        return;
    }

    const double sr = eng.sampleRate;
    const int    bpmNow = eng.atomicBpm.load (std::memory_order_relaxed);

    // Update timing
    int newSPS = std::max (1, (int)(sr * 60.0 / bpmNow / 2.0));
    eng.samplesPerStep = newSPS;

    const int activeStepsNow = eng.atomicActiveSteps.load (std::memory_order_relaxed);

    // Smooth all params (audio thread only)
    auto smooth = [&](float& cur, float target) {
        cur += AUDIO_SMOOTH_K * (target - cur);
    };
    smooth (eng.smoothReverbSize, eng.atomicReverbSize.load (std::memory_order_relaxed));
    smooth (eng.smoothReverbWet,  eng.atomicReverbWet .load (std::memory_order_relaxed));
    smooth (eng.smoothDelayTime,  eng.atomicDelayTime .load (std::memory_order_relaxed));
    smooth (eng.smoothDelayFb,    eng.atomicDelayFb   .load (std::memory_order_relaxed));

    // Filter cutoff from right-hand height
    float rawY     = eng.atomicHandY.load (std::memory_order_relaxed);
    float clampedY = juce::jlimit (0.0f, 1.0f, (rawY - 0.2f) / 0.5f);
    float targetAlpha = juce::jlimit (0.01f, 0.95f,
                                      std::pow (1.0f - clampedY, 2.0f));
    smooth (eng.smoothAlpha, targetAlpha);

    const float alpha         = eng.smoothAlpha;
    const float delayTimeSec  = eng.smoothDelayTime;
    const float delayFb       = eng.smoothDelayFb;
    const float reverbSize    = eng.smoothReverbSize;
    const float reverbWet     = eng.smoothReverbWet;
    const bool  isFist        = eng.atomicIsFist.load (std::memory_order_relaxed);

    // ── Sequencer triggers ─────────────────────────────────────
    {
        const juce::ScopedLock sl (voiceLock);
        const int bufStart = eng.totalSamplesOut;
        const int bufEnd   = bufStart + numSamples;
        int trigSample     = bufStart + eng.nextTrigger;

        while (trigSample < bufEnd)
        {
            int step   = (int(trigSample) / eng.samplesPerStep) % activeStepsNow;
            int offset = int(trigSample) - bufStart;

            for (int r = 0; r < numRows && r < (int)voices.size(); ++r)
            {
                if (step < MAX_STEPS && grid[r][step])
                {
                    ActiveSound as;
                    as.voiceIdx   = r;
                    as.samplePos  = 0;
                    as.startOffset= std::max (0, offset);
                    as.volume     = eng.rowVolumes[r];
                    activeSounds.push_back (as);

                    const juce::ScopedLock sl2 (hitEnvLock);
                    hitEnvelopes.push_back ({ r, 0 });
                }
            }
            trigSample += eng.samplesPerStep;
        }
        eng.nextTrigger = trigSample - bufEnd;
    }

    // ── Mix active sounds ──────────────────────────────────────
    std::vector<float> mix (numSamples, 0.0f);
    {
        const juce::ScopedLock sl (voiceLock);
        std::vector<ActiveSound> stillActive;

        for (auto& snd : activeSounds)
        {
            if (snd.voiceIdx >= (int)voices.size()) continue;
            const auto& data  = voices[snd.voiceIdx].samples;
            int dst   = std::max (0, snd.startOffset);
            int count = std::min (numSamples - dst, (int)data.size() - snd.samplePos);
            if (count > 0)
            {
                for (int i = 0; i < count; ++i)
                    mix[dst + i] += data[snd.samplePos + i] * snd.volume;
                snd.samplePos += count;
                snd.startOffset = 0;
            }
            if (snd.samplePos < (int)data.size())
                stillActive.push_back (snd);
        }
        activeSounds = std::move (stillActive);
    }

    eng.totalSamplesOut += numSamples;

    // ── Low-pass filter ───────────────────────────────────────
    std::vector<float> filtered (numSamples);
    float fs = eng.filterState;
    for (int i = 0; i < numSamples; ++i)
    {
        fs += alpha * (mix[i] - fs);
        filtered[i] = fs;
    }
    eng.filterState = fs;

    // ── Bit-crush ─────────────────────────────────────────────
    std::vector<float> dry (numSamples);
    if (isFist)
        for (int i = 0; i < numSamples; ++i)
            dry[i] = std::round (filtered[i] * 5.0f) / 5.0f;
    else
        dry = filtered;

    // ── Delay ─────────────────────────────────────────────────
    const int delaySamples = juce::jlimit (
        int(0.02 * sr), int(0.8 * sr),
        int(delayTimeSec * sr));

    std::vector<float> out (numSamples);
    int   dp  = eng.delayPtr;
    auto& db  = eng.delayBuf;
    const int dbs = AudioEngine::DELAY_BUF;

    // delayFb controls both the buffer feedback AND the wet output level.
    // At fb=0 there is no delay output; at fb=0.85 it is fully present.
    const float delayWetOut = juce::jlimit (0.0f, 0.95f, delayFb * 1.1f);
    for (int i = 0; i < numSamples; ++i)
    {
        int   rp  = (dp - delaySamples + dbs) % dbs;
        float wet = db[rp];
        db[dp]    = dry[i] + wet * delayFb;
        dp        = (dp + 1) % dbs;
        out[i]    = dry[i] + wet * delayWetOut;
    }
    eng.delayPtr = dp;

    // ── Schroeder reverb ──────────────────────────────────────
    if (reverbWet > 0.01f)
    {
        float combFb = juce::jlimit (0.0f, 0.92f, 0.72f + reverbSize * 0.08f);
        std::vector<float> revOut (numSamples, 0.0f);

        // Comb filters
        for (int k = 0; k < 4; ++k)
        {
            int  dlen = int(AudioEngine::COMB_BASES[k] * reverbSize);
            auto* buf = eng.combBufs[k];
            int  ptr  = eng.combPtrs[k];

            for (int i = 0; i < numSamples; ++i)
            {
                int r      = (ptr - dlen + AudioEngine::COMB_MAX) % AudioEngine::COMB_MAX;
                float d    = buf[r];
                buf[ptr]   = out[i] + d * combFb;
                revOut[i] += d;
                ptr        = (ptr + 1) % AudioEngine::COMB_MAX;
            }
            eng.combPtrs[k] = ptr;
        }
        for (auto& s : revOut) s *= 0.25f;

        // All-pass filters
        for (int k = 0; k < 2; ++k)
        {
            int  dlen = int(AudioEngine::AP_BASES[k] * reverbSize);
            auto* buf = eng.apBufs[k];
            int  ptr  = eng.apPtrs[k];
            std::vector<float> apSig (numSamples);
            const float apFb = 0.5f;

            for (int i = 0; i < numSamples; ++i)
            {
                int r      = (ptr - dlen + AudioEngine::AP_MAX) % AudioEngine::AP_MAX;
                float d    = buf[r];
                float v    = revOut[i] - apFb * d;
                buf[ptr]   = revOut[i] + apFb * d;
                apSig[i]   = v;
                ptr        = (ptr + 1) % AudioEngine::AP_MAX;
            }
            eng.apPtrs[k] = ptr;
            revOut = apSig;
        }

        for (int i = 0; i < numSamples; ++i)
            out[i] = std::tanh (out[i] * (1.0f - reverbWet * 0.5f)
                              + revOut[i] * reverbWet);
    }
    else
    {
        for (auto& s : out) s = std::tanh (s);
    }

    // ── Waveform ring buffer ───────────────────────────────────
    int wp  = eng.wavePtr.load (std::memory_order_relaxed);
    int wbs = AudioEngine::WAVE_BUF;
    for (int i = 0; i < numSamples; ++i)
    {
        eng.waveBuf[wp] = out[i];
        wp = (wp + 1) % wbs;
    }
    eng.wavePtr.store (wp, std::memory_order_relaxed);

    // ── Vectorscope ring buffer (L = delayed copy, R = current) ───────────
    // We use a half-buffer delay as "L" so the Lissajous has interesting shape
    {
        int vp  = eng.vecPtr.load (std::memory_order_relaxed);
        const int vbs = AudioEngine::VEC_BUF;
        const int halfDelay = vbs / 2;
        for (int i = 0; i < numSamples; ++i)
        {
            int rp = (vp - halfDelay + vbs) % vbs;
            eng.vecBufL[vp] = eng.vecBufR[rp];  // "left" = older sample
            eng.vecBufR[vp] = out[i];            // "right" = current
            vp = (vp + 1) % vbs;
        }
        eng.vecPtr.store (vp, std::memory_order_relaxed);
    }

    // ── Write to output ────────────────────────────────────────
    for (int ch = 0; ch < numOutChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            outData[ch][i] = out[i];
}

// ============================================================
//  Built-in synthesis helpers (return AudioSampleBuffer for API
//  compat, but we actually populate voices directly in ctor)
// ============================================================
juce::AudioSampleBuffer PulseFieldPage::synthKick()  { return {}; }
juce::AudioSampleBuffer PulseFieldPage::synthSnare() { return {}; }
juce::AudioSampleBuffer PulseFieldPage::synthHat()   { return {}; }

// ============================================================
//  resized
// ============================================================
void PulseFieldPage::resized()
{
    int w = getWidth(), h = getHeight();

    backButton.setBounds (w - 100, 8, 80, 26);

    const int PAD       = 12;
    const int HEADER_H  = 36;
    const int TOOLBAR_H = 46;
    const int GAUGE_W   = 32;
    const int GAUGE_GAP = 5;
    const int VIZ_W     = 200;   // right visualizer column width

    // ── Camera: 30% of height, 4:3, centred with flanking gauges ────────────
    int camH = std::min ((int)(h * 0.30f), 240);
    int camW = int(camH * (4.0f / 3.0f));
    // Right side needs 2 gauges; left side 1 gauge + viz column reservation
    int sideReserved = (GAUGE_W * 3 + GAUGE_GAP * 2) + PAD * 4 + VIZ_W;
    if (camW > w - sideReserved)
    {
        camW = w - sideReserved;
        camH = int(camW * 0.75f);
    }
    // Centre cam accounting for viz column on the right
    int camX = (w - VIZ_W - camW) / 2;
    int camY = HEADER_H;

    cameraRect = { camX, camY, camW, camH };
    cameraView.setBounds (cameraRect);
    overlayView.setBounds (getLocalBounds());

    int after       = camY + camH + PAD / 2;
    toolbarRect     = { 0, after, w - VIZ_W - PAD, TOOLBAR_H };
    int seqTop      = after + TOOLBAR_H + PAD;

    // ── Right visualizer column: waveform top half, vectorscope bottom half ──
    int vizX   = w - VIZ_W - PAD;
    int vizTop = HEADER_H;
    int vizH   = h - vizTop - PAD;
    int waveH  = vizH / 2 - PAD / 2;
    int vecH   = vizH - waveH - PAD;

    waveformRect    = { vizX, vizTop,              VIZ_W, waveH };
    vectorscopeRect = { vizX, vizTop + waveH + PAD, VIZ_W, vecH  };

    // ── Sequencer: fills left portion below toolbar ───────────────────────
    int nRows = numRows;
    int seqH  = nRows * seqRowH + 40;
    sequencerRect   = { 0, seqTop, w - VIZ_W - PAD * 2, seqH };
    gaugeSidebarRect = {};
}

// ============================================================
//  paint — orchestrates all drawing
// ============================================================
void PulseFieldPage::paint (juce::Graphics& g)
{
    // Background + grid
    drawBackground (g);

    // Camera border (camera content is a child component)
    if (! cameraRect.isEmpty())
    {
        g.setColour (PF_BORDER_DIM);
        g.drawRect  (cameraRect.toFloat(), 1.0f);
    }

    // Side gauges (flanking camera)
    const int PAD      = 14;
    const int GAUGE_W  = 36;
    const int GAUGE_GAP= 6;

    // Left: FILTER gauge
    int lgX = cameraRect.getX() - GAUGE_W - 8;
    float filterNorm = juce::jlimit (0.0f, 1.0f, 1.0f - (gesture.handY - 0.2f) / 0.5f);
    drawSideGauge (g, { lgX, cameraRect.getY(), GAUGE_W, cameraRect.getHeight() },
                   filterNorm, PF_COL_FILTER, "FILTER");

    // Right: TIME, FB
    int rg1X = cameraRect.getRight() + 8;
    int rg2X = rg1X + GAUGE_W + GAUGE_GAP;
    float timeNorm = gesture.delayTimeVal / 0.8f;
    float fbNorm   = gesture.delayFeedback / 0.85f;
    auto  timeCol  = gesture.delayLocked ? juce::Colour (0xffffff00) : PF_COL_TIME;
    drawSideGauge (g, { rg1X, cameraRect.getY(), GAUGE_W, cameraRect.getHeight() },
                   timeNorm, timeCol, "TIME");
    drawSideGauge (g, { rg2X, cameraRect.getY(), GAUGE_W, cameraRect.getHeight() },
                   fbNorm, PF_COL_FB, "FB");

    // Toolbar
    drawToolbar (g, toolbarRect);

    // Sequencer
    drawSequencer (g, sequencerRect);

    // Right visualizer column
    drawWaveformPanel  (g, waveformRect);
    drawVectorscope    (g, vectorscopeRect);

    // Confirm modal overlay
    if (confirmRow >= 0)
        drawConfirmModal (g);

    // Tap modal overlay
    if (tapMode)
        drawTapModal (g);
}

// ============================================================
//  Background + corner decorations
// ============================================================
void PulseFieldPage::drawBackground (juce::Graphics& g)
{
    g.fillAll (PF_BG_DARK);

    // Subtle scan-line grid
    g.setColour (juce::Colour (0x06ff3333));
    for (int x = 0; x < getWidth();  x += 40) g.drawVerticalLine   (x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40) g.drawHorizontalLine (y, 0.0f, (float)getWidth());

    // Corner brackets
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xaaff3333));
    float bL = 20.0f;
    g.drawLine (b.getX(),          b.getY(),           b.getX() + bL,      b.getY(),           1.5f);
    g.drawLine (b.getX(),          b.getY(),           b.getX(),            b.getY() + bL,      1.5f);
    g.drawLine (b.getRight() - bL, b.getY(),           b.getRight(),        b.getY(),           1.5f);
    g.drawLine (b.getRight(),      b.getY(),           b.getRight(),        b.getY() + bL,      1.5f);
    g.drawLine (b.getX(),          b.getBottom(),      b.getX() + bL,      b.getBottom(),      1.5f);
    g.drawLine (b.getX(),          b.getBottom() - bL, b.getX(),            b.getBottom(),      1.5f);
    g.drawLine (b.getRight() - bL, b.getBottom(),      b.getRight(),        b.getBottom(),      1.5f);
    g.drawLine (b.getRight(),      b.getBottom() - bL, b.getRight(),        b.getBottom(),      1.5f);

    // Title
    g.setColour (PF_ACCENT);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.drawText  ("// PULSEFIELD", 28, 8, 300, 24, juce::Justification::centredLeft);

    // Top glow gradient
    juce::ColourGradient grad (juce::Colours::transparentBlack, b.getX(), b.getY(),
                                juce::Colours::transparentBlack, b.getRight(), b.getY(), false);
    grad.addColour (0.2, juce::Colour (0x55ff3333));
    grad.addColour (0.5, juce::Colour (0xffff3333));
    grad.addColour (0.8, juce::Colour (0x55ff3333));
    g.setGradientFill (grad);
    g.fillRect (b.getX(), b.getY(), b.getWidth(), 1.5f);
}

// ============================================================
//  Side gauge (tall bar flanking camera)
// ============================================================
void PulseFieldPage::drawSideGauge (juce::Graphics& g, juce::Rectangle<int> bounds,
                                     float normVal, juce::Colour colour,
                                     const juce::String& label)
{
    // Track
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect  (bounds.toFloat());
    g.setColour (PF_BORDER_DIM);
    g.drawRect  (bounds.toFloat(), 1.0f);

    // Fill
    int fh = int(normVal * bounds.getHeight());
    if (fh > 0)
    {
        auto fillR = bounds.removeFromBottom (fh);
        g.setColour (colour.withAlpha (0.55f));
        g.fillRect  (fillR.toFloat());
        // Bright tip
        g.setColour (colour);
        g.fillRect  ((float)fillR.getX(), (float)fillR.getY(),
                     (float)fillR.getWidth(), 2.0f);
    }

    // Label below
    g.setColour (colour);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (label, juce::Rectangle<int> (bounds.getX(), bounds.getBottom() + 4,
                 bounds.getWidth(), 12), juce::Justification::centred);
}

// ============================================================
//  Toolbar strip (below camera)
// ============================================================
void PulseFieldPage::drawToolbar (juce::Graphics& g, juce::Rectangle<int> tb)
{
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRect  (tb);
    g.setColour (PF_BORDER_DIM);
    g.drawHorizontalLine (tb.getY(),      (float)tb.getX(), (float)tb.getRight());
    g.drawHorizontalLine (tb.getBottom(), (float)tb.getX(), (float)tb.getRight());

    const int PAD = 14;
    int curX = PAD;
    int midY = tb.getCentreY();

    // BPM readout
    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (15.0f)));
    juce::String bpmStr = juce::String (bpm) + " BPM";
    g.drawText  (bpmStr, curX, midY - 10, 90, 20, juce::Justification::centredLeft);
    curX += 95;

    // Step-count buttons [8 | 16 | 32]
    const int sbW = 38, sbH = 26;
    for (int i = 0; i < 3; ++i)
    {
        juce::Rectangle<int> r { curX, midY - sbH / 2, sbW, sbH };
        stepBtnRects[i] = r;
        bool isActive = (STEP_OPTIONS[i] == activeSteps);
        g.setColour (isActive ? PF_ACCENT.withAlpha (0.8f)
                              : juce::Colour (0xff2a2a2a));
        g.fillRect  (r.toFloat());
        g.setColour (isActive ? PF_BORDER_HOT : PF_BORDER_DIM);
        g.drawRect  (r.toFloat(), 1.0f);
        g.setColour (isActive ? PF_TEXT_PRI : PF_TEXT_DIM);
        g.setFont   (juce::Font (juce::FontOptions().withHeight (13.0f)));
        g.drawText  (juce::String (STEP_OPTIONS[i]), r, juce::Justification::centred);
        curX += sbW + 5;
    }

    curX += 16;

    // REVERB label + two mini horizontal sliders (SIZE / WET)
    g.setColour (PF_COL_REVERB.withAlpha (0.8f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("REVERB", curX, tb.getY() + 4, 50, 12, juce::Justification::centredLeft);

    auto drawMiniSlider = [&](HSlider& sl, const juce::String& name, int x)
    {
        juce::Rectangle<int> track { x, midY - 10, 28, 20 };
        sl.rect = track;

        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRect  (track.toFloat());
        g.setColour (PF_BORDER_DIM);
        g.drawRect  (track.toFloat(), 1.0f);

        int fillH = int(sl.normVal() * track.getHeight());
        if (fillH > 0)
        {
            g.setColour (PF_COL_REVERB.withAlpha (0.7f));
            g.fillRect  ((float)track.getX(),
                         (float)(track.getBottom() - fillH),
                         (float)track.getWidth(), (float)fillH);
        }
        // Bright handle line
        int handleY = track.getBottom() - fillH;
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawHorizontalLine (handleY, (float)track.getX(), (float)track.getRight());

        g.setColour (PF_TEXT_DIM);
        g.setFont   (juce::Font (juce::FontOptions().withHeight (8.0f)));
        g.drawText  (name, juce::Rectangle<int> (track.getX(), track.getBottom() + 2,
                     track.getWidth(), 10), juce::Justification::centred);
    };

    drawMiniSlider (sliderSize, "SZ",  curX);
    drawMiniSlider (sliderWet,  "WET", curX + 34);
    curX += 80;

    // SET BPM button (right-aligned)
    int tapW = 100, tapH = 30;
    tapBtnRect = { tb.getRight() - PAD - tapW, midY - tapH / 2, tapW, tapH };

    g.setColour (isPaused && tapMode ? PF_ACCENT.withAlpha (0.9f)
                                     : juce::Colour (0xff2a1010));
    g.fillRect  (tapBtnRect.toFloat());
    g.setColour (PF_ACCENT_DIM);
    g.drawRect  (tapBtnRect.toFloat(), 1.0f);
    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.drawText  ("SET BPM", tapBtnRect, juce::Justification::centred);
}

// ============================================================
//  Sequencer rows
// ============================================================
void PulseFieldPage::drawSequencer (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const int PAD   = 14;
    const int DEL_W = 30;
    const int NAME_W= 70;
    const int VOL_W = 18;
    const int LEFT_W= PAD + DEL_W + 6 + NAME_W + 6 + VOL_W + 8;

    seqLeft  = bounds.getX() + LEFT_W;
    int gridW = bounds.getWidth() - LEFT_W - PAD;
    seqCellW  = std::max (8, gridW / activeSteps);

    // Figure out current visual step (based on audio engine output position)
    int samplesPerStep = std::max (1, (int)(eng.sampleRate * 60.0 / bpm / 2.0));
    int visualStep = 0;
    if (samplesPerStep > 0)
        visualStep = (eng.totalSamplesOut / samplesPerStep) % activeSteps;

    int rowTop = bounds.getY();

    const juce::ScopedLock sl (voiceLock);

    for (int r = 0; r < numRows; ++r)
    {
        int ry = rowTop + r * seqRowH;

        // Row stripe
        g.setColour (r % 2 == 0 ? PF_BG_ROW_A : PF_BG_ROW_B);
        g.fillRect  (juce::Rectangle<int> { PAD, ry, bounds.getWidth() - PAD, seqRowH - 3 }.toFloat());
        g.setColour (PF_BORDER_DIM);
        g.drawRect  (juce::Rectangle<int> { PAD, ry, bounds.getWidth() - PAD, seqRowH - 3 }.toFloat(), 1.0f);

        // Delete button
        juce::Rectangle<int> delR { PAD, ry + (seqRowH - 22) / 2, DEL_W, 22 };
        delBtnRects[r] = delR;
        g.setColour (juce::Colour (0xff551111));
        g.fillRect  (delR.toFloat());
        g.setColour (PF_ACCENT_DIM);
        g.drawRect  (delR.toFloat(), 1.0f);
        g.setColour (PF_TEXT_PRI);
        g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
        g.drawText  ("DEL", delR, juce::Justification::centred);

        // Voice name
        juce::String name = (r < (int)voices.size() ? voices[r].name : "R" + juce::String(r));
        name = name.substring (0, 10);
        g.setColour (PF_TEXT_DIM);
        g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText  (name, juce::Rectangle<int> (PAD + DEL_W + 6, ry + (seqRowH - 14) / 2, NAME_W, 14),
                     juce::Justification::centredLeft);

        // Volume slider
        auto& vs = volSliders[r];
        int vx   = PAD + DEL_W + 6 + NAME_W + 6;
        vs.rect  = { vx, ry + 7, VOL_W, seqRowH - 14 };
        float fillH = vs.value * vs.rect.getHeight();
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRect  (vs.rect.toFloat());
        g.setColour (PF_BORDER_DIM);
        g.drawRect  (vs.rect.toFloat(), 1.0f);
        if (fillH > 0)
        {
            g.setColour (juce::Colour (0xff882233).withAlpha (0.8f));
            g.fillRect  ((float)vs.rect.getX(),
                         (float)(vs.rect.getBottom() - (int)fillH),
                         (float)vs.rect.getWidth(), fillH);
        }

        // Step cells
        for (int c = 0; c < activeSteps; ++c)
        {
            int cellX = seqLeft + c * seqCellW;
            juce::Rectangle<int> cell { cellX + 2, ry + 5, seqCellW - 4, seqRowH - 12 };
            bool isOn = (c < MAX_STEPS && grid[r][c]);

            // Playhead highlight
            if (c == visualStep)
            {
                g.setColour (PF_TEXT_PRI.withAlpha (0.6f));
                g.drawRect  (cell.expanded (2).toFloat(), 1.5f);
                if (c < MAX_STEPS) stepFlash[c] = 1.0f;
            }

            // Cell fill
            float flash = (c < MAX_STEPS ? stepFlash[c] : 0.0f);
            if (isOn)
            {
                g.setColour (PF_COL_ACTIVE.withAlpha (0.55f + flash * 0.4f));
                g.fillRect  (cell.toFloat());
                g.setColour (PF_BORDER_HOT);
                g.drawRect  (cell.toFloat(), 1.0f);
            }
            else
            {
                g.setColour (PF_COL_INACT.withAlpha (0.8f + flash * 0.2f));
                g.fillRect  (cell.toFloat());
            }

            // Beat-group divider every 4 steps
            if (c > 0 && c % 4 == 0)
            {
                g.setColour (PF_BORDER_DIM);
                g.drawVerticalLine (cellX, (float)(ry + 4), (float)(ry + seqRowH - 6));
            }
        }
    }

    // Add-voice button
    int addY = rowTop + numRows * seqRowH + 10;
    addBtnRect = { seqLeft, addY, 120, 28 };
    g.setColour (juce::Colour (0xff1a0a0a));
    g.fillRect  (addBtnRect.toFloat());
    g.setColour (PF_BORDER_DIM);
    g.drawRect  (addBtnRect.toFloat(), 1.0f);
    g.setColour (PF_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.drawText  ("+ Add voice", addBtnRect, juce::Justification::centred);
}

// ============================================================
//  Waveform panel (oscilloscope)
// ============================================================
void PulseFieldPage::drawWaveformPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Background with subtle inner glow on the border
    g.setColour (PF_BG_CARD);
    g.fillRect  (bounds.toFloat());
    g.setColour (PF_BORDER_DIM);
    g.drawRect  (bounds.toFloat(), 1.0f);

    // Subtle horizontal centre line
    float cy = (float)bounds.getCentreY();
    g.setColour (juce::Colour (0x18ff2222));
    g.drawHorizontalLine ((int)cy, (float)bounds.getX(), (float)bounds.getRight());

    // Label
    g.setColour (PF_ACCENT.withAlpha (0.6f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (8.0f)));
    g.drawText  ("OSCILLOSCOPE", juce::Rectangle<int> (bounds.getX() + 5, bounds.getY() + 3,
                 bounds.getWidth() - 10, 11), juce::Justification::centredLeft);

    if (waveformSnap.empty()) return;

    const int   pad   = 8;
    const float cx0   = (float)(bounds.getX() + pad);
    const float amp   = (float)(bounds.getHeight() / 2 - pad - 6);
    const float ww    = (float)(bounds.getWidth() - pad * 2);
    const int   N     = (int)waveformSnap.size();
    // Downsample to pixel width for clarity
    const int   draw  = std::min (N, bounds.getWidth() - pad * 2);

    // Glow pass (wide, very dim)
    juce::Path glow;
    for (int i = 0; i < draw; ++i)
    {
        int   si = (int)((float)i / (float)(draw - 1) * (N - 1));
        float x  = cx0 + (float)i / (float)(draw - 1) * ww;
        float y  = cy  - juce::jlimit (-1.0f, 1.0f, waveformSnap[si]) * amp;
        if (i == 0) glow.startNewSubPath (x, y);
        else        glow.lineTo (x, y);
    }
    g.setColour (PF_COL_WAVE.withAlpha (0.07f));
    g.strokePath (glow, juce::PathStrokeType (6.0f));

    // Mid glow
    g.setColour (PF_COL_WAVE.withAlpha (0.18f));
    g.strokePath (glow, juce::PathStrokeType (2.5f));

    // Sharp core line
    g.setColour (PF_COL_WAVE.withAlpha (0.85f));
    g.strokePath (glow, juce::PathStrokeType (1.0f));

    // Hit-envelope flashes — now drawn in deep red/crimson, fading from bright to dark
    {
        const juce::ScopedLock sl (hitEnvLock);
        for (const auto& env : hitEnvelopes)
        {
            const juce::ScopedLock sl2 (voiceLock);
            if (env.voiceIdx >= (int)voices.size()) continue;
            const auto& data = voices[env.voiceIdx].samples;
            if (data.empty()) continue;

            float fade = juce::jlimit (0.0f, 1.0f, 1.0f - (float)env.pos / (float)data.size());
            if (fade < 0.01f) continue;

            int envN = std::min (bounds.getWidth() - pad * 2, draw);
            juce::Path ep;
            for (int i = 0; i < envN; ++i)
            {
                int   si = (int)((float)i / (float)(envN - 1) * (data.size() - 1));
                float x  = cx0 + (float)i / (float)(envN - 1) * ww;
                float y  = cy  - juce::jlimit (-1.0f, 1.0f, data[si]) * amp;
                if (i == 0) ep.startNewSubPath (x, y);
                else        ep.lineTo (x, y);
            }
            // Bright red flash fading to dark
            g.setColour (juce::Colour (0xffff2222).withAlpha (fade * 0.9f));
            g.strokePath (ep, juce::PathStrokeType (1.5f));
            g.setColour (juce::Colour (0xffff2222).withAlpha (fade * 0.15f));
            g.strokePath (ep, juce::PathStrokeType (4.0f));
        }
    }
}

// ============================================================
//  Vectorscope (Lissajous) — L vs R delayed signal
// ============================================================
void PulseFieldPage::drawVectorscope (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (PF_BG_CARD);
    g.fillRect  (bounds.toFloat());

    if (vecSnapL.empty()) return;

    // Initialize or resize the trail buffer if needed
    if (!vecTrailBuffer.isValid() || vecTrailBuffer.getWidth() != bounds.getWidth())
        vecTrailBuffer = juce::Image (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);

    // 1. Fade the old trails
    {
        juce::Graphics gBuf (vecTrailBuffer);
        gBuf.setColour (PF_BG_CARD.withAlpha (0.2f)); // Control trail length here
        gBuf.fillRect (vecTrailBuffer.getBounds());
    }

    // 2. Draw the new vector frame into the buffer
    {
        juce::Graphics gBuf (vecTrailBuffer);
        float cx = bounds.getWidth() * 0.5f;
        float cy = bounds.getHeight() * 0.5f;
        float rad = std::min (cx, cy) - 10.0f;

        gBuf.setColour (PF_COL_VEC);
        for (size_t i = 1; i < vecSnapL.size(); ++i) {
            gBuf.drawLine (cx + vecSnapL[i-1] * rad, cy - vecSnapR[i-1] * rad,
                           cx + vecSnapL[i]   * rad, cy - vecSnapR[i]   * rad, 1.2f);
        }
    }

    // 3. Draw the buffered trails to the screen
    g.drawImageAt (vecTrailBuffer, bounds.getX(), bounds.getY());
}

// ============================================================
//  Hand skeleton overlay (drawn by overlayView above cameraView)
// ============================================================
static const int HAND_CONNECTIONS[23][2] = {
    {0,1},{1,2},{2,3},{3,4},
    {0,5},{5,6},{6,7},{7,8},
    {0,9},{9,10},{10,11},{11,12},
    {0,13},{13,14},{14,15},{15,16},
    {0,17},{17,18},{18,19},{19,20},
    {5,9},{9,13},{13,17}
};
static constexpr int NUM_HAND_CONNECTIONS = 23;

void PulseFieldPage::drawHandSkeleton (juce::Graphics& g)
{
    // Decode latest OSC hand data
    float numHandsF = oscHandsData[0].load (std::memory_order_relaxed);
    int nh = juce::jlimit (0, 2, (int)numHandsF);

    for (int hi = 0; hi < nh; ++hi)
    {
        int   base    = 1 + hi * 43;
        float labelF  = oscHandsData[base].load (std::memory_order_relaxed);
        bool  isRight = (labelF > 0.5f);

        // Pixel coords inside cameraRect (normalised -> pixel, un-mirrored)
        juce::Point<float> pts[21];
        for (int li = 0; li < 21; ++li)
        {
            float nx = oscHandsData[base + 1 + li * 2    ].load (std::memory_order_relaxed);
            float ny = oscHandsData[base + 1 + li * 2 + 1].load (std::memory_order_relaxed);
            pts[li] = {
                (float)cameraRect.getX() + nx * (float)cameraRect.getWidth(),
                (float)cameraRect.getY() + ny * (float)cameraRect.getHeight()
            };
        }

        juce::Colour lineCol = isRight ? PF_COL_FILTER : PF_COL_TIME;
        g.setColour (lineCol.withAlpha (0.8f));

        for (int ci = 0; ci < NUM_HAND_CONNECTIONS; ++ci)
        {
            int a = HAND_CONNECTIONS[ci][0];
            int b = HAND_CONNECTIONS[ci][1];
            g.drawLine (pts[a].x, pts[a].y, pts[b].x, pts[b].y, 1.5f);
        }

        // Landmark dots
        for (int li = 0; li < 21; ++li)
        {
            bool isTip = (li == 4 || li == 8 || li == 12 || li == 16 || li == 20);
            float r = isTip ? 4.5f : 2.5f;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillEllipse (pts[li].x - r, pts[li].y - r, r * 2.0f, r * 2.0f);
            g.setColour (lineCol.withAlpha (0.9f));
            g.drawEllipse (pts[li].x - r, pts[li].y - r, r * 2.0f, r * 2.0f, 1.0f);
        }

        // Pinch indicator (thumb-4 ↔ index-8)
        float pinchDist = std::hypot (
            oscHandsData[base + 1 + 4 * 2    ].load() - oscHandsData[base + 1 + 8 * 2    ].load(),
            oscHandsData[base + 1 + 4 * 2 + 1].load() - oscHandsData[base + 1 + 8 * 2 + 1].load());
        if (isRight && pinchDist < 0.055f)
        {
            auto mid = (pts[4] + pts[8]) * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawEllipse (mid.x - 10, mid.y - 10, 20.0f, 20.0f, 1.5f);
        }
    }
}

// ============================================================
//  Camera hint overlays
// ============================================================
void PulseFieldPage::drawCamOverlays (juce::Graphics& g)
{
    auto drawHint = [&](int x, int y, juce::Colour col, const juce::String& txt)
    {
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        int tw = g.getCurrentFont().getStringWidth (txt) + 8;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect  ((float)(x - 4), (float)(y - 2), (float)tw, 15.0f);
        g.setColour (col);
        g.drawText  (txt, x, y, tw, 13, juce::Justification::centredLeft);
    };

    drawHint (cameraRect.getX() + 6, cameraRect.getY() + 6,
              PF_COL_FILTER, "R: height=filter  pinch=crush");
    drawHint (cameraRect.getX() + 6, cameraRect.getBottom() - 20,
              PF_COL_TIME,   "L: tilt=time  height=fb  V=lock delay");

    if (gesture.isFist)
    {
        g.setColour (juce::Colour (0xffffff44));
        g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
        g.drawText  ("CRUSH ON",
                     cameraRect.getRight() - 85, cameraRect.getY() + 6,
                     80, 13, juce::Justification::centredLeft);
    }
    if (gesture.delayLocked)
    {
        g.setColour (juce::Colour (0xffffff00));
        g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
        g.drawText  ("DELAY LOCKED",
                     cameraRect.getRight() - 100, cameraRect.getBottom() - 20,
                     95, 13, juce::Justification::centredLeft);
    }
}

// ============================================================
//  Confirm-delete modal
// ============================================================
void PulseFieldPage::drawConfirmModal (juce::Graphics& g)
{
    // Dark overlay
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect  (getLocalBounds().toFloat());

    int bw = 340, bh = 160;
    int bx = (getWidth()  - bw) / 2;
    int by = (getHeight() - bh) / 2;
    juce::Rectangle<int> box { bx, by, bw, bh };

    g.setColour (PF_BG_CARD);
    g.fillRoundedRectangle (box.toFloat(), 8.0f);
    g.setColour (PF_BORDER_DIM);
    g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);

    juce::String vName = (confirmRow < (int)voices.size()
                          ? voices[confirmRow].name
                          : "Row " + juce::String (confirmRow));

    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.drawText  ("Remove \"" + vName + "\" ?",
                 box.getX() + 20, box.getY() + 28, bw - 40, 20,
                 juce::Justification::centred);

    g.setColour (PF_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText  ("This cannot be undone.",
                 box.getX() + 20, box.getY() + 54, bw - 40, 14,
                 juce::Justification::centred);

    confirmYesRect = { box.getCentreX() - 90, box.getBottom() - 48, 80, 28 };
    confirmNoRect  = { box.getCentreX() + 10, box.getBottom() - 48, 80, 28 };

    g.setColour (juce::Colour (0xff881111));
    g.fillRoundedRectangle (confirmYesRect.toFloat(), 5.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (confirmNoRect.toFloat(),  5.0f);
    g.setColour (PF_BORDER_DIM);
    g.drawRoundedRectangle (confirmYesRect.toFloat(), 5.0f, 1.0f);
    g.drawRoundedRectangle (confirmNoRect.toFloat(),  5.0f, 1.0f);

    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.drawText  ("Remove", confirmYesRect, juce::Justification::centred);
    g.drawText  ("Cancel", confirmNoRect,  juce::Justification::centred);
}

// ============================================================
//  Tap-tempo modal
// ============================================================
void PulseFieldPage::drawTapModal (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.82f));
    g.fillRect  (getLocalBounds().toFloat());

    int bw = 380, bh = 190;
    int bx = (getWidth()  - bw) / 2;
    int by = (getHeight() - bh) / 2;
    juce::Rectangle<int> box { bx, by, bw, bh };

    g.setColour (PF_BG_CARD);
    g.fillRoundedRectangle (box.toFloat(), 10.0f);
    g.setColour (PF_BORDER_DIM);
    g.drawRoundedRectangle (box.toFloat(), 10.0f, 1.0f);

    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (16.0f)));
    g.drawText  ("TAP HERE   " + juce::String (bpm) + " BPM",
                 box.getX(), box.getY() + 44, bw, 22, juce::Justification::centred);

    g.setColour (PF_TEXT_DIM);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText  ("Click repeatedly to set tempo",
                 box.getX(), box.getY() + 76, bw, 16, juce::Justification::centred);

    // DONE button
    juce::Rectangle<int> doneR { box.getCentreX() - 44, box.getBottom() - 46, 88, 30 };
    tapBtnRect = doneR;   // reuse tapBtnRect for the DONE button inside modal
    g.setColour (juce::Colour (0xff114422));
    g.fillRoundedRectangle (doneR.toFloat(), 5.0f);
    g.setColour (PF_BORDER_DIM);
    g.drawRoundedRectangle (doneR.toFloat(), 5.0f, 1.0f);
    g.setColour (PF_TEXT_PRI);
    g.setFont   (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.drawText  ("DONE", doneR, juce::Justification::centred);
}

// ============================================================
//  Hit-test helpers
// ============================================================
int PulseFieldPage::hitStepCell (juce::Point<int> pos) const
{
    if (seqCellW <= 0) return -1;
    int col = (pos.x - seqLeft) / seqCellW;
    int row = (pos.y - sequencerRect.getY()) / seqRowH;
    if (row < 0 || row >= numRows || col < 0 || col >= activeSteps) return -1;
    juce::Rectangle<int> cell {
        seqLeft + col * seqCellW + 2,
        sequencerRect.getY() + row * seqRowH + 5,
        seqCellW - 4, seqRowH - 12 };
    if (! cell.contains (pos)) return -1;
    return (row << 8) | col;
}

int PulseFieldPage::hitStepBtn (juce::Point<int> pos) const
{
    for (int i = 0; i < 3; ++i)
        if (stepBtnRects[i].contains (pos))
            return STEP_OPTIONS[i];
    return 0;
}

bool PulseFieldPage::hitAddBtn (juce::Point<int> pos) const
{
    return addBtnRect.contains (pos);
}

int PulseFieldPage::hitDelBtn (juce::Point<int> pos) const
{
    for (int r = 0; r < numRows; ++r)
        if (delBtnRects[r].contains (pos)) return r;
    return -1;
}

bool PulseFieldPage::hitTapBtn (juce::Point<int> pos) const
{
    return tapBtnRect.contains (pos);
}

bool PulseFieldPage::hitConfirmYes (juce::Point<int> pos) const
{
    return confirmYesRect.contains (pos);
}

bool PulseFieldPage::hitConfirmNo (juce::Point<int> pos) const
{
    return confirmNoRect.contains (pos);
}

// ============================================================
//  Mouse events
// ============================================================
void PulseFieldPage::mouseDown (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    // ── Confirm-delete modal takes priority ────────────────────
    if (confirmRow >= 0)
    {
        if (hitConfirmYes (pos))
        {
            int row = confirmRow;
            confirmRow = -1;
            // Remove voice + grid row
            {
                const juce::ScopedLock sl (voiceLock);
                if ((int)voices.size() > 1 && row < (int)voices.size())
                {
                    voices.erase (voices.begin() + row);
                    // Shift grid rows up
                    for (int r = row; r < MAX_ROWS - 1; ++r)
                        std::memcpy (grid[r], grid[r + 1], sizeof(grid[0]));
                    std::memset (grid[MAX_ROWS - 1], 0, sizeof(grid[0]));
                    numRows = (int)voices.size();
                }
            }
        }
        else if (hitConfirmNo (pos))
        {
            confirmRow = -1;
        }
        repaint();
        return;
    }

    // ── Tap modal ──────────────────────────────────────────────
    if (tapMode)
    {
        if (hitTapBtn (pos))
        {
            tapMode   = false;
            isPaused  = false;
        }
        else
        {
            // Tap to set BPM
            double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            tapTimes.push_back (now);
            if ((int)tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
            if ((int)tapTimes.size() >= 2)
            {
                double sum = 0.0;
                for (int i = 1; i < (int)tapTimes.size(); ++i)
                    sum += tapTimes[i] - tapTimes[i - 1];
                double avg = sum / (tapTimes.size() - 1);
                bpm = juce::jlimit (40, 240, (int)(60.0 / avg));
            }
        }
        repaint();
        return;
    }

    // ── Normal mode ────────────────────────────────────────────
    if (hitTapBtn (pos))
    {
        tapMode  = true;
        isPaused = true;
        tapTimes.clear();
        repaint();
        return;
    }

    if (int opt = hitStepBtn (pos))
    {
        activeSteps = opt;
        repaint();
        return;
    }

    if (hitAddBtn (pos))
    {
        if (numRows >= MAX_ROWS) return;

        // Launch the file chooser
        chooser = std::make_unique<juce::FileChooser> ("Select a WAV or MP3...",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.mp3");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
                loadAudioFile (file);
        });
        
        return;
    }

    int delRow = hitDelBtn (pos);
    if (delRow >= 0)
    {
        confirmRow = delRow;
        repaint();
        return;
    }

    // Toggle step cell
    int packed = hitStepCell (pos);
    if (packed >= 0)
    {
        int r = packed >> 8;
        int c = packed & 0xff;
        if (r < MAX_ROWS && c < MAX_STEPS)
        {
            grid[r][c] = !grid[r][c];
            repaint();
        }
        return;
    }

    // Reverb sliders
    if (sliderSize.rect.contains (pos)) { dragTarget = DragTarget::ReverbSize; }
    else if (sliderWet.rect.contains (pos)) { dragTarget = DragTarget::ReverbWet; }

    // Per-row volume sliders
    for (int r = 0; r < numRows; ++r)
    {
        if (volSliders[r].rect.contains (pos))
        {
            dragTarget = DragTarget::VSlider;
            dragVRow   = r;
            break;
        }
    }
}

void PulseFieldPage::mouseDrag (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    auto updateVSlider = [](VSlider& vs, int mouseY) {
        float t = 1.0f - (float)(mouseY - vs.rect.getY()) / (float)vs.rect.getHeight();
        vs.value = juce::jlimit (0.0f, 1.0f, t);
    };
    auto updateHSlider = [](HSlider& sl, int mouseY) {
        // Vertical HSlider: bottom = min, top = max
        float t = 1.0f - (float)(mouseY - sl.rect.getY()) / (float)sl.rect.getHeight();
        float raw = sl.minV + t * (sl.maxV - sl.minV);
        sl.value = juce::jlimit (sl.minV, sl.maxV, raw);
    };

    if (dragTarget == DragTarget::VSlider && dragVRow >= 0)
        updateVSlider (volSliders[dragVRow], pos.y);
    else if (dragTarget == DragTarget::ReverbSize)
        updateHSlider (sliderSize, pos.y);
    else if (dragTarget == DragTarget::ReverbWet)
        updateHSlider (sliderWet, pos.y);

    if (dragTarget != DragTarget::None) repaint();
}

void PulseFieldPage::mouseUp (const juce::MouseEvent&)
{
    dragTarget = DragTarget::None;
    dragVRow   = -1;
}

void PulseFieldPage::loadAudioFile (juce::File file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader != nullptr)
    {
        const juce::ScopedLock sl (voiceLock);
        Voice newVoice;
        newVoice.name = file.getFileNameWithoutExtension();
        newVoice.samples.resize ((int)reader->lengthInSamples);
        
        juce::AudioBuffer<float> temp (reader->numChannels, (int)reader->lengthInSamples);
        reader->read (&temp, 0, (int)reader->lengthInSamples, 0, true, true);
        
        for (int i = 0; i < newVoice.samples.size(); ++i)
            newVoice.samples[i] = temp.getSample (0, i);

        voices.push_back (std::move (newVoice));
        numRows = (int)voices.size();
        std::memset (grid[numRows - 1], 0, sizeof (grid[0]));
        
        juce::MessageManager::callAsync ([this] { resized(); repaint(); });
    }
}