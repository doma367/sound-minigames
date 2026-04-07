#include "Pulsefieldpage.h"
#include "MainComponent.h"

const juce::Colour PulsefieldPage::kColBg       { 0xff0b0b11 };
const juce::Colour PulsefieldPage::kColPanel    { 0xff111119 };
const juce::Colour PulsefieldPage::kColActive   { 0xff00dc69 };
const juce::Colour PulsefieldPage::kColInactive { 0xff242432 };
const juce::Colour PulsefieldPage::kColFilter   { 0xff00d7ff };
const juce::Colour PulsefieldPage::kColTime     { 0xffffaa00 };
const juce::Colour PulsefieldPage::kColTimeLock { 0xffffff00 };
const juce::Colour PulsefieldPage::kColFeedback { 0xffff3232 };
const juce::Colour PulsefieldPage::kColReverb   { 0xff9155ff };
const juce::Colour PulsefieldPage::kColWaveform { 0xff00c8ff };
const juce::Colour PulsefieldPage::kColVol      { 0xffff961e };
const juce::Colour PulsefieldPage::kColAdd      { 0xff167d34 };
const juce::Colour PulsefieldPage::kColDel      { 0xffa01c1c };
const juce::Colour PulsefieldPage::kColBorder   { 0xff262637 };
const juce::Colour PulsefieldPage::kColLabel    { 0xff64647a };
const juce::Colour PulsefieldPage::kColToolbar  { 0xff0e0e14 };

const int PulsefieldPage::kHandConns[kNumHandConns][2] = {
    {0,1},{1,2},{2,3},{3,4}, {0,5},{5,6},{6,7},{7,8}, {0,9},{9,10},{10,11},{11,12},
    {0,13},{13,14},{14,15},{15,16}, {0,17},{17,18},{18,19},{19,20}, {5,9},{9,13},{13,17}
};

PulsefieldPage::PulsefieldPage (MainComponent& mc) : mainComponent (mc)
{
    setLookAndFeel (&laf);
    delayBuf.assign (kDelaySamples, 0.0f);

    addVoice ("Kick",   synthesiseKick  (44100.0), true);
    addVoice ("Snare",  synthesiseSnare (44100.0), true);
    addVoice ("Hi-hat", synthesiseHat   (44100.0), true);

    grid[0][0] = grid[0][4] = true;
    grid[1][2] = grid[1][6] = true;
    stepFlashes.assign (PF_MAX_STEPS, 0.0f);

    backButton.onClick = [this] { mainComponent.showLanding(); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (cameraView);
}

PulsefieldPage::~PulsefieldPage()
{
    stop();
    setLookAndFeel (nullptr);
}

void PulsefieldPage::start()
{
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
    videoReceiver.startReceiver();
    startTimerHz (40);

    // Register hands callback with shared OSC router
    mainComponent.setHandsCallback ([this](const juce::OSCMessage& m)
    {
        decodeHandsOSC (m);
    });

    DBG ("[Pulsefield] started, /hands callback registered");
}

void PulsefieldPage::stop()
{
    mainComponent.clearOSCCallbacks();

    stopTimer();
    videoReceiver.stopReceiver();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    DBG ("[Pulsefield] stopped");
}

// ============================================================
//  Drum synthesis  (CPU-side, called once at startup)
// ============================================================
std::vector<float> PulsefieldPage::synthesiseKick (double sr) const
{
    int len = (int)(sr * 0.1);
    std::vector<float> buf (len);
    juce::Random rng;
    for (int i = 0; i < len; ++i)
    {
        double t   = (double) i / sr;
        double env = std::exp (-t * 40.0);
        double osc = std::sin (2.0 * juce::MathConstants<double>::pi * 150.0
                                * std::pow (0.5, t / 0.03) * t);
        buf[i] = (float)(osc * env);
    }
    return buf;
}

std::vector<float> PulsefieldPage::synthesiseSnare (double sr) const
{
    int len = (int)(sr * 0.1);
    std::vector<float> buf (len);
    juce::Random rng;
    for (int i = 0; i < len; ++i)
    {
        double t   = (double) i / sr;
        double env = std::exp (-t * 60.0);
        float  n   = (rng.nextFloat() * 2.0f - 1.0f) * 0.2f;
        buf[i] = n * (float) env;
    }
    return buf;
}

std::vector<float> PulsefieldPage::synthesiseHat (double sr) const
{
    int len = (int)(sr * 0.03);
    std::vector<float> buf (len);
    juce::Random rng;
    for (int i = 0; i < len; ++i)
    {
        double t   = (double) i / sr;
        double env = std::exp (-t * 120.0);
        float  n   = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
        buf[i] = n * (float) env;
    }
    return buf;
}

// ============================================================
//  Voice management  (call on message thread)
// ============================================================
void PulsefieldPage::addVoice (const juce::String& name, std::vector<float> samples, bool builtIn)
{
    const juce::ScopedLock sl (voiceLock);
    DrumVoice v;
    v.name      = name;
    v.samples   = std::move (samples);
    v.isBuiltIn = builtIn;
    v.volume    = 0.8f;
    voices.push_back (std::move (v));

    // Grow grid row
    std::vector<bool> row (PF_MAX_STEPS, false);
    grid.push_back (std::move (row));

    rowVolumes.push_back (0.8f);
    volDragging.push_back (false);
    delBtnRects.add ({});
    volSliderRects.add ({});
}

void PulsefieldPage::removeVoice (int row)
{
    const juce::ScopedLock sl (voiceLock);
    if ((int) voices.size() <= 1) return;
    if (row < 0 || row >= (int) voices.size()) return;

    voices.erase (voices.begin() + row);
    grid.erase   (grid.begin()   + row);
    rowVolumes.erase  (rowVolumes.begin()  + row);
    volDragging.erase (volDragging.begin() + row);

    if (row < delBtnRects.size())   delBtnRects.remove (row);
    if (row < volSliderRects.size()) volSliderRects.remove (row);
}

bool PulsefieldPage::loadVoice (const juce::File& file)
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    auto* reader = mgr.createReaderFor (file);
    if (reader == nullptr) return false;

    juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    delete reader;

    std::vector<float> samples (buf.getNumSamples());
    auto* src = buf.getReadPointer (0);
    for (int i = 0; i < (int) samples.size(); ++i)
        samples[i] = src[i];

    juce::String name = file.getFileNameWithoutExtension().substring (0, 16);
    addVoice (name, std::move (samples), false);
    return true;
}

