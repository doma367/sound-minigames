#include "Dualcastpage.h"
#include "MainComponent.h"
#include <cmath>
#include <algorithm>
#include <numeric>

// ============================================================
//  Colour palette  — Somatun brand (dark / red dominant)
//  Dualcast adds two accent colours for the two voices:
//    Drone = icy blue    Lead = teal-green
//  These are used only for voice-specific elements; the shell
//  stays in the red-on-dark palette of the rest of the app.
// ============================================================
static const juce::Colour DC_BG_DARK     { 0xff030303 };
static const juce::Colour DC_BG_CARD     { 0xff0a0608 };
static const juce::Colour DC_ACCENT      { 0xffff2222 };  // brand red
static const juce::Colour DC_BORDER_DIM  { 0x2aff2222 };
static const juce::Colour DC_BORDER_HOT  { 0x99ff2222 };
static const juce::Colour DC_TEXT_PRI    { 0xffd8c8c8 };
static const juce::Colour DC_TEXT_DIM    { 0x55d8c8c8 };
static const juce::Colour DC_TEXT_ACC    { 0xffff4444 };

// Voice accent colours
static const juce::Colour DC_DRONE_COL   { 0xffff3333 };  // FleshSynth Red
static const juce::Colour DC_LEAD_COL    { 0xffff9922 };  // Warm Amber/Orange
static const juce::Colour DC_DRONE_DIM   { 0x33ff3333 };
static const juce::Colour DC_LEAD_DIM    { 0x33ff9922 };
static const juce::Colour DC_DIVIDER     { 0x22ffffff };
static const juce::Colour DC_PANEL       { 0xff0d0d12 };

// ============================================================
//  Music theory tables
// ============================================================
namespace DCMusic
{
    static const char* SCALE_NAMES[DC_NUM_SCALES] = {
        "Pentatonic", "Major", "Natural Minor", "Blues",
        "Dorian", "Phrygian", "Lydian", "Mixolydian", "Chromatic"
    };

    static const int SCALES[DC_NUM_SCALES][12] = {
        { 0, 2, 4, 7, 9,  -1, -1, -1, -1, -1, -1, -1 },   // Pentatonic   (5)
        { 0, 2, 4, 5, 7,   9, 11, -1, -1, -1, -1, -1 },   // Major        (7)
        { 0, 2, 3, 5, 7,   8, 10, -1, -1, -1, -1, -1 },   // Natural Minor(7)
        { 0, 3, 5, 6, 7,  10, -1, -1, -1, -1, -1, -1 },   // Blues        (6)
        { 0, 2, 3, 5, 7,   9, 10, -1, -1, -1, -1, -1 },   // Dorian       (7)
        { 0, 1, 3, 5, 7,   8, 10, -1, -1, -1, -1, -1 },   // Phrygian     (7)
        { 0, 2, 4, 6, 7,   9, 11, -1, -1, -1, -1, -1 },   // Lydian       (7)
        { 0, 2, 4, 5, 7,   9, 10, -1, -1, -1, -1, -1 },   // Mixolydian   (7)
        { 0, 1, 2, 3, 4,   5,  6,  7,  8,  9, 10, 11 },   // Chromatic   (12)
    };

    static const int SCALE_LENS[DC_NUM_SCALES] = { 5, 7, 7, 6, 7, 7, 7, 7, 12 };

    static const char* NOTE_NAMES[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };

    static const char* SOUND_NAMES[DC_NUM_SOUNDS] = {
        "Sine", "Organ", "Soft Pad", "Pluck"
    };

    static constexpr double C2_HZ = 65.406;

    static int scaleLen (int scaleIdx)
    {
        return SCALE_LENS[juce::jlimit (0, DC_NUM_SCALES - 1, scaleIdx)];
    }

    // Map discrete step → frequency
    static double stepToFreq (int step, int scaleIdx, int octave)
    {
        int sIdx = juce::jlimit (0, DC_NUM_SCALES - 1, scaleIdx);
        int len  = SCALE_LENS[sIdx];
        step     = juce::jlimit (0, len - 1, step);
        int semi = SCALES[sIdx][step];
        return C2_HZ * std::pow (2.0, (semi + octave * 12) / 12.0);
    }

    // Freq → note name string e.g. "A3"
    static juce::String freqToNoteName (double freq)
    {
        if (freq <= 0.0) return "---";
        double midi  = 12.0 * std::log2 (freq / C2_HZ) + 24.0;
        int    midiI = (int)std::round (midi);
        int    note  = ((midiI % 12) + 12) % 12;
        int    oct   = midiI / 12 - 1;
        return juce::String (NOTE_NAMES[note]) + juce::String (oct);
    }
}

// ============================================================
//  DCCameraView
// ============================================================
void DCCameraView::pushFrame (const juce::Image& img)
{
    {
        const juce::ScopedLock sl (lock);
        latest   = img;
        hasFrame = true;
    }
    if (onNewFrame) onNewFrame();
}

void DCCameraView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    {
        const juce::ScopedLock sl (lock);
        if (latest.isValid())
            g.drawImage (latest, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::centred |
                         juce::RectanglePlacement::fillDestination);
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
//  DCVideoReceiver
// ============================================================
DCVideoReceiver::DCVideoReceiver (DCCameraView& view)
    : juce::Thread ("DCVideoReceiver"), cameraView (view) {}

void DCVideoReceiver::startReceiver() { startThread(); }

void DCVideoReceiver::stopReceiver()
{
    signalThreadShouldExit();
    if (auto* s = streamSocket.load()) s->close();
    stopThread (2000);
}

bool DCVideoReceiver::readExact (juce::StreamingSocket& sock, void* buf, int numBytes)
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

void DCVideoReceiver::run()
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
                juce::MessageManager::callAsync ([this, img]()
                {
                    cameraView.pushFrame (img);
                });
        }

        streamSocket = nullptr;
        wait (kRetryMs);
    }
}

