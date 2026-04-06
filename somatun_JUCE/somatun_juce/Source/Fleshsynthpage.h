#pragma once
#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

// ============================================================
//  Shared pose data exchanged between camera thread and UI
// ============================================================
struct BodyLandmark
{
    float x { 0.0f };
    float y { 0.0f };
};

struct PoseFrame
{
    // Indices match MediaPipe: 11=LS,12=RS,13=LE,14=RE,15=LW,16=RW, 7=LE(ar),8=RE(ar)
    static constexpr int LEFT_EAR       =  7;
    static constexpr int RIGHT_EAR      =  8;
    static constexpr int LEFT_SHOULDER  = 11;
    static constexpr int RIGHT_SHOULDER = 12;
    static constexpr int LEFT_ELBOW     = 13;
    static constexpr int RIGHT_ELBOW    = 14;
    static constexpr int LEFT_WRIST     = 15;
    static constexpr int RIGHT_WRIST    = 16;

    BodyLandmark lm[33] {};
    bool valid { false };
};

// ============================================================
//  Wave display sub-component
// ============================================================
class WaveDisplay : public juce::Component
{
public:
    void setWave (const std::vector<float>& w)
    {
        const juce::ScopedLock sl (lock);
        wave = w;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff000000));
        g.setColour (juce::Colour (0x22ff3333));
        for (int x = 0; x < getWidth(); x += 40)
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        for (int y = 0; y < getHeight(); y += 40)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        const juce::ScopedLock sl (lock);
        if (wave.empty()) return;

        juce::Path path;
        float cx = (float) getWidth();
        float cy = (float) getHeight() * 0.5f;
        float amp = cy * 0.85f;

        for (int i = 0; i < (int) wave.size(); ++i)
        {
            float x = (float) i / (float)(wave.size() - 1) * cx;
            float y = cy - wave[i] * amp;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        g.setColour (juce::Colour (0xff00ffff));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

private:
    std::vector<float> wave;
    juce::CriticalSection lock;
};

// ============================================================
//  Camera frame display — receives frames pushed from VideoReceiver
// ============================================================
class CameraView : public juce::Component
{
public:
    CameraView() = default;

