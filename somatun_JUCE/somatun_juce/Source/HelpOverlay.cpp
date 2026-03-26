#include "HelpOverlay.h"
#include "MainComponent.h"

static const juce::Colour H_ACCENT      { 0xffff3333 };
static const juce::Colour H_ACCENT_DIM  { 0x55ff3333 };
static const juce::Colour H_BORDER_DIM  { 0x33ff3333 };
static const juce::Colour H_BG_DARK     { 0xf2050505 };
static const juce::Colour H_TEXT_PRI    { 0xffe8e8e8 };
static const juce::Colour H_TEXT_DIM    { 0x99e8e8e8 };

HelpOverlay::HelpOverlay(MainComponent& mc) : mainComponent(mc)
{
    setLookAndFeel(&laf);

    titleLabel.setText("// ABOUT SOMATUN", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    titleLabel.setColour(juce::Label::textColourId, H_ACCENT);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    bodyLabel.setText(
        "SOMATUN is a gesture-driven audio playground.\n"
        "Your body is the instrument.\n\n"
        "FLESHSYNTH\n"
        "Six tracked body points form a polyline resampled\n"
        "into a live waveform buffer. Move to sculpt sound.\n\n"
        "PULSEFIELD\n"
        "Step sequencer + body FX.\n"
        "Draw beats, warp with motion.\n\n"
        "DUALCAST\n"
        "Two hands trigger and hold sounds.\n"
        "Span controls filter. Height controls volume.\n\n"
        "Go to Settings to choose your camera and audio output.",
        juce::dontSendNotification);

    bodyLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    bodyLabel.setColour(juce::Label::textColourId, H_TEXT_DIM);
    bodyLabel.setJustificationType(juce::Justification::topLeft);
    bodyLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(bodyLabel);

    closeButton.setButtonText("CLOSE");
    closeButton.onClick = [this] { mainComponent.hideHelp(); };
    addAndMakeVisible(closeButton);
}

HelpOverlay::~HelpOverlay()
{
    setLookAndFeel(nullptr);
}

void HelpOverlay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark panel fill
    g.setColour(H_BG_DARK);
    g.fillRect(bounds);

    // Subtle grid
    g.setColour(juce::Colour(0x06ff3333));
    for (int x = 0; x < getWidth(); x += 40)
        g.drawVerticalLine(x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40)
        g.drawHorizontalLine(y, 0.0f, (float)getWidth());

    // Border
    g.setColour(H_BORDER_DIM);
    g.drawRect(bounds, 1.0f);

    // Top glow line
    juce::ColourGradient grad(juce::Colours::transparentBlack, bounds.getX(), bounds.getY(),
                               juce::Colours::transparentBlack, bounds.getRight(), bounds.getY(), false);
    grad.addColour(0.2, juce::Colour(0x55ff3333));
    grad.addColour(0.5, juce::Colour(0xffff3333));
    grad.addColour(0.8, juce::Colour(0x55ff3333));
    g.setGradientFill(grad);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.5f);

    // Corner brackets — all four
    g.setColour(juce::Colour(0xaaff3333));
    // TL
    g.drawLine(bounds.getX(),          bounds.getY(),           bounds.getX() + 20,    bounds.getY(),          1.5f);
    g.drawLine(bounds.getX(),          bounds.getY(),           bounds.getX(),          bounds.getY() + 20,     1.5f);
    // TR
    g.drawLine(bounds.getRight() - 20, bounds.getY(),           bounds.getRight(),      bounds.getY(),          1.5f);
    g.drawLine(bounds.getRight(),      bounds.getY(),           bounds.getRight(),      bounds.getY() + 20,     1.5f);
    // BL
    g.drawLine(bounds.getX(),          bounds.getBottom(),      bounds.getX() + 20,    bounds.getBottom(),     1.5f);
    g.drawLine(bounds.getX(),          bounds.getBottom() - 20, bounds.getX(),          bounds.getBottom(),     1.5f);
    // BR
    g.drawLine(bounds.getRight() - 20, bounds.getBottom(),      bounds.getRight(),      bounds.getBottom(),     1.5f);
    g.drawLine(bounds.getRight(),      bounds.getBottom() - 20, bounds.getRight(),      bounds.getBottom(),     1.5f);

    // Divider under title
    auto divY = 70.0f;
    int lineW = (int)(bounds.getWidth() - 56);
    int lineX = 28;
    juce::ColourGradient divGrad(juce::Colours::transparentBlack, (float)lineX, divY,
                                  juce::Colours::transparentBlack, (float)(lineX + lineW), divY, false);
    divGrad.addColour(0.0, juce::Colours::transparentBlack);
    divGrad.addColour(0.3, H_ACCENT_DIM);
    divGrad.addColour(0.7, H_ACCENT_DIM);
    divGrad.addColour(1.0, juce::Colours::transparentBlack);
    g.setGradientFill(divGrad);
    g.fillRect((float)lineX, divY, (float)lineW, 1.0f);

    // Status tag
    g.setColour(juce::Colour(0x44ff3333));
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("SYS:ONLINE   BUILD:v0.1.0-alpha",
               28, (int)bounds.getBottom() - 22, (int)bounds.getWidth() - 56, 14,
               juce::Justification::centredLeft);
}

void HelpOverlay::resized()
{
    auto area = getLocalBounds().reduced(28);

    titleLabel.setBounds(area.removeFromTop(42));
    area.removeFromTop(18);
    closeButton.setBounds(area.removeFromBottom(32).removeFromLeft(90));
    area.removeFromBottom(16);
    bodyLabel.setBounds(area);
}