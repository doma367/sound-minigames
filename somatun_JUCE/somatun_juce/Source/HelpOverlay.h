#pragma once
#include <JuceHeader.h>

class MainComponent;

class HelpOverlay : public juce::Component
{
public:
    HelpOverlay(MainComponent& mc);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MainComponent& mainComponent;

    juce::Label titleLabel;
    juce::Label bodyLabel;
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpOverlay)
};