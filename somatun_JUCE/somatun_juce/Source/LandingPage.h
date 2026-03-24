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

    // Buttons (invisible — we draw cards manually, buttons handle clicks)
    juce::TextButton game1Button, game2Button, game3Button;
    juce::TextButton settingsButton { "SETTINGS" };
    juce::TextButton helpButton     { "ABOUT" };

    SomatunLookAndFeel laf;

    juce::Array<Particle> particles;
    float scanlineY    = 0.0f;
    float flickerAlpha = 1.0f;
    int   flickerTimer = 0;
    int   hoveredCard  = -1;  // 0, 1, 2 or -1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LandingPage)
};