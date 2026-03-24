#pragma once
#include <JuceHeader.h>

class MainComponent;

class SettingsPage : public juce::Component
{
public:
    SettingsPage(MainComponent& mc);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MainComponent& mainComponent;

    juce::Label titleLabel;
    juce::Label cameraLabel;
    juce::Label audioLabel;

    juce::ComboBox cameraComboBox;
    juce::ComboBox audioComboBox;

    juce::TextButton backButton { "Back" };

    juce::AudioDeviceManager deviceManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};