#pragma once
#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>
#include <functional>

class LandingPage;
class SettingsPage;
class HelpOverlay;
class FleshSynthPage;
class DualcastPage;
class PulsefieldPage;

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    // Navigation
    void showLanding();
    void showSettings();
    void hideSettings();
    void showHelp();
    void hideHelp();
    void launchMode (int cardIndex);
    void launchVisionProcess();

    // OSC routing — pages register callbacks, MainComponent owns the port
    void setPoseCallback  (std::function<void(const juce::OSCMessage&)> fn);
    void setHandsCallback (std::function<void(const juce::OSCMessage&)> fn);
    void clearOSCCallbacks();

private:
    void oscMessageReceived (const juce::OSCMessage& m) override;

    std::unique_ptr<LandingPage>    landingPage;
    std::unique_ptr<SettingsPage>   settingsPage;
    std::unique_ptr<HelpOverlay>    helpOverlay;
    std::unique_ptr<FleshSynthPage> fleshSynthPage;
    std::unique_ptr<DualcastPage>   dualcastPage;
    std::unique_ptr<PulsefieldPage> pulsefieldPage;

    juce::ChildProcess  visionProcess;

    // Single shared OSC receiver — bound once, lives forever
    juce::OSCReceiver   sharedOSC;
    bool                oscBound { false };

    // Active page callbacks — only one page active at a time
    std::function<void(const juce::OSCMessage&)> poseCallback;
    std::function<void(const juce::OSCMessage&)> handsCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};