#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

// ============================================================
//  Dualcast  — Gesture-driven two-voice synthesiser
//
//  OSC port 9000:
//    /hands  → hand landmark data → gesture state
//  TCP port 9001:
//    JPEG frames → camera preview
//
//  RIGHT hand → master volume  (raise = louder)
//  LEFT  hand → pitch + trigger
//      left  half → DRONE voice
//      right half → LEAD  voice
//      pinch + drag up/down → step through scale notes
//      open hand (0.4 s)   → note ON
//      fist      (0.3 s)   → note OFF
// ============================================================

// ── Music theory constants ──────────────────────────────────
static constexpr int DC_NUM_SCALES = 9;
static constexpr int DC_NUM_SOUNDS = 4;

// ── Hand landmark data (shared with PulseFieldPage format) ──
struct DCHandLandmark { float x { 0.5f }; float y { 0.5f }; };

struct DCHandFrame
{
    static constexpr int NUM_LANDMARKS = 21;
    struct Hand
    {
        bool          valid   { false };
        bool          isRight { false };
        DCHandLandmark lm[NUM_LANDMARKS] {};
    };
    Hand hands[2] {};
    int  numHands { 0 };
};

// ============================================================
//  Camera / video components  (reused pattern from PulseField)
// ============================================================
class DCCameraView;

class DCVideoReceiver : public juce::Thread
{
public:
    explicit DCVideoReceiver (DCCameraView& view);
    ~DCVideoReceiver() override { stopReceiver(); }
    void startReceiver();
    void stopReceiver();

private:
    static bool readExact (juce::StreamingSocket& sock, void* buf, int numBytes);
    void run() override;

    DCCameraView& cameraView;
    std::atomic<juce::StreamingSocket*> streamSocket { nullptr };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DCVideoReceiver)
};

class DCCameraView : public juce::Component
{
public:
    DCCameraView() = default;
    void pushFrame (const juce::Image& img);
    void paint (juce::Graphics& g) override;

    // Called on the message thread after each new frame arrives
    std::function<void()> onNewFrame;

    // Pull the latest frame for external compositing
    juce::Image getLatestFrame()
    {
        const juce::ScopedLock sl (lock);
        return latest;
    }

private:
    juce::Image           latest;
    bool                  hasFrame { false };
    juce::CriticalSection lock;
};

class DCOverlayView : public juce::Component
{
public:
    std::function<void(juce::Graphics&)> onPaint;
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
};

// ============================================================
//  Per-voice state
// ============================================================
struct DCVoiceState
{
    int  scaleIdx  { 0 };   // index into SCALE_NAMES
    int  soundIdx  { 1 };   // 0=Sine 1=Organ 2=SoftPad 3=Pluck
    int  octave    { 2 };   // octave offset (0..6)
    bool showScaleMenu { false };
    bool showSoundMenu { false };

    // Current step within scale (0..len-1)
    int   step      { 0 };
    float anchorY   { 0.5f };
    bool  wasPinch  { false };

    // Hold timers (seconds elapsed while condition true)
    double openHoldStart { -1.0 };
    double fistHoldStart { -1.0 };

    // Smoothed palm position (EMA)
    float emaX { 0.5f };
    float emaY { 0.5f };
    bool  emaInit { false };

    // Whether this voice is currently sounding
    bool noteOn { false };
};

