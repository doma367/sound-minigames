#pragma once
#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>
#include "SomatunLookAndFeel.h"

// Re-use VideoReceiver / CameraView from FleshSynthPage.h
// (they're header-only sub-components - just include the shared header)
#include "FleshSynthPage.h"

class MainComponent;

// ============================================================
//  Music-theory constants
// ============================================================
namespace DualcastTheory
{
    // Scale definitions - semitone offsets from root (C)
    struct Scale { const char* name; std::vector<int> semitones; };

    inline const Scale SCALES[] =
    {
        { "Pentatonic",    { 0, 2, 4, 7, 9 } },
        { "Major",         { 0, 2, 4, 5, 7, 9, 11 } },
        { "Nat. Minor",    { 0, 2, 3, 5, 7, 8, 10 } },
        { "Blues",         { 0, 3, 5, 6, 7, 10 } },
        { "Dorian",        { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",      { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",        { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",    { 0, 2, 4, 5, 7, 9, 10 } },
        { "Chromatic",     { 0,1,2,3,4,5,6,7,8,9,10,11 } },
    };
    inline constexpr int NUM_SCALES = 9;

    inline const char* SOUND_NAMES[] = { "Sine", "Organ", "Soft Pad", "Pluck" };
    inline constexpr int NUM_SOUNDS = 4;

    inline const char* NOTE_NAMES[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    static constexpr double C2_HZ = 65.406;

    inline double stepToFreq (int step, const std::vector<int>& scale, int octave)
    {
        int n    = (int) scale.size();
        step     = juce::jlimit (0, n - 1, step);
        int semi = scale[(size_t) step];
        return C2_HZ * std::pow (2.0, (semi + octave * 12) / 12.0);
    }

    inline juce::String freqToNoteName (double freq)
    {
        if (freq <= 0.0) return "---";
        int midi = (int) std::round (12.0 * std::log2 (freq / C2_HZ) + 24);
        if (midi < 0 || midi > 127) return "---";
        return juce::String (NOTE_NAMES[midi % 12]) + juce::String (midi / 12 - 1);
    }
}

// ============================================================
//  Karplus-Strong delay-line string model
// ============================================================
class KarplusStrongVoice
{
public:
    void trigger (double freq, double sampleRate)
    {
        size = juce::jmax (1, (int)(sampleRate / freq));
        if ((int)buf.size() != size)
            buf.resize ((size_t) size);
        for (auto& s : buf) s = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        pos    = 0;
        active = true;
    }

    void release() { active = false; }

    float tick()
    {
        if (!active || buf.empty()) return 0.0f;
        float out  = buf[(size_t) pos];
        int   nxt  = (pos + 1) % size;
        buf[(size_t) pos] = 0.996f * 0.5f * (buf[(size_t) pos] + buf[(size_t) nxt]);
        pos = nxt;
        return out;
    }

private:
    std::vector<float> buf;
    int  pos    { 0 };
    int  size   { 1 };
    bool active { false };
};

// ============================================================
//  Per-voice audio state - Drone and Lead
// ============================================================
struct DcVoice
{
    // UI-controlled (written from message thread, read on audio thread under lock)
    bool   on       { false };
    double freq     { 130.81 };
    int    soundIdx { 1 };   // 0=Sine 1=Organ 2=Pad 3=Pluck

    // Audio-thread internal
    double phase    { 0.0 };
    double padGain  { 0.0 };
    double envGain  { 0.0 };
    bool   prevOn   { false };
    KarplusStrongVoice ks;

    // Generate `frames` samples of this voice at the given sampleRate.
    // Returns a vector of floating-point samples in [-1, 1].
    void generate (float* out, int frames, double sampleRate);
};

// ============================================================
//  DualcastAudioEngine - two voices + master volume
// ============================================================
class DualcastAudioEngine : public juce::AudioIODeviceCallback
{
public:
    DualcastAudioEngine();

    // Thread-safe setters called from the UI thread
    void setDroneOn    (bool v);
    void setDroneFreq  (double f);
    void setDroneSnd   (int i);
    void setLeadOn     (bool v);
    void setLeadFreq   (double f);
    void setLeadSnd    (int i);
    void setMasterVol  (double v);

    // Snapshot of state for UI display (cheap, approximate - no lock needed)
    double getDroneFreq() const  { return drone.freq; }
    double getLeadFreq()  const  { return lead.freq;  }
    bool   isDroneOn()    const  { return drone.on;   }
    bool   isLeadOn()     const  { return lead.on;    }
    double getMasterVol() const  { return masterVol;  }

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* dev) override
    {
        sampleRate = dev->getCurrentSampleRate();
    }
    void audioDeviceStopped() override {}

private:
    juce::CriticalSection lock;
    DcVoice drone;
    DcVoice lead;
    double  masterVol  { 0.0 };
    double  volGain    { 0.0 };   // smoothed
    double  sampleRate { 44100.0 };
};

// ============================================================
//  Per-voice UI + gesture state
// ============================================================
struct DcVoiceState
{
    int  scaleIdx  { 0 };
    int  soundIdx  { 1 };
    int  octave    { 2 };
    int  step      { 0 };     // current scale step
    bool stepInited{ false };

    // Pinch step controller
    bool  wasPinch   { false };
    float anchorY    { 0.0f };

    const std::vector<int>& scale() const
    {
        return DualcastTheory::SCALES[scaleIdx].semitones;
    }
    juce::String scaleName() const { return DualcastTheory::SCALES[scaleIdx].name; }
    juce::String soundName() const { return DualcastTheory::SOUND_NAMES[soundIdx]; }

    double currentFreq() const
    {
        return DualcastTheory::stepToFreq (step, scale(), octave);
    }

    // Returns new frequency after processing pinch drag
    double updateStep (bool pinch, float hy)
    {
        static constexpr float ZONE = 0.055f;
        int total = (int) scale().size();
        if (!stepInited) { step = total / 2; anchorY = hy; stepInited = true; }

        if (pinch)
        {
            if (!wasPinch)
                anchorY = hy;
            else
            {
                float delta = anchorY - hy;   // drag up = positive
                int moved   = (int)(delta / ZONE);
                if (moved != 0)
                {
                    step     = juce::jlimit (0, total - 1, step + moved);
                    anchorY -= moved * ZONE;
                }
            }
        }
        wasPinch = pinch;
        return currentFreq();
    }

    void resetStep() { stepInited = false; }
};

// ============================================================
//  HoldTimer  - fires after a held condition for `threshold` seconds
// ============================================================
class DcHoldTimer
{
public:
    explicit DcHoldTimer (float threshold = 0.35f) : thresh (threshold) {}

    // Call every UI tick; returns true once condition has been held for `thresh` s
    bool update (bool condition, float dtSec)
    {
        if (condition)
        {
            held += dtSec;
            if (held >= thresh) { held = thresh; return true; }
        }
        else
        {
            held = 0.0f;
        }
        return false;
    }

    void reset() { held = 0.0f; }

private:
    float thresh;
    float held { 0.0f };
};

// ============================================================
//  Smoothed EMA per named channel
// ============================================================
class DcEMA
{
public:
    explicit DcEMA (float alpha = 0.30f) : alpha (alpha) {}

    float operator() (int channel, float val)
    {
        if (channel >= (int) v.size())
            v.resize ((size_t)(channel + 1), val);
        v[(size_t) channel] = alpha * val + (1.0f - alpha) * v[(size_t) channel];
        return v[(size_t) channel];
    }

private:
    float alpha;
    std::vector<float> v;
};

// ============================================================
//  DualcastPage
// ============================================================
class DualcastPage : public juce::Component,
                     private juce::Timer,
                     private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit DualcastPage (MainComponent& mc);
    ~DualcastPage() override;

    void start();
    void stop();

    // juce::Component
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

    // OSC callbacks
    void oscMessageReceived (const juce::OSCMessage& m) override;

private:
    // ---- 40 Hz UI tick ----
    void timerCallback() override;

    // ---- Drawing ----
    void drawBackground    (juce::Graphics& g);
    void drawDivider       (juce::Graphics& g);
    void drawNoteGuidelines(juce::Graphics& g, const DcVoiceState& vs,
                            int xLeft, int xRight, juce::Colour accent,
                            bool voiceOn, float nowSec);
    void drawHandGlow      (juce::Graphics& g, int px, int py,
                            juce::Colour accent, bool on, bool pinch);
    void drawVolumeBar     (juce::Graphics& g, float vol, int cx, int ytop);
    void drawHUD           (juce::Graphics& g);
    void drawVoiceControls (juce::Graphics& g, bool isDrone);
    void drawZoneLabel     (juce::Graphics& g, bool isDrone);

    // ---- Button hit-testing ----
    struct BtnRect { juce::Rectangle<int> r; };
    BtnRect droneScaleBtn, droneSoundBtn;
    BtnRect leadScaleBtn,  leadSoundBtn;

    enum class MenuState { None, DroneScale, DroneSound, LeadScale, LeadSound };
    MenuState openMenu { MenuState::None };

    void buildButtonRects();
    void drawButtonWidget (juce::Graphics& g, const juce::String& label,
                           juce::Rectangle<int> r, bool active, juce::Colour accent);
    void drawDropdown (juce::Graphics& g, int count, int selected,
                    juce::Rectangle<int> anchorBtn,
                    juce::Colour accent, bool isScale);

    // ---- Layout ----
    juce::Rectangle<int> cameraRect;
    juce::Rectangle<int> hudRect;

    // ---- Camera / video ----
    CameraView    cameraView;
    VideoReceiver videoReceiver { cameraView };

    // ---- OSC ----
    juce::OSCReceiver osc;

    // Raw pose landmarks from /pose  (33 × 2)
    static constexpr int kNumLm   = 33;
    static constexpr int kNumLmF  = kNumLm * 2;
    std::atomic<float> oscPose[kNumLmF] {};

    // Raw hand data from /hands
    // Layout: numHands(1 float), then per hand: label(0=Left/1=Right, 1 float),
    //         21 × 2 floats for x/y landmarks
    static constexpr int kMaxHands    = 2;
    static constexpr int kHandLmCount = 21;
    static constexpr int kHandFloats  = kMaxHands * (1 + kHandLmCount * 2) + 1;
    std::atomic<float> oscHands[kHandFloats] {};

    // ---- Parsed hand data (message thread only) ----
    struct HandData
    {
        bool  valid  { false };
        bool  isLeft { false };
        float palmX  { 0.0f };  // normalised
        float palmY  { 0.0f };
        float spread { 0.0f };  // avg tip-palm distance
        bool  pinch  { false };
    };
    HandData hands[kMaxHands];
    int      numHands { 0 };

    void parseHandsOSC();

    // ---- Voice state ----
    DcVoiceState droneState;
    DcVoiceState leadState;

    // ---- Hold timers (open hand = note on, fist = note off) ----
    DcHoldTimer droneOpenTimer { 0.40f };
    DcHoldTimer droneFistTimer { 0.30f };
    DcHoldTimer leadOpenTimer  { 0.40f };
    DcHoldTimer leadFistTimer  { 0.30f };

    // ---- EMA smoothers ----
    DcEMA ema { 0.30f };

    // ---- Hand pixel positions for overlay (message thread) ----
    struct HandPx { int x, y; bool valid; bool pinch; bool on; juce::Colour accent; };
    HandPx handPx[kMaxHands] {};

    // ---- Volume ----
    float masterVol  { 0.0f };
    float volSmooth  { 0.0f };

    // ---- Audio engine + device ----
    DualcastAudioEngine audioEngine;
    juce::AudioDeviceManager deviceManager;

    // ---- Look and feel / UI ----
    SomatunLookAndFeel laf;
    juce::TextButton   backButton { "BACK" };

    MainComponent& mainComponent;

    // Colours
    static const juce::Colour DC_BG;
    static const juce::Colour DC_DRONE;    // blue
    static const juce::Colour DC_LEAD;    // green
    static const juce::Colour DC_DIM;
    static const juce::Colour DC_TEXT;

    static constexpr int TOP_Y_OFFSET = 65;   // below button bar
    static constexpr int BOT_Y_OFFSET = 72;   // above HUD

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualcastPage)
};