#pragma once
#include <JuceHeader.h>

// Forward declarations
class LandingPage;
class SettingsPage;
class HelpOverlay;

//==============================================================================
class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Call these to switch screens
    void showLanding();
    void showSettings();
    void showHelp();
    void hideHelp();

private:
    std::unique_ptr<LandingPage>   landingPage;
    std::unique_ptr<SettingsPage>  settingsPage;
    std::unique_ptr<HelpOverlay>   helpOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};