#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

class HelpOverlay : public juce::Component
{
public:
    HelpOverlay(MainComponent& mc);
    ~HelpOverlay() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MainComponent& mainComponent;

    SomatunLookAndFeel laf;

    juce::Label titleLabel;
    juce::Label bodyLabel;
    juce::TextButton closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpOverlay)
};