#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

struct Particle
{
    float x, y, vx, vy, alpha, size;
};

class LandingPage : public juce::Component,
                    private juce::Timer
{
public:
    LandingPage(MainComponent& mc);
    ~LandingPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    juce::Image logoImage;
    juce::Image iconImage;

    // --- OMINOUS BOOT ANIMATION VARIABLES ---
    float bootTime = 0.0f;
    const float BOOT_DELAY_1  = 1.5f;
    const float BOOT_LOGO_IN  = 3.5f;
    const float BOOT_LOGO_OUT = 6.0f;
    const float BOOT_DELAY_2  = 7.0f;
    const float BOOT_UI_IN    = 9.0f;
    // ----------------------------------------

    void timerCallback() override;
    void initParticles();
    void drawGrid(juce::Graphics&);
    void drawParticles(juce::Graphics&);
    void drawTitle(juce::Graphics&);
    void drawScanline(juce::Graphics&);
    void drawGameCard(juce::Graphics& g, juce::Rectangle<int> bounds,
                      const juce::String& tag, const juce::String& name,
                      const juce::String& desc, bool hovered);

    MainComponent& mainComponent;

    // Buttons (invisible game cards handle clicks; icon buttons are visible)
    juce::TextButton game1Button, game2Button, game3Button;
    juce::TextButton settingsButton { "SETTINGS" };
    juce::TextButton helpButton     { "ABOUT" };
    juce::TextButton exitButton     { "EXIT" };

    SomatunLookAndFeel laf;

    juce::Array<Particle> particles;
    float scanlineY    = 0.0f;
    float flickerAlpha = 1.0f;
    int   flickerTimer = 0;
    int   hoveredCard  = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LandingPage)
};