// ============================================================
//  DualcastPage — constructor / destructor
// ============================================================
DualcastPage::DualcastPage (MainComponent& mc)
    : mainComponent (mc)
{
    setLookAndFeel (&laf);

    // Default voice states (mirrors gestsamp.py initial values)
    droneState.scaleIdx = 0;  droneState.soundIdx = 1;  droneState.octave = 2;
    leadState .scaleIdx = 0;  leadState .soundIdx = 1;  leadState .octave = 3;
    droneState.step = DCMusic::scaleLen (0) / 2;
    leadState .step = DCMusic::scaleLen (0) / 2;

    waveformSnap.assign (AudioEngine::WAVE_BUF, 0.0f);

    // Camera receiver pushes frames to cameraView; we pull the image
    // out directly in paint() so the draw order is fully controlled.
    videoReceiver = std::make_unique<DCVideoReceiver> (cameraView);
    cameraView.onNewFrame = [this] { repaint(); };

    backButton.onClick = [this]
    {
        stop();
        mainComponent.showLanding();
    };
    addAndMakeVisible (backButton);
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
    videoReceiver = std::make_unique<DCVideoReceiver> (cameraView);
    videoReceiver->startReceiver();

    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);

    startTimerHz (40);

    mainComponent.setHandsCallback ([this](const juce::OSCMessage& m)
    {
        handleHandsMessage (m);
    });

    DBG ("[Dualcast] started");
}

void DualcastPage::stop()
{
    mainComponent.clearOSCCallbacks();

    stopTimer();
    if (videoReceiver) videoReceiver->stopReceiver();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    DBG ("[Dualcast] stopped");
}

// ============================================================
//  OSC — /hands  (message thread)
// ============================================================
void DualcastPage::handleHandsMessage (const juce::OSCMessage& m)
{
    const int n = juce::jmin (m.size(), (int)MAX_HANDS_FLOATS);
    for (int i = 0; i < n; ++i)
        if (m[i].isFloat32())
            oscHandsData[i].store (m[i].getFloat32(), std::memory_order_relaxed);
}

// ============================================================
//  Gesture helpers
// ============================================================
static constexpr float DC_PINCH_THRESH = 0.055f;  // thumb-index normalised distance

static float handSpread (const DCHandLandmark lm[])
{
    // Mean distance of all 5 fingertips from palm centre (lm[9])
    float palmX = lm[9].x, palmY = lm[9].y;
    float sum = 0.0f;
    for (int t : { 4, 8, 12, 16, 20 })
        sum += std::hypot (lm[t].x - palmX, lm[t].y - palmY);
    return sum / 5.0f;
}

static bool isPinch (const DCHandLandmark lm[])
{
    return std::hypot (lm[4].x - lm[8].x, lm[4].y - lm[8].y) < DC_PINCH_THRESH;
}

// ============================================================
//  processGesture  (called from timerCallback)
//  Mirrors gestsamp.py hand-tracking loop
// ============================================================
void DualcastPage::processGesture (const DCHandFrame& hf)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    // Reset per-frame visibility
    handPosLeft.valid  = false;
    handPosRight.valid = false;

    for (int hi = 0; hi < hf.numHands; ++hi)
    {
        const auto& h = hf.hands[hi];
        if (! h.valid) continue;

        // EMA-smoothed palm coords (gestsamp uses EMA(alpha=0.30) per hand)
        float rawX = h.lm[9].x;
        float rawY = h.lm[9].y;

        // We identify which voice state owns this hand, but also need
        // a per-hand EMA.  Since we have at most one left and one right
        // hand, we can use the handPosLeft/Right structs for tracking.

        float pinchDist = std::hypot (h.lm[4].x - h.lm[8].x, h.lm[4].y - h.lm[8].y);
        bool  pinch     = pinchDist < DC_PINCH_THRESH;
        float spread    = handSpread (h.lm);

        // ── RIGHT → master volume ─────────────────────────────
        if (h.isRight)
        {
            // Smooth via the struct we stored last frame
            float prevX = handPosRight.valid ? handPosRight.x : rawX;
            float prevY = handPosRight.valid ? handPosRight.y : rawY;
            float ex    = EMA_ALPHA * rawX + (1.0f - EMA_ALPHA) * prevX;
            float ey    = EMA_ALPHA * rawY + (1.0f - EMA_ALPHA) * prevY;

            handPosRight.x       = ex;
            handPosRight.y       = ey;
            handPosRight.valid   = true;
            handPosRight.isRight = true;
            handPosRight.pinch   = pinch;

            // vol = clip(1.15 - ey * 1.3, 0, 1)
            float vol = juce::jlimit (0.0f, 1.0f, 1.15f - ey * 1.3f);
            // EMA on the volume itself (key "vol" in python, alpha=0.30)
            float prevVol = eng.atomicMasterVol.load (std::memory_order_relaxed);
            float smoothVol = EMA_ALPHA * vol + (1.0f - EMA_ALPHA) * prevVol;
            eng.atomicMasterVol.store (smoothVol, std::memory_order_relaxed);
        }

        // ── LEFT → pitch + trigger ────────────────────────────
        if (! h.isRight)
        {
            float prevX = handPosLeft.valid ? handPosLeft.x : rawX;
            float prevY = handPosLeft.valid ? handPosLeft.y : rawY;
            float ex    = EMA_ALPHA * rawX + (1.0f - EMA_ALPHA) * prevX;
            float ey    = EMA_ALPHA * rawY + (1.0f - EMA_ALPHA) * prevY;

            handPosLeft.x       = ex;
            handPosLeft.y       = ey;
            handPosLeft.valid   = true;
            handPosLeft.isRight = false;
            handPosLeft.pinch   = pinch;

            // Which half? left half → drone, right half → lead
            bool          isDrone = ex < 0.5f;
            DCVoiceState& vs      = isDrone ? droneState : leadState;

            // ── Hold timer: open hand → note ON ─────────────
            if (spread > SPREAD_OPEN)
            {
                if (vs.openHoldStart < 0.0)  vs.openHoldStart = now;
                if ((now - vs.openHoldStart) >= OPEN_HOLD_S)
                {
                    vs.noteOn = true;
                    if (isDrone) eng.atomicDroneOn.store (true, std::memory_order_relaxed);
                    else         eng.atomicLeadOn .store (true, std::memory_order_relaxed);
                }
            }
            else
            {
                vs.openHoldStart = -1.0;
            }

            // ── Hold timer: fist → note OFF ──────────────────
            if (spread < SPREAD_FIST)
            {
                if (vs.fistHoldStart < 0.0)  vs.fistHoldStart = now;
                if ((now - vs.fistHoldStart) >= FIST_HOLD_S)
                {
                    vs.noteOn = false;
                    if (isDrone) eng.atomicDroneOn.store (false, std::memory_order_relaxed);
                    else         eng.atomicLeadOn .store (false, std::memory_order_relaxed);
                }
            }
            else
            {
                vs.fistHoldStart = -1.0;
            }

            // ── Step controller: pinch + drag ─────────────────
            // Mirrors StepController.update() from gestsamp.py
            int scaleLen = DCMusic::scaleLen (vs.scaleIdx);

            if (pinch)
            {
                if (! vs.wasPinch)
                {
                    vs.anchorY = ey;
                }
                else
                {
                    float delta      = vs.anchorY - ey;   // up = positive
                    int   stepsMoved = (int)(delta / STEP_ZONE);
                    if (stepsMoved != 0)
                    {
                        vs.step = juce::jlimit (0, scaleLen - 1,
                                                vs.step + stepsMoved);
                        vs.anchorY -= stepsMoved * STEP_ZONE;
                    }
                }
            }
            vs.wasPinch = pinch;

            // Compute frequency and push to atomic
            double freq = DCMusic::stepToFreq (vs.step, vs.scaleIdx, vs.octave);
            if (isDrone) eng.atomicDroneFreq.store ((float)freq, std::memory_order_relaxed);
            else         eng.atomicLeadFreq .store ((float)freq, std::memory_order_relaxed);
        }
    }
}

