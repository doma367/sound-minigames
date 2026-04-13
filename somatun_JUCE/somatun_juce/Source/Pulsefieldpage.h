#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

// ============================================================
//  Hand landmark data (from /hands OSC)
//
//  OSC /hands layout:
//    [0]         = numHands (0..2)
//    per hand (stride = 43):
//      [base+0]  = label (0.0=Left, 1.0=Right)
//      [base+1..42] = lm[0].x, lm[0].y, ..., lm[20].x, lm[20].y
// ============================================================
struct HandLandmark { float x { 0.5f }; float y { 0.5f }; };

struct HandFrame
{
    static constexpr int NUM_LANDMARKS = 21;

    struct Hand
    {
        bool          valid  { false };
        bool          isRight { false };
        HandLandmark  lm[NUM_LANDMARKS] {};
    };

    Hand hands[2] {};
    int  numHands { 0 };
};

// ============================================================
//  GestureState — mirrors vision.py GestureState
// ============================================================
struct GestureState
{
    // Right hand
    float handY         { 0.5f };   // index tip height → filter cutoff
    bool  isFist        { false };  // pinch → bit-crush on/off
    // Left hand
    float delayTimeVal  { 0.1f };   // hand tilt → echo time
    float delayFeedback { 0.0f };   // wrist height → delay feedback
    bool  delayLocked   { false };  // peace sign toggle

    // Raw (pre-smoothed) for gauge display
    float rawHandY        { 0.5f };
    float rawDelayTime    { 0.1f };
    float rawDelayFeedback{ 0.0f };
};

// ============================================================
//  VideoReceiver — TCP JPEG stream from somatun_vision.py
// ============================================================
class PFCameraView;

class PFVideoReceiver : public juce::Thread
{
public:
    explicit PFVideoReceiver (PFCameraView& view);
    ~PFVideoReceiver() override { stopReceiver(); }

    void startReceiver();
    void stopReceiver();

private:
    static bool readExact (juce::StreamingSocket& sock, void* buf, int numBytes);
    void run() override;

    PFCameraView& cameraView;
    std::atomic<juce::StreamingSocket*> streamSocket { nullptr };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFVideoReceiver)
};

// ============================================================
//  PFCameraView — displays JPEG frames from somatun_vision.py
// ============================================================
class PFCameraView : public juce::Component
{
public:
    PFCameraView() = default;

    void pushFrame (const juce::Image& img);
    void paint (juce::Graphics& g) override;

private:
    juce::Image           latest;
    bool                  hasFrame { false };
    juce::CriticalSection lock;
};

// ============================================================
//  OverlayView — transparent child drawn above PFCameraView
// ============================================================
class PFOverlayView : public juce::Component
{
public:
    std::function<void(juce::Graphics&)> onPaint;
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
};