void PulsefieldPage::decodeHandsOSC (const juce::OSCMessage& m)
{
    if (m.isEmpty()) return;

    DBG ("[Pulsefield] /hands message size: " + juce::String(m.size()));

    HandsFrame frame;
    frame.numHands = (int) m[0].getFloat32();
    frame.numHands = juce::jlimit (0, 2, frame.numHands);

    for (int h = 0; h < frame.numHands; ++h)
    {
        int base = 1 + h * 43;   // stride = 1 + 21*2
        if (m.size() < base + 43) break;

        auto& hand = frame.hands[h];
        hand.isRight = (m[base].getFloat32() > 0.5f);
        hand.valid   = true;

        for (int lm = 0; lm < 21; ++lm)
        {
            hand.x[lm] = m[base + 1 + lm * 2    ].getFloat32();
            hand.y[lm] = m[base + 1 + lm * 2 + 1].getFloat32();
        }
    }

    const juce::ScopedLock sl (handsLock);
    latestHands = frame;
}

// ============================================================
//  Gesture state update  (called from timerCallback, message thread)
// ============================================================
static bool isPeace (const HandLandmarks& lm)
{
    bool indexUp    = lm.y[8]  < lm.y[6];
    bool middleUp   = lm.y[12] < lm.y[10];
    bool ringDown   = lm.y[16] > lm.y[14];
    bool pinkyDown  = lm.y[20] > lm.y[18];
    float thumbDist = std::hypot (lm.x[4] - lm.x[8], lm.y[4] - lm.y[8]);
    bool notPinch   = thumbDist > 0.06f;
    return indexUp && middleUp && ringDown && pinkyDown && notPinch;
}

void PulsefieldPage::updateGestureState()
{
    HandsFrame frame;
    {
        const juce::ScopedLock sl (handsLock);
        frame = latestHands;
    }

    for (int h = 0; h < frame.numHands; ++h)
    {
        const auto& hand = frame.hands[h];
        if (! hand.valid) continue;

        if (hand.isRight)
        {
            // Index tip height → filter cutoff
            float rawY = hand.y[8];
            handYSmooth += kSmoothK * (rawY - handYSmooth);

            // Pinch thumb↔index → bit-crush
            float pinchDist = std::hypot (hand.x[4] - hand.x[8], hand.y[4] - hand.y[8]);
            isFist = (pinchDist < 0.04f);
        }
        else  // Left hand
        {
            // Peace sign → toggle delay lock
            if (peaceCooldown > 0)
            {
                --peaceCooldown;
                peaceCount = 0;
            }
            else
            {
                if (isPeace (hand))  ++peaceCount;
                else                  peaceCount = 0;

                if (peaceCount >= kPeaceConfirmFrames)
                {
                    delayLocked  = ! delayLocked;
                    peaceCount   = 0;
                    peaceCooldown = kPeaceCooldownFrames;
                }
            }

            // Wrist height → delay feedback
            float rawFb = juce::jlimit (0.0f, 0.85f, 1.1f - hand.y[0] * 1.3f);
            delayFeedbackSmooth += kSmoothK * (rawFb - delayFeedbackSmooth);

            // Hand tilt → delay time (unlocked only)
            if (! delayLocked)
            {
                float dy    = std::abs (hand.y[5] - hand.y[17]);
                float rawDt = juce::jlimit (0.05f, 0.8f, dy * 4.0f);
                delayTimeSmooth += kSmoothK * (rawDt - delayTimeSmooth);
            }
        }
    }

    // If no hands, filter alpha stays at current; push updated FX params
    float rawY   = handYSmooth;
    float clamped = juce::jlimit (0.0f, 1.0f, (rawY - 0.2f) / 0.5f);
    float targetAlpha = juce::jlimit (0.01f, 0.95f,
                                      std::pow (1.0f - clamped, 2.0f));

    // Update FX param snapshot for audio thread
    {
        const juce::ScopedLock sl (fxLock);
        fxParams.filterAlpha   += 0.05f * (targetAlpha - fxParams.filterAlpha);
        fxParams.delayTimeSec   = delayTimeSmooth;
        fxParams.delayFeedback  = delayFeedbackSmooth;
        fxParams.reverbSize     = reverbSizeVal;
        fxParams.reverbWet      = reverbWetVal;
        fxParams.bitCrushOn     = isFist;
        fxParams.paused         = tapMode;   // sequencer paused during tap modal
    }
}

// ============================================================
//  Timer callback  (40 Hz, message thread)
// ============================================================
void PulsefieldPage::timerCallback()
{
    updateGestureState();

    // Decay step flashes
    for (auto& f : stepFlashes)
        f *= 0.82f;

    repaint();
}

// ============================================================
//  Audio device callback
// ============================================================
void PulsefieldPage::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    sampleRate = d->getCurrentSampleRate();
    outputLatencySamples = (int) d->getOutputLatencyInSamples();

    int bpmLocal = bpm;
    samplesPerStep = juce::jmax (1, (int)(sampleRate * (60.0 / bpmLocal / 2.0)));
    nextTrigger    = 0;

    // Rebuild built-in voices at correct sample rate
    {
        const juce::ScopedLock sl (voiceLock);
        if (voices.size() >= 1 && voices[0].isBuiltIn) voices[0].samples = synthesiseKick  (sampleRate);
        if (voices.size() >= 2 && voices[1].isBuiltIn) voices[1].samples = synthesiseSnare (sampleRate);
        if (voices.size() >= 3 && voices[2].isBuiltIn) voices[2].samples = synthesiseHat   (sampleRate);
    }

    // Clear delay + reverb
    std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
    delayPtr = 0;
    std::fill (combPtrs, combPtrs + 4, 0);
    std::fill (apPtrs,   apPtrs   + 2, 0);
    std::fill (&combBufs[0][0], &combBufs[0][0] + 4 * kCombBufSize, 0.0f);
    std::fill (&apBufs[0][0],   &apBufs[0][0]   + 2 * kAPBufSize,   0.0f);
    filterState = 0.0f;
}

