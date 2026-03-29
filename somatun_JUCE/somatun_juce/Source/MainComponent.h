#pragma once
#include <JuceHeader.h>

class LandingPage;
class SettingsPage;
class HelpOverlay;
class FleshSynthPage;

//==============================================================================
class MainComponent : public juce::Component
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
    void launchMode (int cardIndex);   // 0 = FleshSynth, 1 = Pulsefield, 2 = Dualcast

private:
    std::unique_ptr<LandingPage>    landingPage;
    std::unique_ptr<SettingsPage>   settingsPage;
    std::unique_ptr<HelpOverlay>    helpOverlay;
    std::unique_ptr<FleshSynthPage> fleshSynthPage;

    juce::ChildProcess visionProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};