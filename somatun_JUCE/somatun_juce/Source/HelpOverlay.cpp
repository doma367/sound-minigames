#include "HelpOverlay.h"
#include "MainComponent.h"

HelpOverlay::HelpOverlay(MainComponent& mc) : mainComponent(mc)
{
    titleLabel.setText("ABOUT SOMATUN", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff3333));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    bodyLabel.setText(
        "SOMATUN is a gesture-driven audio playground.\n"
        "Your body is the instrument.\n\n"
        "FLESHSYNTH  -  Six tracked body points form a polyline\n"
        "resampled into a live waveform buffer.\n\n"
        "PULSEFIELD  -  Step sequencer + body FX.\n"
        "Draw beats, warp with motion.\n\n"
        "DUALCAST  -  Two hands trigger and hold sounds.\n"
        "Span controls filter. Height controls volume.\n\n"
        "Go to Settings to choose your camera and audio output.",
    juce::dontSendNotification);
    
    bodyLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    bodyLabel.setColour(juce::Label::textColourId, juce::Colour(0xaae8e8e8));
    bodyLabel.setJustificationType(juce::Justification::topLeft);
    bodyLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(bodyLabel);

    closeButton.setButtonText("CLOSE");
    closeButton.onClick = [this] { mainComponent.hideHelp(); };
    addAndMakeVisible(closeButton);
}

void HelpOverlay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark panel
    g.setColour(juce::Colour(0xf0080808));
    g.fillRect(bounds);

    // Red border
    g.setColour(juce::Colour(0x66ff3333));
    g.drawRect(bounds, 1.0f);

    // Top glow line
    juce::ColourGradient grad(juce::Colours::transparentBlack, bounds.getX(), bounds.getY(),
                               juce::Colours::transparentBlack, bounds.getRight(), bounds.getY(), false);
    grad.addColour(0.2, juce::Colour(0x55ff3333));
    grad.addColour(0.5, juce::Colour(0xffff3333));
    grad.addColour(0.8, juce::Colour(0x55ff3333));
    g.setGradientFill(grad);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);

    // Corner brackets
    g.setColour(juce::Colour(0xaaff3333));
    g.drawLine(bounds.getX(), bounds.getY(), bounds.getX() + 16, bounds.getY(), 1.5f);
    g.drawLine(bounds.getX(), bounds.getY(), bounds.getX(), bounds.getY() + 16, 1.5f);
    g.drawLine(bounds.getRight() - 16, bounds.getBottom(), bounds.getRight(), bounds.getBottom(), 1.5f);
    g.drawLine(bounds.getRight(), bounds.getBottom() - 16, bounds.getRight(), bounds.getBottom(), 1.5f);
}

void HelpOverlay::resized()
{
    auto area = getLocalBounds().reduced(28);
    titleLabel.setBounds(area.removeFromTop(40));
    area.removeFromTop(8);
    closeButton.setBounds(area.removeFromBottom(32).removeFromLeft(90));
    area.removeFromBottom(10);
    bodyLabel.setBounds(area);
}