#pragma once
#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>
#include <vector>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "SomatunLookAndFeel.h"

class MainComponent;

// ============================================================
//  CameraView — receives frames pushed from VideoReceiver
// ============================================================
class PFCameraView : public juce::Component
{
public:
    PFCameraView() = default;

    void pushFrame (const juce::Image& img)
    {
        {
            const juce::ScopedLock sl (lock);
            latest   = img;
            hasFrame = true;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
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
            g.setColour (juce::Colour (0x0a00ff44));
            for (int y = 0; y < getHeight(); y += 16)
                g.drawHorizontalLine (y, 0.0f, (float) getWidth());
            g.setColour (juce::Colour (0x6600dc69));
            g.setFont   (juce::Font (juce::FontOptions().withHeight (11.0f)));
            g.drawText  ("// CAMERA FEED LOADING...",
                         12, getHeight() - 24, getWidth() - 24, 18,
                         juce::Justification::centredLeft);
        }
    }

private:
    juce::Image      latest;
    bool             hasFrame { false };
    juce::CriticalSection lock;
};

// ============================================================
//  PFVideoReceiver — TCP/JPEG protocol
// ============================================================
class PFVideoReceiver : public juce::Thread
{
public:
    explicit PFVideoReceiver (PFCameraView& view)
        : juce::Thread ("PFVideoReceiver"), cameraView (view) {}

    ~PFVideoReceiver() override { stopReceiver(); }

    void startReceiver() { startThread(); }
    void stopReceiver()
    {
        signalThreadShouldExit();
        if (auto* s = streamSocket.load()) s->close();
        stopThread (2000);
    }

private:
    static bool readExact (juce::StreamingSocket& sock, void* buf, int numBytes)
    {
        auto* p = static_cast<char*> (buf);
        int remaining = numBytes;
        while (remaining > 0)
        {
            int got = sock.read (p, remaining, true);
            if (got <= 0) return false;
            p += got; remaining -= got;
        }
        return true;
    }

    void run() override
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

                uint32_t len = ((uint32_t) hdr[0] << 24) | ((uint32_t) hdr[1] << 16)
                             | ((uint32_t) hdr[2] <<  8) |  (uint32_t) hdr[3];

                if (len == 0 || len > (uint32_t) kMaxFrameBytes) break;

                juce::MemoryBlock mb (len);
                if (! readExact (*sock, mb.getData(), (int) len)) break;

                juce::MemoryInputStream mis (mb, false);
                juce::JPEGImageFormat   fmt;
                juce::Image img = fmt.decodeImage (mis);

                if (img.isValid())
                {
                    juce::MessageManager::callAsync ([this, img]()
                    {
                        cameraView.pushFrame (img);
                    });
                }
            }

            streamSocket = nullptr;
            wait (kRetryMs);
        }
    }

    PFCameraView& cameraView;
    std::atomic<juce::StreamingSocket*> streamSocket { nullptr };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFVideoReceiver)
};

struct HandLandmarks {
    float x[21] {};
    float y[21] {};
    bool  isRight { false };
    bool  valid   { false };
};

struct HandsFrame {
    HandLandmarks hands[2];
    int           numHands { 0 };
};

struct DrumVoice {
    juce::String        name;
    std::vector<float>  samples;
    float               volume { 0.8f };
    bool                isBuiltIn { true };
};

struct ActiveSound {
    const float* data     { nullptr };
    int          length   { 0 };
    int          position { 0 };
    float        volume   { 1.0f };
};

static constexpr int PF_MAX_STEPS    = 32;

