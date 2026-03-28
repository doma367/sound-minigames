#pragma once
#include <JuceHeader.h>
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
    static constexpr int LEFT_EAR      =  7;
    static constexpr int RIGHT_EAR     =  8;
    static constexpr int LEFT_SHOULDER = 11;
    static constexpr int RIGHT_SHOULDER= 12;
    static constexpr int LEFT_ELBOW    = 13;
    static constexpr int RIGHT_ELBOW   = 14;
    static constexpr int LEFT_WRIST    = 15;
    static constexpr int RIGHT_WRIST   = 16;

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
//  Camera frame display — shows raw pixel data from camera
// ============================================================
class CameraView : public juce::Component,
                   public juce::CameraDevice::Listener
{
public:
    CameraView() = default;

    // CameraDevice::Listener
    void imageReceived (const juce::Image& img) override
    {
        {
            const juce::ScopedLock sl (lock);
            latest = img.createCopy();
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        const juce::ScopedLock sl (lock);
        if (latest.isValid())
            g.drawImage (latest, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    }

    // Overlay drawing (joints + wave polyline) is painted separately by FleshSynthPage
    juce::Image getLatest()
    {
        const juce::ScopedLock sl (lock);
        return latest;
    }

private:
    juce::Image latest;
    juce::CriticalSection lock;
};

// ============================================================
//  FleshSynthPage
// ============================================================
class FleshSynthPage : public juce::Component,
                       private juce::Timer,
                       private juce::AudioIODeviceCallback
{
public:
    explicit FleshSynthPage (MainComponent& mc);
    ~FleshSynthPage() override;

    void start();   // open camera + audio
    void stop();    // close camera + audio

    // juce::Component
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Called externally with a new pose (from a background tracker thread or stub)
    void updatePose (const PoseFrame& pf);

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
    void drawWavePanel     (juce::Graphics& g, juce::Rectangle<int> bounds);
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

    // ---- Camera ----
    std::unique_ptr<juce::CameraDevice> cameraDevice;
    CameraView cameraView;

    // ---- Pose data ----
    PoseFrame latestPose;
    juce::CriticalSection poseLock;

    // ---- Overlay joint positions (pixel space inside cameraRect) ----
    struct JointPixel { int x, y; };
    JointPixel jointPx[6] {};
    bool       hasJoints { false };
    JointPixel earPx[2] {};

    // ---- Audio device manager ----
    juce::AudioDeviceManager deviceManager;

    // ---- Look and feel ----
    SomatunLookAndFeel laf;

    // ---- Back button ----
    juce::TextButton backButton { "BACK" };

    MainComponent& mainComponent;

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