void PulsefieldPage::audioDeviceIOCallbackWithContext (
    const float* const*, int,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Zero outputs
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch])
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    FXParams fx;
    {
        const juce::ScopedLock sl (fxLock);
        fx = fxParams;
    }

    if (fx.paused) return;

    // Update timing
    {
        int curBpm = bpm;
        int sps = juce::jmax (1, (int)(sampleRate * (60.0 / curBpm / 2.0)));
        samplesPerStep = sps;
    }

    // Sequencer trigger loop
    long long bufStart = totalSamples;
    long long bufEnd   = bufStart + numSamples;

    std::vector<std::pair<int,int>> triggers;   // (step, offset within buffer)

    {
        long long trigSample = bufStart + nextTrigger;
        while (trigSample < bufEnd)
        {
            int step   = (int)((trigSample / samplesPerStep) % activeSteps);
            int offset = (int)(trigSample - bufStart);
            triggers.push_back ({ step, offset });
            trigSample += samplesPerStep;
        }
        nextTrigger = (int)(trigSample - bufEnd);
    }

    // Gather voice snapshots
    struct VoiceSnap { const float* data; int len; float vol; };
    std::vector<VoiceSnap> snap;
    {
        const juce::ScopedLock sl (voiceLock);
        int nv = (int) voices.size();
        snap.resize (nv);
        for (int r = 0; r < nv; ++r)
            snap[r] = { voices[r].samples.data(),
                        (int) voices[r].samples.size(),
                        (r < (int) rowVolumes.size()) ? rowVolumes[r] : 1.0f };
    }

    // Fire triggers
    {
        const juce::ScopedLock sl (voiceLock);
        int nv = (int) snap.size();
        for (auto& [step, offset] : triggers)
        {
            for (int r = 0; r < nv && r < (int) grid.size(); ++r)
            {
                if (step < (int) grid[r].size() && grid[r][step])
                {
                    ActiveSound s;
                    s.data     = snap[r].data;
                    s.length   = snap[r].len;
                    s.position = 0;
                    s.volume   = snap[r].vol;
                    activeSounds.push_back (s);

                    // Hit envelope
                    const juce::ScopedLock el (envLock);
                    hitEnvelopes.push_back ({ snap[r].data, snap[r].len, 0 });
                }
            }
        }
    }

    // Mix active sounds
    std::vector<float> mix (numSamples, 0.0f);
    {
        auto it = activeSounds.begin();
        while (it != activeSounds.end())
        {
            int remaining = it->length - it->position;
            int count     = juce::jmin (numSamples, remaining);
            for (int i = 0; i < count; ++i)
                mix[i] += it->data[it->position + i] * it->volume;
            it->position += count;
            if (it->position >= it->length)
                it = activeSounds.erase (it);
            else
                ++it;
        }
    }

    // Advance hit envelopes
    {
        const juce::ScopedLock el (envLock);
        auto it = hitEnvelopes.begin();
        while (it != hitEnvelopes.end())
        {
            it->pos += numSamples;
            if (it->pos >= it->len) it = hitEnvelopes.erase (it);
            else                    ++it;
        }
    }

    // Filter (low-pass, alpha driven by hand height)
    float fs = filterState;
    float alpha = fx.filterAlpha;
    for (int i = 0; i < numSamples; ++i)
    {
        fs += alpha * (mix[i] - fs);
        mix[i] = fs;
    }
    filterState = fs;

    // Bit-crush
    if (fx.bitCrushOn)
        for (int i = 0; i < numSamples; ++i)
            mix[i] = std::round (mix[i] * 5.0f) / 5.0f;

    // Delay
    int delaySamples = (int) juce::jlimit (0.02f, 0.8f, fx.delayTimeSec) * (int) sampleRate;
    delaySamples     = juce::jlimit (1, kDelaySamples - 1, delaySamples);
    float fb         = fx.delayFeedback;

    std::vector<float> out (numSamples);
    {
        int dp  = delayPtr;
        int dbs = kDelaySamples;
        for (int i = 0; i < numSamples; ++i)
        {
            int   rp  = (dp - delaySamples + dbs) % dbs;
            float wet = delayBuf[rp];
            delayBuf[dp] = mix[i] + wet * fb;
            dp = (dp + 1) % dbs;
            out[i] = mix[i] + (fb > 0.05f ? wet * 0.3f : 0.0f);
        }
        delayPtr = dp;
    }

    // Reverb (Schroeder)
    if (fx.reverbWet > 0.01f)
    {
        float combFb = juce::jlimit (0.0f, 0.92f, 0.72f + fx.reverbSize * 0.08f);

        for (int i = 0; i < numSamples; ++i)
        {
            float rev = 0.0f;

            // Comb filters
            for (int k = 0; k < 4; ++k)
            {
                int dlen = (int)(kCombBase[k] * fx.reverbSize);
                dlen     = juce::jlimit (1, kCombBufSize - 1, dlen);
                int& ptr = combPtrs[k];
                int  bsz = kCombBufSize;
                int  rp  = (ptr - dlen + bsz) % bsz;
                float d  = combBufs[k][rp];
                combBufs[k][ptr] = out[i] + d * combFb;
                ptr = (ptr + 1) % bsz;
                rev += d;
            }
            rev *= 0.25f;

            // Allpass filters
            for (int k = 0; k < 2; ++k)
            {
                int dlen = (int)(kAPBase[k] * fx.reverbSize);
                dlen     = juce::jlimit (1, kAPBufSize - 1, dlen);
                int& ptr = apPtrs[k];
                int  bsz = kAPBufSize;
                int  rp  = (ptr - dlen + bsz) % bsz;
                float d  = apBufs[k][rp];
                float v  = rev - 0.5f * d;
                apBufs[k][ptr] = rev + 0.5f * d;
                ptr = (ptr + 1) % bsz;
                rev = v;
            }

            float w = fx.reverbWet;
            out[i] = std::tanh (out[i] * (1.0f - w * 0.5f) + rev * w);
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = std::tanh (out[i]);
    }

    // Write to waveform ring buffer
    {
        const juce::ScopedLock sl (waveLock);
        for (int i = 0; i < numSamples; ++i)
        {
            waveBuf[waveBufPtr] = out[i];
            waveBufPtr = (waveBufPtr + 1) % kWaveBufSize;
        }
    }

    // Output
    if (numOutputChannels > 0 && outputChannelData[0])
        for (int i = 0; i < numSamples; ++i)
            outputChannelData[0][i] = out[i];
    if (numOutputChannels > 1 && outputChannelData[1])
        for (int i = 0; i < numSamples; ++i)
            outputChannelData[1][i] = out[i];

    totalSamples += numSamples;
}