class PulsefieldPage : public juce::Component,
                       private juce::Timer,
                       private juce::AudioIODeviceCallback
{
public:
    explicit PulsefieldPage (MainComponent& mc);
    ~PulsefieldPage() override;

    void start();
    void stop();

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int,
                                           int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}

    std::vector<float> synthesiseKick  (double sr) const;
    std::vector<float> synthesiseSnare (double sr) const;
    std::vector<float> synthesiseHat   (double sr) const;

    void addVoice  (const juce::String& name, std::vector<float> samples, bool builtIn = false);
    bool loadVoice (const juce::File& file);   
    void removeVoice (int row);

    void decodeHandsOSC (const juce::OSCMessage& m);
    void updateGestureState();

    int  getSyncedStep (int activeSteps) const;

    void drawBackground     (juce::Graphics&);
    void drawCameraSection  (juce::Graphics&, juce::Rectangle<int> camBounds);
    void drawSideGauge      (juce::Graphics&, juce::Rectangle<int> bounds,
                             float value, juce::Colour colour, const juce::String& label);
    void drawHandSkeleton   (juce::Graphics&, juce::Rectangle<int> camBounds,
                             const HandLandmarks& hand);
    void drawToolbar        (juce::Graphics&, juce::Rectangle<int> bounds);
    void drawSequencer      (juce::Graphics&, juce::Rectangle<int> bounds);
    void drawWaveformPanel  (juce::Graphics&, juce::Rectangle<int> bounds);
    void drawTapModal       (juce::Graphics&);
    void drawConfirmModal   (juce::Graphics&);

    juce::Rectangle<int> cameraRect, leftGaugeRect, rightGauge1Rect, rightGauge2Rect;
    juce::Rectangle<int> toolbarRect, sequencerRect, waveformRect;

    juce::Rectangle<int> stepBtnRects[3];      
    juce::Rectangle<int> tapBpmBtnRect;
    juce::Rectangle<int> addVoiceBtnRect;
    juce::Array<juce::Rectangle<int>> delBtnRects;   
    juce::Array<juce::Rectangle<int>> volSliderRects; 

    int seqTopY { 0 }, seqLeftX { 0 }, seqCellW { 0 }, seqRowH { 54 };

    juce::CriticalSection voiceLock;
    std::vector<DrumVoice>         voices;
    std::vector<std::vector<bool>> grid;   

    std::vector<ActiveSound> activeSounds;

    static constexpr int kWaveBufSize = 2048;
    float   waveBuf[kWaveBufSize] {};
    int     waveBufPtr { 0 };
    juce::CriticalSection waveLock;

    struct HitEnv { const float* data; int len; int pos; };
    juce::CriticalSection envLock;
    std::vector<HitEnv>   hitEnvelopes;

    double sampleRate { 44100.0 };
    int    samplesPerStep { 0 }, nextTrigger { 0 };
    long long totalSamples { 0 };
    int    outputLatencySamples { 0 }, activeSteps { 8 }, bpm { 120 };

    HandsFrame latestHands;
    juce::CriticalSection handsLock;

    float handYSmooth { 0.5f }, delayTimeSmooth { 0.1f }, delayFeedbackSmooth { 0.0f };   
    bool  isFist { false }, delayLocked { false };  

    int   peaceCount { 0 }, peaceCooldown { 0 };
    static constexpr int kPeaceConfirmFrames  = 8;
    static constexpr int kPeaceCooldownFrames = 20;
    static constexpr float kSmoothK = 0.18f;

    float filterState { 0.0f };
    static constexpr int kDelaySamples = 44100 * 2;
    std::vector<float>   delayBuf;
    int                  delayPtr { 0 };

    // FIX: Initialized inside header to satisfy constexpr requirements
    static constexpr int kCombBase[4] = { 1557, 1617, 1491, 1422 };
    static constexpr int kAPBase[2]   = { 225, 556 };

    static constexpr int kCombBufSize = 6000;   
    static constexpr int kAPBufSize   = 2000;

    float combBufs[4][kCombBufSize] {};
    float apBufs  [2][kAPBufSize]   {};
    int   combPtrs[4] {}, apPtrs[2] {};

    struct FXParams {
        float filterAlpha { 0.95f }, delayTimeSec { 0.1f }, delayFeedback { 0.0f };
        float reverbSize { 1.0f }, reverbWet { 0.0f };
        bool  bitCrushOn { false }, paused { false };
    };
    FXParams fxParams;
    juce::CriticalSection fxLock;

    float reverbSizeVal { 1.0f }, reverbWetVal { 0.0f };
    juce::Rectangle<int> reverbSizeRect, reverbWetRect;

    std::vector<float> rowVolumes;
    std::vector<bool>  volDragging;
    std::vector<float> stepFlashes;   

    bool  tapMode { false };
    std::vector<double> tapTimes;
    juce::Rectangle<int> tapModalRect;

    int   confirmRow { -1 };   
    enum class Drag { None, ReverbSize, ReverbWet, Volume };
    Drag   currentDrag { Drag::None };
    int    dragRow { -1 }, dragStartY { 0 };

    PFCameraView    cameraView;
    PFVideoReceiver videoReceiver { cameraView };
    juce::AudioDeviceManager deviceManager;
    juce::TextButton backButton { "BACK" };
    SomatunLookAndFeel laf;

    MainComponent& mainComponent;

    static const juce::Colour kColBg, kColPanel, kColActive, kColInactive, kColFilter, kColTime, 
                              kColTimeLock, kColFeedback, kColReverb, kColWaveform, kColVol, 
                              kColAdd, kColDel, kColBorder, kColLabel, kColToolbar;

    static constexpr int kNumHandConns = 23;
    static const int kHandConns[kNumHandConns][2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulsefieldPage)
};