// ============================================================
//  Timer  (40 Hz)
// ============================================================
void DualcastPage::timerCallback()
{
    // ── Parse OSC /hands ──────────────────────────────────────
    DCHandFrame hf;
    float numHandsF = oscHandsData[0].load (std::memory_order_relaxed);
    hf.numHands = juce::jlimit (0, 2, (int)numHandsF);

    for (int hi = 0; hi < hf.numHands; ++hi)
    {
        auto& h    = hf.hands[hi];
        int   base = 1 + hi * 43;
        h.valid    = true;
        h.isRight  = oscHandsData[base].load (std::memory_order_relaxed) > 0.5f;
        for (int li = 0; li < DCHandFrame::NUM_LANDMARKS; ++li)
        {
            h.lm[li].x = oscHandsData[base + 1 + li * 2    ].load (std::memory_order_relaxed);
            h.lm[li].y = oscHandsData[base + 1 + li * 2 + 1].load (std::memory_order_relaxed);
        }
    }

    processGesture (hf);

    // ── Sound indices ─────────────────────────────────────────
    eng.atomicDroneSound.store (droneState.soundIdx, std::memory_order_relaxed);
    eng.atomicLeadSound .store (leadState .soundIdx, std::memory_order_relaxed);

    // ── Snapshot waveform ─────────────────────────────────────
    {
        int ptr = eng.wavePtr.load (std::memory_order_relaxed);
        std::vector<float> snap (AudioEngine::WAVE_BUF);
        for (int i = 0; i < AudioEngine::WAVE_BUF; ++i)
            snap[i] = eng.waveBuf[(ptr + i) % AudioEngine::WAVE_BUF];
        waveformSnap = std::move (snap);
    }

    animTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    repaint();
}

// ============================================================
//  Audio device
// ============================================================
void DualcastPage::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    eng.sampleRate = dev->getCurrentSampleRate();
}