// ============================================================
//  Sequencer — synced step for playhead display
// ============================================================
int PulsefieldPage::getSyncedStep (int steps) const
{
    long long synced = juce::jmax (0LL, totalSamples - (long long) outputLatencySamples);
    if (samplesPerStep <= 0) return 0;
    return (int)((synced / samplesPerStep) % steps);
}

// ============================================================
//  Paint
// ============================================================
void PulsefieldPage::paint (juce::Graphics& g)
{
    g.fillAll (kColBg);

    drawBackground    (g);
    drawCameraSection (g, cameraRect);
    drawToolbar       (g, toolbarRect);
    drawSequencer     (g, sequencerRect);
    drawWaveformPanel (g, waveformRect);

    if (confirmRow >= 0)  drawConfirmModal (g);
    if (tapMode)          drawTapModal (g);
}

// ============================================================
//  drawBackground — subtle grid
// ============================================================
void PulsefieldPage::drawBackground (juce::Graphics& g)
{
    g.setColour (juce::Colour (0x08004422));
    for (int x = 0; x < getWidth();  x += 40) g.drawVerticalLine   (x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40) g.drawHorizontalLine (y, 0.0f, (float)getWidth());
}

// ============================================================
//  drawSideGauge — vertical fill bar (matches Python _draw_side_gauge)
// ============================================================
void PulsefieldPage::drawSideGauge (juce::Graphics& g, juce::Rectangle<int> b,
                                     float value, juce::Colour col, const juce::String& label)
{
    g.setColour (juce::Colour (0xff1e1e2a));
    g.fillRoundedRectangle (b.toFloat(), 5.0f);

    int fh = (int)(value * b.getHeight());
    if (fh > 0)
    {
        auto fill = b.removeFromBottom (fh);
        g.setColour (col);
        g.fillRoundedRectangle (fill.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        g.drawHorizontalLine (fill.getY(), (float)fill.getX(), (float)fill.getRight());
    }

    g.setColour (col);
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText (label, b.getX(), b.getBottom() + 3, b.getWidth(), 14,
                juce::Justification::centred);
}

// ============================================================
//  drawHandSkeleton — overlaid on camera rect
// ============================================================
void PulsefieldPage::drawHandSkeleton (juce::Graphics& g, juce::Rectangle<int> camBounds,
                                        const HandLandmarks& hand)
{
    if (! hand.valid) return;

    auto toPixel = [&](int lm) -> juce::Point<int>
    {
        // Mirror x to match the flipped camera feed
        float px = (float) camBounds.getX() + (1.0f - hand.x[lm]) * (float) camBounds.getWidth();
        float py = (float) camBounds.getY() + hand.y[lm] * (float) camBounds.getHeight();
        return { (int)px, (int)py };
    };

    juce::Colour lineCol = hand.isRight ? kColFilter : kColTime;
    g.setColour (lineCol);

    for (int c = 0; c < kNumHandConns; ++c)
    {
        auto a = toPixel (kHandConns[c][0]);
        auto b = toPixel (kHandConns[c][1]);
        g.drawLine ((float)a.x, (float)a.y, (float)b.x, (float)b.y, 2.0f);
    }

    int fingertips[] = { 4, 8, 12, 16, 20 };
    for (int lm = 0; lm < 21; ++lm)
    {
        auto pt = toPixel (lm);
        bool isTip = false;
        for (int f : fingertips) isTip |= (f == lm);
        int r = isTip ? 5 : 3;
        g.setColour (juce::Colours::white);
        g.fillEllipse ((float)(pt.x - r), (float)(pt.y - r), (float)(r * 2), (float)(r * 2));
        g.setColour (lineCol);
        g.drawEllipse ((float)(pt.x - r), (float)(pt.y - r), (float)(r * 2), (float)(r * 2), 1.0f);
    }
}

// ============================================================
//  drawCameraSection
// ============================================================
void PulsefieldPage::drawCameraSection (juce::Graphics& g, juce::Rectangle<int> camBounds)
{
    // Left gauge: FILTER
    float filterNorm = juce::jlimit (0.0f, 1.0f, 1.0f - (handYSmooth - 0.2f) / 0.5f);
    drawSideGauge (g, leftGaugeRect, filterNorm, kColFilter, "FILTER");

    // Right gauge 1: TIME
    float timeNorm = delayTimeSmooth / 0.8f;
    juce::Colour timeCol = delayLocked ? kColTimeLock : kColTime;
    drawSideGauge (g, rightGauge1Rect, timeNorm, timeCol, "TIME");

    // Right gauge 2: FB
    float fbNorm = delayFeedbackSmooth / 0.85f;
    drawSideGauge (g, rightGauge2Rect, fbNorm, kColFeedback, "FB");

    // Camera frame (drawn by CameraView child component — just border here)
    g.setColour (kColBorder);
    g.drawRect (camBounds, 1);

    // Hand skeleton overlays (drawn in paint order, on top of camera child)
    HandsFrame frame;
    {
        const juce::ScopedLock sl (handsLock);
        frame = latestHands;
    }
    for (int h = 0; h < frame.numHands; ++h)
        drawHandSkeleton (g, camBounds, frame.hands[h]);

    // Text hint overlays
    g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    auto drawHint = [&](int x, int y, juce::Colour col, const juce::String& txt)
    {
        juce::String t = txt;
        int tw = (int) g.getCurrentFont().getStringWidth (t) + 8;
        g.setColour (juce::Colour (0x96000000));
        g.fillRect (x - 4, y - 2, tw, 17);
        g.setColour (col);
        g.drawText (t, x, y, tw, 15, juce::Justification::centredLeft);
    };
    drawHint (camBounds.getX() + 6, camBounds.getY() + 6,
              kColFilter, "R hand  height=filter  pinch=crush");
    drawHint (camBounds.getX() + 6, camBounds.getBottom() - 20,
              kColTime,   "L hand  tilt=echo time  height=feedback  peace=lock delay");

    if (isFist)
    {
        g.setColour (juce::Colour (0xffffd200));
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText ("CRUSH ON", camBounds.getRight() - 90, camBounds.getY() + 6, 84, 15,
                    juce::Justification::centredLeft);
    }
    if (delayLocked)
    {
        g.setColour (kColTimeLock);
        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
        g.drawText ("DELAY LOCKED", camBounds.getRight() - 106, camBounds.getBottom() - 20, 100, 15,
                    juce::Justification::centredLeft);
    }
}

// ============================================================
//  drawToolbar
// ============================================================
void PulsefieldPage::drawToolbar (juce::Graphics& g, juce::Rectangle<int> tb)
{
    g.setColour (kColToolbar);
    g.fillRect (tb);
    g.setColour (kColBorder);
    g.drawHorizontalLine (tb.getY(),       (float)tb.getX(), (float)tb.getRight());
    g.drawHorizontalLine (tb.getBottom()-1, (float)tb.getX(), (float)tb.getRight());

    int cx  = tb.getX() + 14;
    int midy = tb.getCentreY();

    // BPM
    g.setColour (juce::Colour (0xffb4b4cd));
    g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    juce::String bpmStr = juce::String (bpm) + " BPM";
    g.drawText (bpmStr, cx, midy - 9, 80, 18, juce::Justification::centredLeft);
    cx += g.getCurrentFont().getStringWidth (bpmStr) + 14;

    // Step buttons
    int sbw = 38, sbh = 24;
    int stepOpts[] = { 8, 16, 32 };
    for (int i = 0; i < 3; ++i)
    {
        auto r = juce::Rectangle<int> (cx, midy - sbh/2, sbw, sbh);
        stepBtnRects[i] = r;
        bool sel = (stepOpts[i] == activeSteps);
        g.setColour (sel ? juce::Colour (0xff50508b) : juce::Colour (0xff2d2d41));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        g.drawText (juce::String (stepOpts[i]), r, juce::Justification::centred);
        cx += sbw + 5;
    }
    cx += 16;

    // REVERB label
    g.setColour (kColReverb);
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText ("REVERB", cx, tb.getY() + 4, 56, 14, juce::Justification::centredLeft);

    // Reverb SIZE slider
    int slH = tb.getHeight() - 8;
    int slY = tb.getY() + 4;
    reverbSizeRect = juce::Rectangle<int> (cx, slY, 28, slH);
    {
        auto r = reverbSizeRect;
        g.setColour (juce::Colour (0xff1e1e2a));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        float normSize = (reverbSizeVal - 0.5f) / (3.0f - 0.5f);
        int fh = (int)(normSize * r.getHeight());
        if (fh > 0)
        {
            g.setColour (kColReverb);
            g.fillRoundedRectangle (r.removeFromBottom (fh).toFloat(), 4.0f);
        }
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
        g.drawText ("SZ", reverbSizeRect.getX(), reverbSizeRect.getBottom() + 2,
                    reverbSizeRect.getWidth(), 12, juce::Justification::centred);
    }

    // Reverb WET slider
    reverbWetRect = juce::Rectangle<int> (cx + 34, slY, 28, slH);
    {
        auto r = reverbWetRect;
        g.setColour (juce::Colour (0xff1e1e2a));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        int fh = (int)(reverbWetVal * r.getHeight());
        if (fh > 0)
        {
            g.setColour (kColReverb);
            g.fillRoundedRectangle (r.removeFromBottom (fh).toFloat(), 4.0f);
        }
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
        g.drawText ("WET", reverbWetRect.getX(), reverbWetRect.getBottom() + 2,
                    reverbWetRect.getWidth(), 12, juce::Justification::centred);
    }

    // SET BPM button (right-aligned)
    tapBpmBtnRect = juce::Rectangle<int> (tb.getRight() - 124, midy - 16, 110, 32);
    g.setColour (juce::Colour (0xffaf2020));
    g.fillRoundedRectangle (tapBpmBtnRect.toFloat(), 7.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.drawText ("SET BPM", tapBpmBtnRect, juce::Justification::centred);
}

// ============================================================
//  drawSequencer
// ============================================================
void PulsefieldPage::drawSequencer (juce::Graphics& g, juce::Rectangle<int> seqArea)
{
    const juce::ScopedLock sl (voiceLock);

    int nRows    = (int) voices.size();
    int stepOpts[] = { 8, 16, 32 };
    int steps    = activeSteps;

    static constexpr int kDelW  = 30;
    static constexpr int kNameW = 70;
    static constexpr int kVolW  = 18;
    static constexpr int kPad   = 14;
    static constexpr int kLeftW = kPad + kDelW + 6 + kNameW + 6 + kVolW + 8;

    int gridW = seqArea.getWidth() - kLeftW - kPad;
    int cw    = juce::jmax (8, gridW / steps);
    seqCellW  = cw;
    seqLeftX  = seqArea.getX() + kLeftW;
    seqTopY   = seqArea.getY();

    delBtnRects.clearQuick();
    volSliderRects.clearQuick();

    int visualStep = getSyncedStep (steps);

    for (int r = 0; r < nRows; ++r)
    {
        int ry = seqArea.getY() + r * seqRowH;

        // Row stripe
        juce::Colour stripe = (r % 2 == 0) ? juce::Colour (0xff14141e) : juce::Colour (0xff0f0f16);
        g.setColour (stripe);
        g.fillRoundedRectangle ((float)(seqArea.getX() + kPad), (float)ry,
                                 (float)(seqArea.getWidth() - kPad * 2), (float)(seqRowH - 3), 4.0f);

        // DEL button
        auto delR = juce::Rectangle<int> (seqArea.getX() + kPad, ry + (seqRowH - 26) / 2, kDelW, 26);
        g.setColour (kColDel);
        g.fillRoundedRectangle (delR.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        g.drawText ("DEL", delR, juce::Justification::centred);
        delBtnRects.add (delR);

        // Voice name
        juce::String name = voices[r].name.substring (0, 10);
        g.setColour (kColLabel);
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        int nameX = seqArea.getX() + kPad + kDelW + 6;
        g.drawText (name, nameX, ry + (seqRowH - 14) / 2, kNameW, 14,
                    juce::Justification::centredLeft);

        // Volume slider
        int volX = nameX + kNameW + 6;
        auto volR = juce::Rectangle<int> (volX, ry + 7, kVolW, seqRowH - 14);
        g.setColour (juce::Colour (0xff1e1e2a));
        g.fillRoundedRectangle (volR.toFloat(), 4.0f);
        float vol  = (r < (int) rowVolumes.size()) ? rowVolumes[r] : 0.8f;
        int   volH = (int)(vol * volR.getHeight());
        if (volH > 0)
        {
            g.setColour (kColVol);
            g.fillRoundedRectangle (volR.removeFromBottom (volH).toFloat(), 4.0f);
        }
        volSliderRects.add (juce::Rectangle<int> (volX, ry + 7, kVolW, seqRowH - 14));

        // Step cells
        for (int c = 0; c < steps; ++c)
        {
            int cx2 = seqLeftX + c * cw;
            auto cell = juce::Rectangle<int> (cx2 + 2, ry + 5, cw - 4, seqRowH - 12);

            bool on = (c < (int) grid[r].size()) && grid[r][c];
            juce::Colour base = on ? kColActive : kColInactive;

            float flash = (c < (int) stepFlashes.size()) ? stepFlashes[c] : 0.0f;
            base = base.brighter (flash * 0.5f);

            if (c == visualStep)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (cell.expanded (2).toFloat(), 3.0f, 2.0f);
                if (c < (int) stepFlashes.size())
                    stepFlashes[c] = 1.0f;
            }

            g.setColour (base);
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);

            // Beat divider every 4 steps
            if (c > 0 && c % 4 == 0)
            {
                g.setColour (juce::Colour (0xff373748));
                g.drawVerticalLine (cx2, (float)(ry + 4), (float)(ry + seqRowH - 6));
            }
        }
    }

    // Add voice button
    int addY = seqArea.getY() + nRows * seqRowH + 10;
    addVoiceBtnRect = juce::Rectangle<int> (seqLeftX, addY, 120, 30);
    g.setColour (kColAdd);
    g.fillRoundedRectangle (addVoiceBtnRect.toFloat(), 6.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.drawText ("+ Add voice", addVoiceBtnRect, juce::Justification::centred);
}

// ============================================================
//  drawWaveformPanel
// ============================================================
void PulsefieldPage::drawWaveformPanel (juce::Graphics& g, juce::Rectangle<int> b)
{
    g.setColour (kColPanel);
    g.fillRoundedRectangle (b.toFloat(), 8.0f);
    g.setColour (kColBorder);
    g.drawRoundedRectangle (b.toFloat(), 8.0f, 1.0f);

    g.setColour (kColLabel);
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText ("LIVE WAVEFORM", b.getX() + 10, b.getY() + 8, b.getWidth() - 20, 14,
                juce::Justification::centredLeft);

    int innerY = b.getY() + 24;
    int innerH = b.getHeight() - 34;
    int innerW = b.getWidth() - 20;
    int midY   = innerY + innerH / 2;

    // Zero line
    g.setColour (juce::Colour (0xff23283a));
    g.drawHorizontalLine (midY, (float)(b.getX() + 10), (float)(b.getX() + 10 + innerW));

    // Waveform
    float waveCopy[kWaveBufSize];
    int   wavePtr;
    {
        const juce::ScopedLock sl (waveLock);
        std::copy (waveBuf, waveBuf + kWaveBufSize, waveCopy);
        wavePtr = waveBufPtr;
    }

    if (innerW > 1)
    {
        juce::Path path;
        for (int i = 0; i < innerW; ++i)
        {
            int idx = (wavePtr + i * kWaveBufSize / innerW) % kWaveBufSize;
            float val = juce::jlimit (-1.0f, 1.0f, waveCopy[idx]);
            float x = (float)(b.getX() + 10 + i);
            float y = (float)midY - val * (float)(innerH / 2 - 3);
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (kColWaveform);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    // Hit envelope flashes
    {
        const juce::ScopedLock sl (envLock);
        for (const auto& env : hitEnvelopes)
        {
            if (env.pos >= env.len || env.len < 2) continue;
            float fade = juce::jmax (0.0f, 1.0f - (float)env.pos / (float)env.len);
            juce::Colour envCol = juce::Colour::fromRGB (0, (uint8_t)(255 * fade), (uint8_t)(110 * fade));
            int envW = juce::jmin (innerW, 160);
            juce::Path ep;
            for (int i = 0; i < envW; ++i)
            {
                int idx = (int)((float)i / (float)envW * (float)env.len);
                idx = juce::jlimit (0, env.len - 1, idx);
                float val = juce::jlimit (-1.0f, 1.0f, env.data[idx]);
                float x = (float)(b.getX() + 10 + i);
                float y = (float)midY - val * (float)(innerH / 2 - 3);
                if (i == 0) ep.startNewSubPath (x, y);
                else        ep.lineTo (x, y);
            }
            g.setColour (envCol);
            g.strokePath (ep, juce::PathStrokeType (1.0f));
        }
    }
}

// ============================================================
//  drawTapModal
// ============================================================
void PulsefieldPage::drawTapModal (juce::Graphics& g)
{
    int W = getWidth(), H = getHeight();

    g.setColour (juce::Colour (0xd2000000));
    g.fillRect (getLocalBounds());

    juce::Rectangle<int> box (W / 2 - 190, H / 2 - 95, 380, 190);
    g.setColour (juce::Colour (0xff1e1e2c));
    g.fillRoundedRectangle (box.toFloat(), 12.0f);
    g.setColour (kColBorder);
    g.drawRoundedRectangle (box.toFloat(), 12.0f, 1.0f);

    g.setColour (juce::Colour (0xffe1e1e1));
    g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f)));
    g.drawText ("TAP HERE   " + juce::String (bpm) + " BPM",
                box, juce::Justification::centred);

    g.setColour (juce::Colour (0xff78788a));
    g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
    g.drawText ("Click repeatedly to set tempo",
                box.getX(), box.getY() + 70, box.getWidth(), 20,
                juce::Justification::centred);

    // DONE button
    tapModalRect = juce::Rectangle<int> (box.getCentreX() - 44, box.getBottom() - 46, 88, 30);
    g.setColour (juce::Colour (0xff00b941));
    g.fillRoundedRectangle (tapModalRect.toFloat(), 5.0f);
    g.setColour (juce::Colours::white);
    g.drawText ("DONE", tapModalRect, juce::Justification::centred);
}

// ============================================================
//  drawConfirmModal
// ============================================================
void PulsefieldPage::drawConfirmModal (juce::Graphics& g)
{
    int W = getWidth(), H = getHeight();

    g.setColour (juce::Colour (0xb4000000));
    g.fillRect (getLocalBounds());

    int bw = 340, bh = 160;
    juce::Rectangle<int> box (W / 2 - bw / 2, H / 2 - bh / 2, bw, bh);
    g.setColour (juce::Colour (0xff1c1c28));
    g.fillRoundedRectangle (box.toFloat(), 12.0f);
    g.setColour (kColBorder);
    g.drawRoundedRectangle (box.toFloat(), 12.0f, 1.0f);

    juce::String name = "Row " + juce::String (confirmRow);
    {
        const juce::ScopedLock sl (voiceLock);
        if (confirmRow < (int) voices.size())
            name = voices[confirmRow].name;
    }

    g.setColour (juce::Colour (0xffe6e6e6));
    g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.drawText ("Remove  \"" + name + "\" ?",
                box.getX(), box.getY() + 30, box.getWidth(), 20, juce::Justification::centred);

    g.setColour (juce::Colour (0xff78788c));
    g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    g.drawText ("This cannot be undone.",
                box.getX(), box.getY() + 58, box.getWidth(), 16, juce::Justification::centred);

    // YES button
    juce::Rectangle<int> yesR (box.getCentreX() - 90, box.getBottom() - 50, 80, 30);
    g.setColour (kColDel);
    g.fillRoundedRectangle (yesR.toFloat(), 6.0f);
    g.setColour (juce::Colours::white);
    g.drawText ("Remove", yesR, juce::Justification::centred);

    // NO button
    juce::Rectangle<int> noR (box.getCentreX() + 10, box.getBottom() - 50, 80, 30);
    g.setColour (juce::Colour (0xff2d2d41));
    g.fillRoundedRectangle (noR.toFloat(), 6.0f);
    g.setColour (juce::Colour (0xffc8c8c8));
    g.drawText ("Cancel", noR, juce::Justification::centred);
}

// ============================================================
//  resized
// ============================================================
void PulsefieldPage::resized()
{
    int W = getWidth(), H = getHeight();
    static constexpr int kPad         = 14;
    static constexpr int kGaugeW      = 28;
    static constexpr int kGaugeGap    = 6;
    static constexpr int kToolbarH    = 52;
    static constexpr int kSeqRowH     = 54;
    static constexpr int kSideReserved = kGaugeW * 2 + kGaugeGap + kPad * 2 + 8;

    seqRowH = kSeqRowH;

    // Camera
    int camH = juce::jmin ((int)(H * 0.36f), 280);
    int camW = (int)(camH * (4.0f / 3.0f));
    camW = juce::jmin (camW, W - kSideReserved);
    camH = (int)(camW * 0.75f);
    int camX = (W - camW) / 2;
    int camY = kPad;
    cameraRect = { camX, camY, camW, camH };
    cameraView.setBounds (cameraRect);

    // Side gauges
    int lgX = camX - kGaugeW - 8;
    leftGaugeRect = { lgX, camY, kGaugeW, camH };

    int rg1X = camX + camW + 8;
    rightGauge1Rect = { rg1X, camY, kGaugeW, camH };

    int rg2X = rg1X + kGaugeW + kGaugeGap;
    rightGauge2Rect = { rg2X, camY, kGaugeW, camH };

    int camBottom = camY + camH;

    // Toolbar
    toolbarRect = { 0, camBottom + kPad / 2, W, kToolbarH };

    int toolbarBottom = toolbarRect.getBottom();

    // Waveform panel (bottom-right, spans height of sequencer area)
    int nRows = (int) voices.size();
    int waveW = juce::jmax (180, (int)(W * 0.26f));
    int waveH = juce::jmax (120, nRows * kSeqRowH / 2);
    waveformRect = { W - waveW - kPad, toolbarBottom + kPad, waveW, waveH };

    // Sequencer occupies left/centre area below toolbar
    sequencerRect = { 0, toolbarBottom + kPad,
                      W - waveW - kPad * 2,
                      nRows * kSeqRowH + 50 };  // +50 for add button

    // Back button (top-right corner)
    backButton.setBounds (W - 90, 8, 80, 28);
}

// ============================================================
//  Mouse
// ============================================================
void PulsefieldPage::mouseDown (const juce::MouseEvent& e)
{
    int mx = e.x, my = e.y;

    // ── Confirm modal takes priority ──────────────────────────────────────
    if (confirmRow >= 0)
    {
        int W = getWidth(), H = getHeight();
        int bw = 340, bh = 160;
        juce::Rectangle<int> box (W/2 - bw/2, H/2 - bh/2, bw, bh);
        juce::Rectangle<int> yesR (box.getCentreX()-90, box.getBottom()-50, 80, 30);
        juce::Rectangle<int> noR  (box.getCentreX()+10, box.getBottom()-50, 80, 30);
        if (yesR.contains (mx, my))
        {
            removeVoice (confirmRow);
            confirmRow = -1;
            resized();
        }
        else if (noR.contains (mx, my))
        {
            confirmRow = -1;
        }
        return;
    }

    // ── Tap tempo modal ────────────────────────────────────────────────────
    if (tapMode)
    {
        if (tapModalRect.contains (mx, my))
        {
            tapMode = false;
            {
                const juce::ScopedLock sl (fxLock);
                fxParams.paused = false;
            }
        }
        else
        {
            double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            tapTimes.push_back (now);
            if ((int) tapTimes.size() > 5)
                tapTimes.erase (tapTimes.begin());
            if ((int) tapTimes.size() >= 2)
            {
                double sum = 0.0;
                for (int i = 1; i < (int) tapTimes.size(); ++i)
                    sum += tapTimes[i] - tapTimes[i-1];
                double avg = sum / (tapTimes.size() - 1);
                bpm = juce::jlimit (40, 240, (int)(60.0 / avg));
            }
        }
        return;
    }

    // ── SET BPM button ─────────────────────────────────────────────────────
    if (tapBpmBtnRect.contains (mx, my))
    {
        tapMode = true;
        tapTimes.clear();
        {
            const juce::ScopedLock sl (fxLock);
            fxParams.paused = true;
        }
        return;
    }

    // ── Step-count buttons ─────────────────────────────────────────────────
    int stepOpts[] = { 8, 16, 32 };
    for (int i = 0; i < 3; ++i)
        if (stepBtnRects[i].contains (mx, my)) { activeSteps = stepOpts[i]; return; }

    // ── Add voice button ───────────────────────────────────────────────────
    if (addVoiceBtnRect.contains (mx, my))
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Select drum sample", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.aiff;*.mp3");

        chooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    loadVoice (result);
                    juce::MessageManager::callAsync ([this] { resized(); repaint(); });
                }
            });
        return;
    }

    // ── Delete buttons ─────────────────────────────────────────────────────
    for (int i = 0; i < delBtnRects.size(); ++i)
        if (delBtnRects[i].contains (mx, my)) { confirmRow = i; return; }

    // ── Reverb SIZE slider ──────────────────────────────────────────────────
    if (reverbSizeRect.contains (mx, my))
    {
        currentDrag = Drag::ReverbSize;
        dragStartY  = my;
        return;
    }

    // ── Reverb WET slider ──────────────────────────────────────────────────
    if (reverbWetRect.contains (mx, my))
    {
        currentDrag = Drag::ReverbWet;
        dragStartY  = my;
        return;
    }

    // ── Per-row volume sliders ──────────────────────────────────────────────
    for (int i = 0; i < volSliderRects.size(); ++i)
    {
        if (volSliderRects[i].contains (mx, my))
        {
            currentDrag = Drag::Volume;
            dragRow     = i;
            dragStartY  = my;
            return;
        }
    }

    // ── Sequencer step cells ───────────────────────────────────────────────
    if (seqCellW > 0)
    {
        int col = (mx - seqLeftX) / seqCellW;
        int row = (my - seqTopY)  / seqRowH;
        const juce::ScopedLock sl (voiceLock);
        int nRows = (int) voices.size();
        if (row >= 0 && row < nRows && col >= 0 && col < activeSteps)
        {
            auto cell = juce::Rectangle<int> (seqLeftX + col * seqCellW + 2,
                                               seqTopY  + row * seqRowH  + 5,
                                               seqCellW - 4, seqRowH - 12);
            if (cell.contains (mx, my) && col < (int) grid[row].size())
                grid[row][col] = ! grid[row][col];
        }
    }
}