// ============================================================
//  PulseFieldPage
//
//  Gesture-controlled drum sequencer, ported from:
//    main.py  — sequencer logic + event handling
//    audio.py — DrumMachine (filter, delay, reverb, bit-crush)
//    vision.py — HandTracker gesture extraction
//    ui.py    — layout + drawing
//
//  OSC port 9000:
//    /hands  → hand landmark data → gesture state
//  TCP port 9001:
//    JPEG frames → camera preview
// ============================================================
class PulseFieldPage : public juce::Component,
                       private juce::Timer,
                       private juce::AudioIODeviceCallback
{
public:
    explicit PulseFieldPage (MainComponent& mc);
    ~PulseFieldPage() override;

    void start();
    void stop();

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Called by MainComponent's OSC router (message thread)
    void handleHandsMessage (const juce::OSCMessage& m);

private:
    // ── Timer ────────────────────────────────────────────────
    void timerCallback() override;

    // ── Audio ────────────────────────────────────────────────
    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* dev) override;
    void audioDeviceStopped()     override {}

    // ── Gesture extraction ───────────────────────────────────
    void updateGesture (const HandFrame& hf);

    // ── Drawing helpers ──────────────────────────────────────
    void drawBackground      (juce::Graphics& g);
    void drawToolbar         (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawSideGauge       (juce::Graphics& g, juce::Rectangle<int> bounds,
                              float normVal, juce::Colour colour,
                              const juce::String& label);
    void drawSequencer       (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawWaveformPanel   (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawVectorscope     (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawHandSkeleton    (juce::Graphics& g);
    void drawCamOverlays     (juce::Graphics& g);
    void drawConfirmModal    (juce::Graphics& g);
    void drawTapModal        (juce::Graphics& g);
    void drawGaugeSidebar    (juce::Graphics& g, juce::Rectangle<int> bounds);

    // ── Hit-test helpers ─────────────────────────────────────
    int   hitStepCell    (juce::Point<int> pos) const;  // returns packed (row<<8|col) or -1
    int   hitStepBtn     (juce::Point<int> pos) const;  // returns step count (8/16/32) or 0
    bool  hitAddBtn      (juce::Point<int> pos) const;
    int   hitDelBtn      (juce::Point<int> pos) const;  // returns row or -1
    bool  hitTapBtn      (juce::Point<int> pos) const;
    bool  hitConfirmYes  (juce::Point<int> pos) const;
    bool  hitConfirmNo   (juce::Point<int> pos) const;

    // ── Audio engine helpers ──────────────────────────────────
    // All synthesis runs on the audio thread via audioDeviceIOCallback.
    // State is shared via atomic / lock-protected structs below.

    juce::AudioSampleBuffer synthKick();
    juce::AudioSampleBuffer synthSnare();
    juce::AudioSampleBuffer synthHat();

    // ── Sequencer grid ────────────────────────────────────────
    static constexpr int MAX_ROWS  = 16;
    static constexpr int MAX_STEPS = 32;

    bool  grid[MAX_ROWS][MAX_STEPS] {};
    int   numRows    { 3 };
    int   activeSteps{ 8 };

    // ── Voice data ────────────────────────────────────────────
    struct Voice
    {
        juce::String         name;
        std::vector<float>   samples;  // mono float32, 44100 Hz
        float                volume { 0.8f };
    };

    juce::CriticalSection    voiceLock;
    std::vector<Voice>       voices;

    // Active sound playback entries (audio thread only)
    struct ActiveSound
    {
        int   voiceIdx;
        int   samplePos;
        int   startOffset;
        float volume;
    };
    std::vector<ActiveSound> activeSounds;   // audio thread only

    // ── Per-row volume sliders ────────────────────────────────
    struct VSlider
    {
        juce::Rectangle<int> rect;
        float value   { 0.8f };
        bool  dragging{ false };
    };
    std::array<VSlider, MAX_ROWS> volSliders;

    // ── Reverb sliders ────────────────────────────────────────
    struct HSlider
    {
        juce::Rectangle<int> rect;
        float value { 0.0f };
        float minV  { 0.0f };
        float maxV  { 1.0f };
        bool  dragging { false };
        float normVal() const { return (value - minV) / (maxV - minV); }
    };
    HSlider sliderSize;   // reverb size  0.5..3.0
    HSlider sliderWet;    // reverb wet   0.0..1.0

    enum class DragTarget { None, VSlider, ReverbSize, ReverbWet };
    DragTarget dragTarget { DragTarget::None };
    int        dragVRow   { -1 };

    // ── Sequencer layout cache (updated in resized / draw) ───
    juce::Rectangle<int> cameraRect;
    juce::Rectangle<int> toolbarRect;
    juce::Rectangle<int> sequencerRect;
    juce::Rectangle<int> waveformRect;
    juce::Rectangle<int> vectorscopeRect;
    juce::Rectangle<int> gaugeSidebarRect;

    // Computed per-draw (used for hit-testing)
    int seqLeft    { 0 };    // x of first step column
    int seqCellW   { 0 };    // width of each step cell
    int seqRowH    { 54 };   // height of each row

    // Step / tap / confirm button rects (filled each paint)
    juce::Rectangle<int> stepBtnRects[3];          // [8, 16, 32]
    juce::Rectangle<int> addBtnRect;
    juce::Rectangle<int> delBtnRects[MAX_ROWS];
    juce::Rectangle<int> tapBtnRect;
    juce::Rectangle<int> confirmYesRect;
    juce::Rectangle<int> confirmNoRect;

    // ── UI state ──────────────────────────────────────────────
    bool tapMode      { false };
    bool isPaused     { false };
    int  confirmRow   { -1 };  // row awaiting delete confirmation (-1 = none)

    // Tap tempo
    std::vector<double> tapTimes;
    int    bpm        { 120 };

    // Step flash (visual playhead glow)
    float stepFlash[MAX_STEPS] {};

    // ── Gesture state ─────────────────────────────────────────
    GestureState gesture;

    // Peace-sign debounce
    int peaceCount    { 0 };
    int peaceCooldown { 0 };
    static constexpr int PEACE_CONFIRM_FRAMES  = 8;
    static constexpr int PEACE_COOLDOWN_FRAMES = 20;

    // ── Audio engine state (audio thread) ────────────────────
    struct AudioEngine
    {
        // Sequencer timing
        double sampleRate         { 44100.0 };
        int    samplesPerStep     { 0 };
        int    nextTrigger        { 0 };
        int    totalSamplesOut    { 0 };
        int    outputLatencySamples { 0 };

        // Smoothed gesture params (audio thread)
        float smoothAlpha      { 1.0f };
        float smoothDelayTime  { 0.1f };
        float smoothDelayFb    { 0.0f };
        float smoothReverbSize { 1.0f };
        float smoothReverbWet  { 0.0f };

        float filterState { 0.0f };

        // Delay
        static constexpr int DELAY_BUF = 44100 * 2;
        std::array<float, DELAY_BUF> delayBuf {};
        int   delayPtr { 0 };

        // Schroeder reverb
        static constexpr int COMB_BASES[4] = { 1557, 1617, 1491, 1422 };
        static constexpr int AP_BASES[2]   = { 225,  556  };
        static constexpr int COMB_MAX = int(1557 * 3.0) + 1;
        static constexpr int AP_MAX   = int(556  * 3.0) + 1;
        float combBufs[4][COMB_MAX] {};
        float apBufs  [2][AP_MAX]   {};
        int   combPtrs[4] {};
        int   apPtrs  [2] {};

        // Waveform ring buffer for oscilloscope + vectorscope
        static constexpr int WAVE_BUF = 2048;
        float waveBuf[WAVE_BUF] {};
        std::atomic<int> wavePtr { 0 };

        // Vectorscope: stores (L, R) pairs where L = delayed, R = current
        static constexpr int VEC_BUF = 1024;  // number of (L,R) pairs
        float vecBufL[VEC_BUF] {};
        float vecBufR[VEC_BUF] {};
        std::atomic<int> vecPtr { 0 };

        // Atomics written by timer (message thread), read by audio thread
        std::atomic<float> atomicHandY        { 0.5f };
        std::atomic<float> atomicDelayTime    { 0.1f };
        std::atomic<float> atomicDelayFb      { 0.0f };
        std::atomic<float> atomicReverbSize   { 1.0f };
        std::atomic<float> atomicReverbWet    { 0.0f };
        std::atomic<bool>  atomicIsFist       { false };
        std::atomic<bool>  atomicIsPaused     { false };
        std::atomic<int>   atomicBpm          { 120 };
        std::atomic<int>   atomicActiveSteps  { 8 };

        // Per-row volumes (guarded by same lock as voices)
        float rowVolumes[MAX_ROWS] {};
    } eng;

    // Waveform snapshot for display (updated each timer tick from ring buf)
    std::vector<float> waveformSnap;
    std::vector<float> vecSnapL;
    std::vector<float> vecSnapR;
    juce::Image vecTrailBuffer;

    // Hit envelopes for oscilloscope flash
    struct HitEnv { int voiceIdx; int pos; };
    juce::CriticalSection hitEnvLock;
    std::vector<HitEnv>   hitEnvelopes;

    // ── OSC landing zone (atomics) ─────────────────────────────
    static constexpr int MAX_HANDS_FLOATS = 1 + 2 * (1 + 21 * 2);  // 87
    std::atomic<float> oscHandsData[MAX_HANDS_FLOATS] {};

    // ── JUCE infrastructure ────────────────────────────────────
    juce::AudioDeviceManager deviceManager;
    SomatunLookAndFeel        laf;
    juce::TextButton          backButton { "BACK" };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;
    void loadAudioFile(juce::File file);

    PFCameraView   cameraView;
    PFOverlayView  overlayView;
    std::unique_ptr<PFVideoReceiver> videoReceiver;

    MainComponent& mainComponent;

    // ── Constants ─────────────────────────────────────────────
    static constexpr float SMOOTH_K            = 0.18f;
    static constexpr float AUDIO_SMOOTH_K      = 0.05f;
    static constexpr int   STEP_OPTIONS[3]     = { 8, 16, 32 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseFieldPage)
};