// ============================================================
//  Audio callback
//  Mirrors gestsamp.py  AudioEngine.callback + Voice.generate
//
//  Four synth voices (per DCVoiceState.soundIdx):
//    0 = Sine          simple sine
//    1 = Organ         band-limited additive (odd harmonics)
//    2 = Soft Pad      sine + 2nd harmonic, slow envelope
//    3 = Pluck         Karplus-Strong
//
//  Master volume envelope: EMA on volGain  (k ≈ 0.006/sample)
//  Final mix: (drone * 0.45 + lead * 0.30) * volEnv, clipped to [-1,1]
// ============================================================
void DualcastPage::audioDeviceIOCallbackWithContext (
    const float* const*, int,
    float* const* outData, int numOutChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    const double sr      = eng.sampleRate;
    const float  masterV = eng.atomicMasterVol.load (std::memory_order_relaxed);
    const bool   droneOn = eng.atomicDroneOn  .load (std::memory_order_relaxed);
    const bool   leadOn  = eng.atomicLeadOn   .load (std::memory_order_relaxed);
    const float  droneF  = eng.atomicDroneFreq.load (std::memory_order_relaxed);
    const float  leadF   = eng.atomicLeadFreq .load (std::memory_order_relaxed);
    const int    droneS  = eng.atomicDroneSound.load (std::memory_order_relaxed);
    const int    leadS   = eng.atomicLeadSound .load (std::memory_order_relaxed);

    // ── Smooth envelope helper: exp decay toward target ──────
    // k ≈ 0.004 for standard voices, 0.0025 for pad
    auto smoothEnv = [&](float cur, float tgt, float k, int frames,
                          std::vector<float>& envOut) -> float
    {
        envOut.resize (frames);
        for (int i = 0; i < frames; ++i)
        {
            cur += (tgt - cur) * (1.0f - std::exp (-k));
            envOut[i] = cur;
        }
        return cur;
    };

    // ── Rich (band-limited) wave — odd harmonics ──────────────
    auto richWave = [&](const std::vector<double>& phases, float freq) -> std::vector<float>
    {
        std::vector<float> sig (phases.size(), 0.0f);
        float nyq = (float)(sr / 2.0);
        int h = 1;
        while (freq * h < nyq)
        {
            for (int i = 0; i < (int)phases.size(); ++i)
                sig[i] += (1.0f / h) * std::sin ((float)(h * phases[i]));
            h += 2;
        }
        float mx = 0.0f;
        for (float s : sig) mx = std::max (mx, std::abs (s));
        if (mx > 1e-9f) for (auto& s : sig) s /= mx;
        return sig;
    };

    // ── Generate a voice block ────────────────────────────────
    auto generateVoice = [&](bool on, float freq, int soundIdx,
                              double& phase,
                              float& gain, float& padGain,
                              bool& prevOn,
                              std::vector<float>& ksBuf, int& ksPos,
                              int& ksSize, bool& ksActive) -> std::vector<float>
    {
        std::vector<float> out (numSamples, 0.0f);
        float tgt = on ? 1.0f : 0.0f;

        if (soundIdx == 3)   // ── Pluck (Karplus-Strong) ──
        {
            // Trigger on note-on edge
            if (on && ! prevOn)
            {
                ksSize = std::max (1, (int)(sr / std::max (1.0f, freq)));
                ksBuf.assign (ksSize, 0.0f);
                juce::Random rng;
                for (auto& s : ksBuf) s = rng.nextFloat() * 2.0f - 1.0f;
                ksPos    = 0;
                ksActive = true;
            }
            if (! on && prevOn) ksActive = false;
            prevOn = on;

            if (ksActive && ksSize > 0)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    out[i]      = ksBuf[ksPos];
                    int nxt     = (ksPos + 1) % ksSize;
                    ksBuf[ksPos] = 0.996f * 0.5f * (ksBuf[ksPos] + ksBuf[nxt]);
                    ksPos        = nxt;
                }
            }
            return out;
        }

        if (soundIdx == 2)   // ── Soft Pad ──
        {
            double inc = juce::MathConstants<double>::twoPi * freq / sr;
            std::vector<double> phases (numSamples);
            for (int i = 0; i < numSamples; ++i)
                phases[i] = phase + i * inc;
            phase = std::fmod (phases.back() + inc, juce::MathConstants<double>::twoPi);

            std::vector<float> env;
            padGain = smoothEnv (padGain, tgt, 0.0025f, numSamples, env);
            prevOn  = on;
            for (int i = 0; i < numSamples; ++i)
                out[i] = (std::sin ((float)phases[i])
                          + 0.3f * std::sin (2.0f * (float)phases[i])) * env[i];
            return out;
        }

        // ── Sine (0) or Organ (1) ──
        double inc = juce::MathConstants<double>::twoPi * freq / sr;
        std::vector<double> phases (numSamples);
        for (int i = 0; i < numSamples; ++i)
            phases[i] = phase + i * inc;
        phase = std::fmod (phases.back() + inc, juce::MathConstants<double>::twoPi);

        std::vector<float> sig;
        if (soundIdx == 1)
            sig = richWave (phases, freq);
        else
        {
            sig.resize (numSamples);
            for (int i = 0; i < numSamples; ++i)
                sig[i] = std::sin ((float)phases[i]);
        }

        std::vector<float> env;
        gain   = smoothEnv (gain, tgt, 0.004f, numSamples, env);
        prevOn = on;
        for (int i = 0; i < numSamples; ++i)
            out[i] = sig[i] * env[i];
        return out;
    };

    std::vector<float> droneSig = generateVoice (
        droneOn, droneF, droneS,
        eng.dronePhase, eng.droneGain, eng.dronePadGain, eng.dronePrevOn,
        eng.droneKsBuf, eng.droneKsPos, eng.droneKsSize, eng.droneKsActive);

    std::vector<float> leadSig  = generateVoice (
        leadOn, leadF, leadS,
        eng.leadPhase, eng.leadGain, eng.leadPadGain, eng.leadPrevOn,
        eng.leadKsBuf, eng.leadKsPos, eng.leadKsSize, eng.leadKsActive);

    // ── Volume envelope (EMA, k=0.006) ────────────────────────
    std::vector<float> mixed (numSamples);
    const float volK = 0.006f;
    for (int i = 0; i < numSamples; ++i)
    {
        eng.volGain += (masterV - eng.volGain) * (1.0f - std::exp (-volK));
        mixed[i]     = (droneSig[i] * 0.45f + leadSig[i] * 0.30f) * eng.volGain;
        mixed[i]     = juce::jlimit (-1.0f, 1.0f, mixed[i]);
    }

    // ── Waveform ring buffer ───────────────────────────────────
    int wp  = eng.wavePtr.load (std::memory_order_relaxed);
    int wbs = AudioEngine::WAVE_BUF;
    for (int i = 0; i < numSamples; ++i)
    {
        eng.waveBuf[wp] = mixed[i];
        wp = (wp + 1) % wbs;
    }
    eng.wavePtr.store (wp, std::memory_order_relaxed);

    // ── Write output ──────────────────────────────────────────
    for (int ch = 0; ch < numOutChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            outData[ch][i] = mixed[i];
}

// ============================================================
//  resized
// ============================================================
void DualcastPage::resized()
{
    int w = getWidth(), h = getHeight();

    backButton.setBounds (w - 100, 8, 80, 26);

    cameraRect = { 0, 0, w, h };

    // Increased toolbar height from 68 to 84 for extra padding
    toolbarRect = { 0, 0, w, 84 };

    // Increased HUD height from 58 to 70
    hudRect = { 0, h - 70, w, 70 };

    playRect = { 0, toolbarRect.getBottom(), w, h - toolbarRect.getBottom() - hudRect.getHeight() };

    // ── Toolbar buttons ──
    // Increased button width (148 -> 170) and height (28 -> 32)
    int bw = 170, bh = 32;
    int dCx = w / 4;
    int lCx = 3 * w / 4;

    droneScaleBtn = { dCx - bw / 2, toolbarRect.getY() + 12,         bw, bh };
    droneSoundBtn = { dCx - bw / 2, toolbarRect.getY() + 12 + bh + 4, bw, bh };
    leadScaleBtn  = { lCx - bw / 2, toolbarRect.getY() + 12,         bw, bh };
    leadSoundBtn  = { lCx - bw / 2, toolbarRect.getY() + 12 + bh + 4, bw, bh };
}