void PulsefieldPage::mouseDrag (const juce::MouseEvent& e)
{
    int my = e.y;

    if (currentDrag == Drag::ReverbSize && reverbSizeRect.getHeight() > 0)
    {
        float norm = 1.0f - (float)(my - reverbSizeRect.getY()) / (float)reverbSizeRect.getHeight();
        reverbSizeVal = juce::jlimit (0.5f, 3.0f, 0.5f + norm * (3.0f - 0.5f));
    }
    else if (currentDrag == Drag::ReverbWet && reverbWetRect.getHeight() > 0)
    {
        float norm = 1.0f - (float)(my - reverbWetRect.getY()) / (float)reverbWetRect.getHeight();
        reverbWetVal = juce::jlimit (0.0f, 1.0f, norm);
    }
    else if (currentDrag == Drag::Volume && dragRow >= 0
             && dragRow < (int) rowVolumes.size()
             && volSliderRects[dragRow].getHeight() > 0)
    {
        float norm = 1.0f - (float)(my - volSliderRects[dragRow].getY())
                          / (float)volSliderRects[dragRow].getHeight();
        rowVolumes[dragRow] = juce::jlimit (0.0f, 1.0f, norm);
    }
}

void PulsefieldPage::mouseUp (const juce::MouseEvent&)
{
    currentDrag = Drag::None;
    dragRow     = -1;
}