// ============================================================
//  DualcastPage
// ============================================================
class DualcastPage : public juce::Component,
                     private juce::Timer,
                     private juce::AudioIODeviceCallback
{
public:
    explicit DualcastPage (MainComponent& mc);
    ~DualcastPage() override;

    void start();
    void stop();

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

    // Called by MainComponent's OSC router (message thread)
    void handleHandsMessage (const juce::OSCMessage& m);

private:
    // ── Timer ──────────────────────────────────────────────────
    void timerCallback() override;

    // ── Audio ──────────────────────────────────────────────────
    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* dev) override;
    void audioDeviceStopped() override {}

    // ── Gesture extraction ─────────────────────────────────────
    void processGesture (const DCHandFrame& hf);

    // ── Drawing helpers ────────────────────────────────────────
    void drawBackground     (juce::Graphics& g);
    void drawToolbar        (juce::Graphics& g, juce::Rectangle<int> tb);
    void drawZones          (juce::Graphics& g, juce::Rectangle<int> playArea);
    void drawNoteGuidelines (juce::Graphics& g, juce::Rectangle<int> zone,
                              const DCVoiceState& vs, bool isDrone);
    void drawHandGlow       (juce::Graphics& g, float nx, float ny,
                              juce::Colour accent, bool noteOn, bool pinch);
    void drawVolumeBar      (juce::Graphics& g, float vol, float nx, float ny);
    void drawHUD            (juce::Graphics& g, juce::Rectangle<int> hud);
    void drawHandSkeleton   (juce::Graphics& g);
    void drawDropdown       (juce::Graphics& g, juce::Rectangle<int> anchor,
                              const juce::StringArray& items, int sel,
                              juce::Colour accent, bool above);
    void drawScaleBtn       (juce::Graphics& g, juce::Rectangle<int> r,
                              const juce::String& label, bool active,
                              juce::Colour accent);
    void drawSoundBtn       (juce::Graphics& g, juce::Rectangle<int> r,
                              const juce::String& label, bool active,
                              juce::Colour accent);

    // ── Hit-testing helpers ────────────────────────────────────
    int  hitScaleBtn  (juce::Point<int> p) const;  // 0=drone 1=lead -1=none
    int  hitSoundBtn  (juce::Point<int> p) const;
    int  hitDropdown  (juce::Point<int> p, int voiceIdx,
                       bool isScale) const;          // returns item index or -1

    // ── Geometry (filled in resized) ───────────────────────────
    juce::Rectangle<int> cameraRect;
    juce::Rectangle<int> toolbarRect;
    juce::Rectangle<int> playRect;
    juce::Rectangle<int> hudRect;

    // Button rects
    juce::Rectangle<int> droneScaleBtn;
    juce::Rectangle<int> droneSoundBtn;
    juce::Rectangle<int> leadScaleBtn;
    juce::Rectangle<int> leadSoundBtn;

    // Dropdown item rects (filled when menu open)
    juce::Array<juce::Rectangle<int>> droneDropRects;
    juce::Array<juce::Rectangle<int>> leadDropRects;

    // ── Application state ──────────────────────────────────────
    DCVoiceState droneState;
    DCVoiceState leadState;

    // Last known hand positions (normalised, for overlay)
    struct HandPos { float x { 0.5f }; float y { 0.5f }; bool valid { false }; bool isRight { false }; bool pinch { false }; };
    HandPos handPosLeft;
    HandPos handPosRight;

    // Animated time (updated each timer tick)
    double animTime { 0.0 };

    // ── OSC landing zone ───────────────────────────────────────
    static constexpr int MAX_HANDS_FLOATS = 1 + 2 * (1 + 21 * 2);  // 87
    std::atomic<float> oscHandsData[MAX_HANDS_FLOATS] {};

    // ── Audio engine ───────────────────────────────────────────
    struct AudioEngine
    {
        double sampleRate { 44100.0 };

        // ── Drone voice ──────────────────────────────────────
        double dronePhase     { 0.0 };
        float  dronePadGain   { 0.0f };  // for SoftPad envelope
        float  droneGain      { 0.0f };  // for Sine/Organ envelope
        bool   dronePrevOn    { false };

        // Karplus-Strong for drone Pluck
        std::vector<float> droneKsBuf;
        int   droneKsPos  { 0 };
        int   droneKsSize { 1 };
        bool  droneKsActive { false };

        // ── Lead voice ────────────────────────────────────────
        double leadPhase     { 0.0 };
        float  leadPadGain   { 0.0f };
        float  leadGain      { 0.0f };
        bool   leadPrevOn    { false };

        std::vector<float> leadKsBuf;
        int   leadKsPos   { 0 };
        int   leadKsSize  { 1 };
        bool  leadKsActive { false };

        // ── Volume envelope ───────────────────────────────────
        float volGain { 0.0f };

        // ── Waveform ring buffer (oscilloscope display) ───────
        static constexpr int WAVE_BUF = 2048;
        float waveBuf[WAVE_BUF] {};
        std::atomic<int> wavePtr { 0 };

        // ── Atomics (message thread → audio thread) ───────────
        std::atomic<float> atomicMasterVol  { 0.0f };
        std::atomic<float> atomicDroneFreq  { 130.81f };
        std::atomic<float> atomicLeadFreq   { 261.63f };
        std::atomic<bool>  atomicDroneOn    { false };
        std::atomic<bool>  atomicLeadOn     { false };
        std::atomic<int>   atomicDroneSound { 1 };
        std::atomic<int>   atomicLeadSound  { 1 };
    } eng;

    // Waveform snapshot for display
    std::vector<float> waveformSnap;

    // ── Constants ──────────────────────────────────────────────
    static constexpr float EMA_ALPHA     = 0.30f;
    static constexpr float OPEN_HOLD_S   = 0.40;   // seconds open hand → note on
    static constexpr float FIST_HOLD_S   = 0.30;   // seconds fist      → note off
    static constexpr float STEP_ZONE     = 0.055f; // normalised Y distance per step
    static constexpr float SPREAD_OPEN   = 0.20f;  // spread > this = open hand
    static constexpr float SPREAD_FIST   = 0.10f;  // spread < this = fist

    // ── JUCE infrastructure ────────────────────────────────────
    juce::AudioDeviceManager deviceManager;
    SomatunLookAndFeel        laf;
    juce::TextButton          backButton { "BACK" };

    DCCameraView   cameraView;
    DCOverlayView  overlayView;
    std::unique_ptr<DCVideoReceiver> videoReceiver;

    MainComponent& mainComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualcastPage)
};