// ============================================================
//  paint  — single-pass compositing:
//    1. camera frame (full bounds)
//    2. background tints / panels
//    3. toolbar, zones, oscilloscope, HUD
//    4. hand skeleton overlay
// ============================================================
void DualcastPage::paint (juce::Graphics& g)
{
    // ── 1. Camera frame ───────────────────────────────────────
    {
        juce::Image frame = cameraView.getLatestFrame();
        if (frame.isValid())
        {
            g.drawImage (frame, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::centred |
                         juce::RectanglePlacement::fillDestination);
        }
        else
        {
            // No feed yet — dark grid placeholder
            g.fillAll (DC_BG_DARK);
            g.setColour (juce::Colour (0x0aff3333));
            for (int y = 0; y < getHeight(); y += 16)
                g.drawHorizontalLine (y, 0.0f, (float)getWidth());
            g.setColour (juce::Colour (0x44ff3333));
            g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            g.drawText ("// CAMERA FEED LOADING...", 12, getHeight() - 24,
                        getWidth() - 24, 18, juce::Justification::centredLeft);
        }
    }

    // ── 2-4. UI panels on top ─────────────────────────────────
    drawBackground   (g);
    drawToolbar      (g, toolbarRect);
    drawZones        (g, playRect);

    // Waveform oscilloscope strip
    {
        auto oscR = juce::Rectangle<int> (0, playRect.getBottom() - 60, getWidth(), 60);
        g.setColour (DC_DRONE_COL.withAlpha (0.08f));
        g.fillRect (oscR);
        g.setColour (DC_DRONE_COL.withAlpha (0.22f));
        g.drawHorizontalLine (oscR.getY(), 0.0f, (float)getWidth());

        const int nPts = (int)waveformSnap.size();
        if (nPts > 1)
        {
            float cx = oscR.getX(), cy = oscR.getCentreY(), ht = oscR.getHeight() / 2.0f - 4;
            juce::Path p;
            p.startNewSubPath (cx, cy - waveformSnap[0] * ht);
            float step = (float)oscR.getWidth() / (nPts - 1);
            for (int i = 1; i < nPts; ++i)
                p.lineTo (cx + i * step, cy - waveformSnap[i] * ht);

            g.setColour (DC_DRONE_COL.withAlpha (0.55f));
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
    }

    drawHUD          (g, hudRect);
    drawHandSkeleton (g);
}

// ============================================================
//  drawBackground
// ============================================================
void DualcastPage::drawBackground (juce::Graphics& g)
{
    // Translucent overlay so camera feed remains visible
    g.setColour (DC_BG_DARK.withAlpha (0.72f));
    g.fillRect (toolbarRect);
    g.setColour (DC_BG_DARK.withAlpha (0.30f));
    g.fillRect (playRect);
    g.setColour (DC_BG_DARK.withAlpha (0.85f));
    g.fillRect (hudRect);
}

// ============================================================
//  drawToolbar
// ============================================================
void DualcastPage::drawToolbar (juce::Graphics& g, juce::Rectangle<int> tb)
{
    // Bottom border line
    g.setColour (DC_BORDER_DIM);
    g.drawHorizontalLine (tb.getBottom() - 1, 0.0f, (float)getWidth());

    // "DUALCAST" title centred
    g.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));
    g.setColour (DC_TEXT_ACC);
    g.drawText ("// DUALCAST", tb.withTrimmedRight (110),
                juce::Justification::centred);

    // Drone buttons (left quarter)
    drawScaleBtn (g, droneScaleBtn,
                  juce::String (DCMusic::SCALE_NAMES[droneState.scaleIdx]),
                  droneState.showScaleMenu, DC_DRONE_COL);
    drawSoundBtn (g, droneSoundBtn,
                  juce::String (DCMusic::SOUND_NAMES[droneState.soundIdx]),
                  droneState.showSoundMenu, DC_DRONE_COL);

    // Lead buttons (right quarter)
    drawScaleBtn (g, leadScaleBtn,
                  juce::String (DCMusic::SCALE_NAMES[leadState.scaleIdx]),
                  leadState.showScaleMenu, DC_LEAD_COL);
    drawSoundBtn (g, leadSoundBtn,
                  juce::String (DCMusic::SOUND_NAMES[leadState.soundIdx]),
                  leadState.showSoundMenu, DC_LEAD_COL);

    // Zone labels under buttons - bumped up the height and margin
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.setColour (DC_DRONE_COL.withAlpha (0.6f));
    g.drawText ("DRONE",
                droneScaleBtn.getX(), toolbarRect.getBottom() - 16,
                droneScaleBtn.getWidth(), 12,
                juce::Justification::centred);
    g.setColour (DC_LEAD_COL.withAlpha (0.6f));
    g.drawText ("LEAD",
                leadScaleBtn.getX(), toolbarRect.getBottom() - 16,
                leadScaleBtn.getWidth(), 12,
                juce::Justification::centred);

    // Keyboard hints - adjusted trimming so it doesn't overlap borders
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.setColour (DC_TEXT_DIM);
    g.drawText ("Z/X drone oct  |  C/V lead oct  |  S/D drone scale  |  F/G lead scale  |  1-4 drone sound  |  Q-R lead sound",
                tb.reduced (12, 0).withTrimmedTop (tb.getHeight() - 18),
                juce::Justification::centred);
}

// ============================================================
//  drawScaleBtn / drawSoundBtn  — styled toolbar buttons
// ============================================================
void DualcastPage::drawScaleBtn (juce::Graphics& g, juce::Rectangle<int> r,
                                  const juce::String& label,
                                  bool active, juce::Colour accent)
{
    // Background
    g.setColour (active ? accent.withAlpha (0.22f) : DC_BG_CARD.withAlpha (0.88f));
    g.fillRect (r);

    // Border
    g.setColour (active ? accent.withAlpha (0.85f) : accent.withAlpha (0.25f));
    g.drawRect (r, 1);

    // Top glow line
    g.setColour (active ? accent.withAlpha (0.9f) : accent.withAlpha (0.18f));
    g.drawHorizontalLine (r.getY(), (float)r.getX(), (float)r.getRight());

    // Corner accents (top-left)
    g.setColour (active ? accent : accent.withAlpha (0.35f));
    g.drawLine ((float)r.getX(), (float)r.getY(), (float)r.getX() + 8, (float)r.getY(), 1.5f);
    g.drawLine ((float)r.getX(), (float)r.getY(), (float)r.getX(), (float)r.getY() + 8, 1.5f);
    // bottom-right
    g.drawLine ((float)r.getRight() - 8, (float)r.getBottom(), (float)r.getRight(), (float)r.getBottom(), 1.5f);
    g.drawLine ((float)r.getRight(), (float)r.getBottom() - 8, (float)r.getRight(), (float)r.getBottom(), 1.5f);

    // Label — "◀ SCALE ▶"
    g.setFont (juce::Font (juce::FontOptions().withHeight (11.5f)));
    g.setColour (active ? DC_BG_DARK : DC_TEXT_PRI);
    g.drawText (juce::String (L"\u25c4 ") + label + juce::String (L" \u25ba"),
                r, juce::Justification::centred);
}