    void pushFrame (const juce::Image& img)
    {
        {
            const juce::ScopedLock sl (lock);
            latest  = img;
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
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }

        if (! hasFrame)
        {
            // Faint scan lines while waiting
            g.setColour (juce::Colour (0x0aff3333));
            for (int y = 0; y < getHeight(); y += 16)
                g.drawHorizontalLine (y, 0.0f, (float) getWidth());

            // Loading text bottom-left — disappears the moment feed arrives
            g.setColour (juce::Colour (0x66ff3333));
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
//  VideoReceiver — background thread that reads JPEG frames
//  from somatun_vision.py over a TCP socket on localhost:9001.
//
//  Frame format (written by Python's VideoServer):
//    [4 bytes big-endian uint32 = JPEG length] [JPEG bytes]
// ============================================================
class VideoReceiver : public juce::Thread
{
public:
    explicit VideoReceiver (CameraView& view)
        : juce::Thread ("VideoReceiver"), cameraView (view) {}

    ~VideoReceiver() override { stopReceiver(); }

    void startReceiver()
    {
        startThread();
    }

    void stopReceiver()
    {
        // Signal the thread and force the blocking socket read to unblock
        signalThreadShouldExit();
        if (auto* s = streamSocket.load())
            s->close();
        stopThread (2000);
    }

private:
    // ---- Helpers ----
    static bool readExact (juce::StreamingSocket& sock, void* buf, int numBytes)
    {
        auto* p = static_cast<char*> (buf);
        int remaining = numBytes;
        while (remaining > 0)
        {
            int got = sock.read (p, remaining, true);
            if (got <= 0) return false;
            p         += got;
            remaining -= got;
        }
        return true;
    }

    // ---- Thread entry point ----
    void run() override
    {
        constexpr int kPort           = 9001;
        constexpr int kRetryMs        = 1000;
        constexpr int kMaxFrameBytes  = 4 * 1024 * 1024;   // 4 MB sanity cap

        while (! threadShouldExit())
        {
            auto sock = std::make_unique<juce::StreamingSocket>();
            streamSocket = sock.get();

            DBG ("[VideoReceiver] connecting to 127.0.0.1:" + juce::String (kPort));

            if (! sock->connect ("127.0.0.1", kPort, 2000))
            {
                streamSocket = nullptr;
                wait (kRetryMs);
                continue;
            }

            DBG ("[VideoReceiver] connected");

            while (! threadShouldExit())
            {
                // Read 4-byte big-endian length
                uint8_t hdr[4];
                if (! readExact (*sock, hdr, 4))
                    break;

                uint32_t len = ((uint32_t) hdr[0] << 24) | ((uint32_t) hdr[1] << 16)
                             | ((uint32_t) hdr[2] <<  8) |  (uint32_t) hdr[3];

                if (len == 0 || len > (uint32_t) kMaxFrameBytes)
                    break;

                // Read JPEG payload
                juce::MemoryBlock mb (len);
                if (! readExact (*sock, mb.getData(), (int) len))
                    break;

                // Decode JPEG → juce::Image
                juce::MemoryInputStream mis (mb, false);
                juce::JPEGImageFormat   fmt;
                juce::Image img = fmt.decodeImage (mis);

                if (img.isValid())
                {
                    // Deliver to CameraView on the message thread
                    juce::MessageManager::callAsync ([this, img]()
                    {
                        cameraView.pushFrame (img);
                    });
                }
            }

            streamSocket = nullptr;
            DBG ("[VideoReceiver] disconnected, retrying...");
            wait (kRetryMs);
        }
    }

    CameraView& cameraView;
    std::atomic<juce::StreamingSocket*> streamSocket { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoReceiver)
};

// ============================================================
//  OverlayView — transparent component that sits above CameraView
//  in the Z-order so trail, skeleton overlay, and particles are
//  always rendered on top of the camera feed.
// ============================================================
class OverlayView : public juce::Component
{
public:
    std::function<void(juce::Graphics&)> onPaint;

    void paint (juce::Graphics& g) override
    {
        if (onPaint) onPaint (g);
    }
};

// ============================================================
//  FleshSynthPage
// ============================================================
class FleshSynthPage : public juce::Component,
                       private juce::Timer,
                       private juce::AudioIODeviceCallback,
                       private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit FleshSynthPage (MainComponent& mc);
    ~FleshSynthPage() override;

    void start();   // launch pose tracker + audio
    void stop();    // shut down pose tracker + audio

    // juce::Component
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // OSC receiver callback
    void oscMessageReceived (const juce::OSCMessage& m) override;

private:
    // ---- Timer (UI update @ 40 Hz) ----
    void timerCallback() override;

    // ---- Audio callback ----
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int,
                                           int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    // ---- Internal helpers ----
    void buildWaveFromPose (const PoseFrame& pf, int viewW, int viewH);
    void drawOverlay       (juce::Graphics& g);
    void drawSidebar       (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawSlider        (juce::Graphics& g, const juce::String& label,
                            juce::Rectangle<int> track, float normVal,
                            juce::Colour colour, float displayVal, const juce::String& unit);

    // ---- Pose smoothing ----
    struct SmoothPts { float x[6]{}, y[6]{}; bool init = false; };
    SmoothPts smoothPts;
    void smoothJoints (const float rawX[6], const float rawY[6],
                       float outX[6], float outY[6]);

    // ---- Wave smoothing ----
    std::vector<float> prevWaveNorm;   // previous normalised wave (for smoothing)
    std::vector<float> currentWave;    // final display + audio wave (512 samples, ±1)

    // ---- Audio state ----
    struct AudioState
    {
        double   oscFreq    { 440.0 };
        double   phase      { 0.0  };
        double   sampleRate { 44100.0 };
        float    morphAlpha { 0.08f };
        std::vector<float> playBuf;   // morphed copy used by audio thread
        std::vector<float> snapBuf;   // snapshot pushed from UI thread
        juce::CriticalSection lock;
    } audio;

    // ---- Pitch / tilt ----
    float pitchFactor      { 1.0f };
    float tiltRawSmooth    { 0.0f };
    float baseFreq         { 440.0f };
    float baseFreqSmooth   { 440.0f };
    float oscFreq          { 440.0f };
    bool  tiltInvert       { false };

    // ---- Slider state ----
    enum class DragTarget { None, Freq, Tilt };
    DragTarget dragging { DragTarget::None };

    float freqSliderNorm  { 0.476f };  // maps to 440 Hz on [55,1760]
    float tiltSliderNorm  { 0.25f  };

    juce::Rectangle<int> freqTrackRect;
    juce::Rectangle<int> tiltTrackRect;
    juce::Rectangle<int> toggleRect;

    // ---- Layout rects ----
    juce::Rectangle<int> cameraRect;
    juce::Rectangle<int> wavePanelRect;
    juce::Rectangle<int> sidebarRect;

    // ---- Camera view + video receiver ----
    CameraView    cameraView;
    OverlayView   overlayView;
    VideoReceiver videoReceiver { cameraView };

    // ---- Pose data ----
    PoseFrame latestPose;

    // ---- Overlay joint positions (pixel space inside cameraRect) ----
    struct JointPixel { int x, y; };
    JointPixel jointPx[6] {};
    bool       hasJoints { false };
    JointPixel earPx[2] {};

    // ---- Ghost trail — ring buffer of past skeleton frames ----
    static constexpr int kTrailLen = 18;   // number of ghost frames
    struct TrailFrame
    {
        JointPixel joints[6];
        JointPixel ears[2];
        bool       valid { false };
    };
    TrailFrame trailBuf[kTrailLen] {};
    int        trailHead { 0 };             // index of next write slot

    void pushTrailFrame();
    void drawTrail     (juce::Graphics& g);
    void drawParticles (juce::Graphics& g);

    // ---- Red particle system (spectrum panel) ----
    struct Particle
    {
        float x, y;       // normalised 0..1 within wavePanelRect
        float vx, vy;
        float life;       // 1 → 0
        float size;
    };
    std::vector<Particle> particles;
    juce::Random rng;

    void tickParticles();
    void spawnParticlesFromWave();

    // ---- Harmonic spectrum ----
    static constexpr int kNumBars = 24;
    float spectrumBars[kNumBars] {};

    void updateSpectrum();
    void drawSpectrumPanel (juce::Graphics& g, juce::Rectangle<int> bounds);

    // ---- Audio device manager ----
    juce::AudioDeviceManager deviceManager;

    // ---- Look and feel ----
    SomatunLookAndFeel laf;

    // ---- Back button ----
    juce::TextButton backButton { "BACK" };

    MainComponent& mainComponent;

    // ---- OSC receiver ----
    juce::OSCReceiver osc;
    static constexpr int kNumLandmarks = 33;
    static constexpr int kNumFloats    = kNumLandmarks * 2; // x + y per landmark
    std::atomic<float> oscLandmarks[kNumFloats]{};

    // Constants matching Python prototype
    static constexpr float JOINT_SMOOTH_ALPHA = 0.45f;
    static constexpr float WAVE_SMOOTH_ALPHA  = 0.35f;
    static constexpr float TILT_SMOOTH_ALPHA  = 0.35f;
    static constexpr float TILT_DEADZONE      = 0.008f;
    static constexpr float MAX_TILT           = 0.07f;
    static constexpr float TILT_MAX_RANGE_ST  = 4.0f;
    static constexpr float PITCH_ALPHA_TRACK  = 0.12f;
    static constexpr float PITCH_ALPHA_RELAX  = 0.05f;
    static constexpr float BASE_FREQ_SMOOTH_A = 0.07f;
    static constexpr float SLIDER_MIN_FREQ    = 55.0f;
    static constexpr float SLIDER_MAX_FREQ    = 1760.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FleshSynthPage)
};