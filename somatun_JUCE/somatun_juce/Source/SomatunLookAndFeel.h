#pragma once
#include <JuceHeader.h>

class SomatunLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Brand colors
    static constexpr uint32_t BG_DARK      = 0xff050505;
    static constexpr uint32_t BG_CARD      = 0xff0d0d0d;
    static constexpr uint32_t ACCENT_RED   = 0xffff3333;
    static constexpr uint32_t ACCENT_GLOW  = 0x33ff2222;
    static constexpr uint32_t BORDER_DIM   = 0x33ff3333;
    static constexpr uint32_t BORDER_HOT   = 0xaaff3333;
    static constexpr uint32_t TEXT_PRIMARY  = 0xffe8e8e8;
    static constexpr uint32_t TEXT_DIM      = 0x66e8e8e8;
    static constexpr uint32_t TEXT_ACCENT   = 0xffff4444;

    SomatunLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(BG_DARK));
        setColour(juce::TextButton::buttonColourId,          juce::Colour(BG_CARD));
        setColour(juce::TextButton::buttonOnColourId,        juce::Colour(ACCENT_GLOW));
        setColour(juce::TextButton::textColourOffId,         juce::Colour(TEXT_PRIMARY));
        setColour(juce::ComboBox::backgroundColourId,        juce::Colour(BG_CARD));
        setColour(juce::ComboBox::textColourId,              juce::Colour(TEXT_PRIMARY));
        setColour(juce::ComboBox::outlineColourId,           juce::Colour(BORDER_DIM));
        setColour(juce::Label::textColourId,                 juce::Colour(TEXT_PRIMARY));
        setColour(juce::PopupMenu::backgroundColourId,       juce::Colour(BG_CARD));
        setColour(juce::PopupMenu::textColourId,             juce::Colour(TEXT_PRIMARY));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(ACCENT_GLOW));
        setColour(juce::PopupMenu::highlightedTextColourId,  juce::Colour(ACCENT_RED));
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour&,
                              bool isHighlighted,
                              bool isDown) override
    {
        if (button.getWidth() <= 0 || button.getHeight() <= 0)
            return;

        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

        // Card fill
        g.setColour(isDown       ? juce::Colour(0xff1a0a0a)
                    : isHighlighted ? juce::Colour(0xff130808)
                                   : juce::Colour(BG_CARD));
        g.fillRect(bounds);

        // Border
        g.setColour(isHighlighted ? juce::Colour(BORDER_HOT)
                                  : juce::Colour(BORDER_DIM));
        g.drawRect(bounds, 1.0f);

        // Top glow line
        g.setColour(isHighlighted ? juce::Colour(ACCENT_RED)
                                  : juce::Colour(BORDER_DIM));
        g.drawLine(bounds.getX(), bounds.getY(),
                   bounds.getRight(), bounds.getY(), 1.0f);

        // Corner accents (top-left)
        g.setColour(juce::Colour(isHighlighted ? BORDER_HOT : BORDER_DIM));
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getX() + 10, bounds.getY(), 1.5f);
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getX(), bounds.getY() + 10, 1.5f);
        // Bottom-right
        g.drawLine(bounds.getRight() - 10, bounds.getBottom(), bounds.getRight(), bounds.getBottom(), 1.5f);
        g.drawLine(bounds.getRight(), bounds.getBottom() - 10, bounds.getRight(), bounds.getBottom(), 1.5f);
    }

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool isHighlighted, bool) override
    {
        g.setColour(isHighlighted ? juce::Colour(ACCENT_RED)
                                  : juce::Colour(TEXT_PRIMARY));
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText(button.getButtonText(),
                   button.getLocalBounds(),
                   juce::Justification::centred);
    }
};