void DualcastPage::drawSoundBtn (juce::Graphics& g, juce::Rectangle<int> r,
                                  const juce::String& label,
                                  bool active, juce::Colour accent)
{
    g.setColour (active ? accent.withAlpha (0.22f) : DC_BG_CARD.withAlpha (0.88f));
    g.fillRect (r);
    g.setColour (active ? accent.withAlpha (0.85f) : accent.withAlpha (0.25f));
    g.drawRect (r, 1);

    g.setFont (juce::Font (juce::FontOptions().withHeight (11.5f)));
    g.setColour (active ? DC_BG_DARK : DC_TEXT_PRI);
    g.drawText (juce::String (L"\u266a ") + label, r, juce::Justification::centred);
}

// ============================================================
//  drawDropdown
// ============================================================
void DualcastPage::drawDropdown (juce::Graphics& g, juce::Rectangle<int> anchor,
                                  const juce::StringArray& items, int sel,
                                  juce::Colour accent, bool above)
{
    const int iw = 155, ih = 26, gap = 2;
    int ax = anchor.getX();
    int ay = above ? anchor.getY() - (items.size() * (ih + gap))
                   : anchor.getBottom() + gap;

    for (int i = 0; i < items.size(); ++i)
    {
        juce::Rectangle<int> r { ax, ay + i * (ih + gap), iw, ih };
        bool isSel = (i == sel);

        g.setColour (isSel ? accent.withAlpha (0.85f) : DC_PANEL.withAlpha (0.92f));
        g.fillRect (r);
        g.setColour (isSel ? accent : accent.withAlpha (0.35f));
        g.drawRect (r, 1);

        g.setFont (juce::Font (juce::FontOptions().withHeight (11.5f)));
        g.setColour (isSel ? DC_BG_DARK : DC_TEXT_PRI);
        g.drawText (items[i], r.reduced (8, 0), juce::Justification::centredLeft);
    }
}

// ============================================================
//  drawZones  — note guidelines + hand glow + divider
// ============================================================
void DualcastPage::drawZones (juce::Graphics& g, juce::Rectangle<int> pa)
{
    int topY  = pa.getY();
    int botY  = pa.getBottom() - 65;  // leave room for oscilloscope strip
    int midX  = getWidth() / 2;
    int oscH  = 60;

    // ── Divider ──────────────────────────────────────────────
    g.setColour (DC_DIVIDER);
    g.drawVerticalLine (midX, (float)topY, (float)(botY));

    // ── Octave labels ─────────────────────────────────────────
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.5f)));
    g.setColour (DC_DRONE_COL.withAlpha (0.55f));
    g.drawText ("Oct " + juce::String (droneState.octave),
                12, topY + 4, 60, 12, juce::Justification::centredLeft);
    g.setColour (DC_LEAD_COL.withAlpha (0.55f));
    g.drawText ("Oct " + juce::String (leadState.octave),
                midX + 8, topY + 4, 60, 12, juce::Justification::centredLeft);

    // ── Note guidelines — drone (left zone) ───────────────────
    drawNoteGuidelines (g, { 0, topY, midX - 1, botY - topY }, droneState, true);

    // ── Note guidelines — lead (right zone) ──────────────────
    drawNoteGuidelines (g, { midX + 1, topY, getWidth() - midX - 1, botY - topY }, leadState, false);

    // ── Dropdown overlays ─────────────────────────────────────
    {
        juce::StringArray scaleNames, soundNames;
        for (int i = 0; i < DC_NUM_SCALES; ++i) scaleNames.add (DCMusic::SCALE_NAMES[i]);
        for (int i = 0; i < DC_NUM_SOUNDS; ++i) soundNames.add (DCMusic::SOUND_NAMES[i]);

        if (droneState.showScaleMenu)
            drawDropdown (g, droneScaleBtn, scaleNames, droneState.scaleIdx, DC_DRONE_COL, false);
        if (droneState.showSoundMenu)
            drawDropdown (g, droneSoundBtn, soundNames, droneState.soundIdx, DC_DRONE_COL, false);
        if (leadState.showScaleMenu)
            drawDropdown (g, leadScaleBtn,  scaleNames, leadState.scaleIdx,  DC_LEAD_COL,  false);
        if (leadState.showSoundMenu)
            drawDropdown (g, leadSoundBtn,  soundNames, leadState.soundIdx,  DC_LEAD_COL,  false);
    }

    // ── Hand overlays (drawn on top of guidelines) ─────────────
    if (handPosLeft.valid)
    {
        bool  isDrone = handPosLeft.x < 0.5f;
        auto& vs      = isDrone ? droneState : leadState;
        juce::Colour accent = isDrone ? DC_DRONE_COL : DC_LEAD_COL;
        float px = handPosLeft.x  * getWidth();
        float py = handPosLeft.y  * getHeight();
        drawHandGlow (g, px, py, accent, vs.noteOn, handPosLeft.pinch);
    }
    if (handPosRight.valid)
    {
        float px  = handPosRight.x * getWidth();
        float py  = handPosRight.y * getHeight();
        drawVolumeBar (g, eng.atomicMasterVol.load(), px, py);
    }
}

// ============================================================
//  drawNoteGuidelines
// ============================================================
void DualcastPage::drawNoteGuidelines (juce::Graphics& g, juce::Rectangle<int> zone,
                                        const DCVoiceState& vs, bool isDrone)
{
    juce::Colour accent = isDrone ? DC_DRONE_COL : DC_LEAD_COL;
    int sIdx   = vs.scaleIdx;
    int sLen   = DCMusic::scaleLen (sIdx);
    int topY   = zone.getY();
    int botY   = zone.getBottom();
    int leftX  = zone.getX();
    int rightX = zone.getRight();
    int curStep = vs.step;
    bool noteOn = vs.noteOn;
    double t   = animTime;

    // Compute Y positions (bottom = step 0, top = highest step)
    for (int i = 0; i < sLen; ++i)
    {
        int y = (sLen == 1) ? (topY + botY) / 2
                            : (int)(botY - i * (float)(botY - topY) / (sLen - 1));

        bool isCur     = (i == curStep);
        bool isPlaying = isCur && noteOn;

        int noteIdx = DCMusic::SCALES[sIdx][i] % 12;
        juce::String noteName = juce::String (DCMusic::NOTE_NAMES[(noteIdx + 12) % 12]);

        if (isPlaying)
        {
            float pulse = 0.55f + 0.45f * std::sin ((float)(t * 8.0));
            juce::Colour col  = accent.withAlpha (pulse);
            juce::Colour halo = accent.withAlpha (pulse * 0.3f);

            // Halo (7px wide)
            g.setColour (halo);
            g.drawHorizontalLine (y, (float)leftX, (float)rightX);
            for (int dy = -3; dy <= 3; ++dy)
                if (dy != 0) g.drawHorizontalLine (y + dy, (float)leftX, (float)rightX);

            // Main line
            g.setColour (col);
            g.drawHorizontalLine (y, (float)leftX, (float)rightX);

            // Note label — bright, larger
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.setColour (accent);
            int lx = isDrone ? leftX  + 6 : rightX - 30;
            g.drawText (noteName, lx, y - 7, 28, 14, juce::Justification::centredLeft);
        }
        else if (isCur)
        {
            // Current step, not playing
            g.setColour (accent.withAlpha (0.5f));
            g.drawHorizontalLine (y, (float)leftX, (float)rightX);

            g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            g.setColour (accent.withAlpha (0.7f));
            int lx = isDrone ? leftX  + 6 : rightX - 26;
            g.drawText (noteName, lx, y - 6, 24, 12, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (DC_TEXT_DIM.withAlpha (0.25f));
            g.drawHorizontalLine (y, (float)leftX, (float)rightX);

            g.setFont (juce::Font (juce::FontOptions().withHeight (9.5f)));
            g.setColour (DC_TEXT_DIM.withAlpha (0.35f));
            int lx = isDrone ? leftX  + 4 : rightX - 22;
            g.drawText (noteName, lx, y - 5, 20, 10, juce::Justification::centredLeft);
        }
    }
}

// ============================================================
//  drawHandGlow
// ============================================================
void DualcastPage::drawHandGlow (juce::Graphics& g, float px, float py,
                                  juce::Colour accent, bool noteOn, bool pinch)
{
    if (noteOn)
    {
        float pulse = 0.6f + 0.4f * std::sin ((float)(animTime * 8.0));
        int   r     = (int)(28.0f + 10.0f * pulse);
        juce::Colour col  = accent.withAlpha (pulse);
        juce::Colour halo = accent.withAlpha (pulse * 0.3f);

        g.setColour (halo);
        g.drawEllipse (px - r - 6, py - r - 6, (r + 6) * 2.0f, (r + 6) * 2.0f, 4.0f);
        g.setColour (col);
        g.drawEllipse (px - r, py - r, r * 2.0f, r * 2.0f, 2.0f);
    }
    if (pinch)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (px - 18, py - 18, 36.0f, 36.0f, 1.0f);
    }
}

// ============================================================
//  drawVolumeBar  (right hand)
// ============================================================
void DualcastPage::drawVolumeBar (juce::Graphics& g, float vol, float cx, float cy)
{
    const int bh = 100, bw = 10;
    float ytop = cy - 70.0f;
    juce::Rectangle<float> bg { cx - bw / 2.0f, ytop, (float)bw, (float)bh };

    g.setColour (DC_PANEL.withAlpha (0.7f));
    g.fillRect (bg);
    g.setColour (DC_TEXT_DIM.withAlpha (0.4f));
    g.drawRect (bg, 1.0f);

    int fill = (int)(vol * bh);
    if (fill > 0)
    {
        juce::Colour volCol { 0xffff9922 };  // Swapped from green to Amber
        g.setColour (volCol.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (cx - bw / 2.0f, ytop + bh - fill, (float)bw, (float)fill));
    }

    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.setColour (juce::Colour (0xffff9922).withAlpha (0.7f)); // Swapped to Amber
    g.drawText (juce::String ((int)(vol * 100)) + "%",
                (int)(cx - 16), (int)(ytop + bh + 4), 32, 12,
                juce::Justification::centred);
    g.drawText ("VOL", (int)(cx - 12), (int)(ytop - 14), 24, 12,
                juce::Justification::centred);
}

// ============================================================
//  drawHUD
// ============================================================
void DualcastPage::drawHUD (juce::Graphics& g, juce::Rectangle<int> hud)
{
    // Separator line
    g.setColour (DC_BORDER_DIM);
    g.drawHorizontalLine (hud.getY(), 0.0f, (float)getWidth());

    bool  droneOn = eng.atomicDroneOn  .load (std::memory_order_relaxed);
    bool  leadOn  = eng.atomicLeadOn   .load (std::memory_order_relaxed);
    float droneF  = eng.atomicDroneFreq.load (std::memory_order_relaxed);
    float leadF   = eng.atomicLeadFreq .load (std::memory_order_relaxed);

    juce::Colour dc = droneOn ? DC_DRONE_COL : DC_TEXT_DIM;
    juce::Colour lc = leadOn  ? DC_LEAD_COL  : DC_TEXT_DIM;

    // Drone info (left)
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.5f)));
    g.setColour (dc);
    // Increased bounding box width (280 -> 320) and height (18 -> 20)
    g.drawText ("DRONE  " + DCMusic::freqToNoteName (droneF) +
                "  " + juce::String (droneF, 1) + " Hz",
                16, hud.getY() + 14, 320, 20, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText (droneOn ? juce::String (L"\u25cf ON") : juce::String (L"\u25cb OFF"),
                16, hud.getY() + 38, 80, 14, juce::Justification::centredLeft);

    // Lead info (right)
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.5f)));
    g.setColour (lc);
    // Pulled it slightly further from the right edge and expanded the box
    g.drawText ("LEAD   " + DCMusic::freqToNoteName (leadF) +
                "  " + juce::String (leadF, 1) + " Hz",
                getWidth() - 340, hud.getY() + 14, 320, 20, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText (leadOn ? juce::String (L"\u25cf ON") : juce::String (L"\u25cb OFF"),
                getWidth() - 340, hud.getY() + 38, 80, 14, juce::Justification::centredLeft);

    // Keyboard hints (centre)
    g.setFont (juce::Font (juce::FontOptions().withHeight (8.5f)));
    g.setColour (DC_TEXT_DIM);
    g.drawText ("Left hand: pinch+drag = step  |  open = play  |  fist = stop  |  Right hand = volume",
                0, hud.getY() + 38, getWidth(), 12, juce::Justification::centred);
}

// ============================================================
//  drawHandSkeleton  — drawn on overlayView (camera coords)
//  Mirrors somatun_vision.py  draw_hand_landmarks
// ============================================================
void DualcastPage::drawHandSkeleton (juce::Graphics& g)
{
    // We don't have direct landmark data here (only palm EMA),
    // so we draw a simple position indicator ring per visible hand.
    // Full skeleton drawing requires landmark data forwarded from OSC,
    // which can be added as an extension if desired.
    // What we draw: a small cross-hair at palm position.

    auto drawCrosshair = [&](float nx, float ny, juce::Colour col)
    {
        float px = nx * getWidth();
        float py = ny * getHeight();
        float r  = 14.0f;
        g.setColour (col.withAlpha (0.5f));
        g.drawLine (px - r, py, px + r, py, 1.0f);
        g.drawLine (px, py - r, px, py + r, 1.0f);
        g.drawEllipse (px - 5, py - 5, 10, 10, 1.0f);
    };

    if (handPosLeft.valid)
        drawCrosshair (handPosLeft.x, handPosLeft.y,
                       handPosLeft.x < 0.5f ? DC_DRONE_COL : DC_LEAD_COL);

    if (handPosRight.valid)
        drawCrosshair (handPosRight.x, handPosRight.y,
                       juce::Colour (0xff44d448));
}

// ============================================================
//  Hit-test helpers
// ============================================================
int DualcastPage::hitScaleBtn (juce::Point<int> p) const
{
    if (droneScaleBtn.contains (p)) return 0;
    if (leadScaleBtn .contains (p)) return 1;
    return -1;
}

int DualcastPage::hitSoundBtn (juce::Point<int> p) const
{
    if (droneSoundBtn.contains (p)) return 0;
    if (leadSoundBtn .contains (p)) return 1;
    return -1;
}

int DualcastPage::hitDropdown (juce::Point<int> p, int voiceIdx, bool isScale) const
{
    const auto& anchorBtn = isScale ? (voiceIdx == 0 ? droneScaleBtn : leadScaleBtn)
                                    : (voiceIdx == 0 ? droneSoundBtn : leadSoundBtn);
    int itemCount = isScale ? DC_NUM_SCALES : DC_NUM_SOUNDS;
    const int iw = 155, ih = 26, gap = 2;
    int ax = anchorBtn.getX();
    int ay = anchorBtn.getBottom() + gap;

    for (int i = 0; i < itemCount; ++i)
    {
        juce::Rectangle<int> r { ax, ay + i * (ih + gap), iw, ih };
        if (r.contains (p)) return i;
    }
    return -1;
}

// ============================================================
//  mouseDown  — dropdown menus + keyboard handled here
//  Mirrors gestsamp.py mouse_cb + handle_voice_click
// ============================================================
void DualcastPage::mouseDown (const juce::MouseEvent& e)
{
    auto p = e.getPosition();

    // Close any open menu on click outside (early check)
    auto closeAllMenus = [&]
    {
        droneState.showScaleMenu = droneState.showSoundMenu = false;
        leadState .showScaleMenu = leadState .showSoundMenu = false;
    };

    // ── Dropdown interaction ─────────────────────────────────
    // Check open drone menus first
    if (droneState.showScaleMenu)
    {
        int hit = hitDropdown (p, 0, true);
        if (hit >= 0)
        {
            droneState.scaleIdx = hit;
            droneState.step = DCMusic::scaleLen (hit) / 2;
        }
        closeAllMenus();
        repaint(); return;
    }
    if (droneState.showSoundMenu)
    {
        int hit = hitDropdown (p, 0, false);
        if (hit >= 0)
        {
            droneState.soundIdx = hit;
            eng.atomicDroneSound.store (hit, std::memory_order_relaxed);
        }
        closeAllMenus();
        repaint(); return;
    }
    if (leadState.showScaleMenu)
    {
        int hit = hitDropdown (p, 1, true);
        if (hit >= 0)
        {
            leadState.scaleIdx = hit;
            leadState.step = DCMusic::scaleLen (hit) / 2;
        }
        closeAllMenus();
        repaint(); return;
    }
    if (leadState.showSoundMenu)
    {
        int hit = hitDropdown (p, 1, false);
        if (hit >= 0)
        {
            leadState.soundIdx = hit;
            eng.atomicLeadSound.store (hit, std::memory_order_relaxed);
        }
        closeAllMenus();
        repaint(); return;
    }

    // ── Button toggles ───────────────────────────────────────
    int scaleHit = hitScaleBtn (p);
    int soundHit = hitSoundBtn (p);

    if (scaleHit == 0)
    {
        closeAllMenus();
        droneState.showScaleMenu = true;
        repaint(); return;
    }
    if (scaleHit == 1)
    {
        closeAllMenus();
        leadState.showScaleMenu = true;
        repaint(); return;
    }
    if (soundHit == 0)
    {
        closeAllMenus();
        droneState.showSoundMenu = true;
        repaint(); return;
    }
    if (soundHit == 1)
    {
        closeAllMenus();
        leadState.showSoundMenu = true;
        repaint(); return;
    }

    // Click outside all menus → close everything
    closeAllMenus();
    repaint();
}