#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

struct Particle
{
    float x, y, vx, vy, alpha, size;
};

struct WebNode
{
    float x, y;
    float vx, vy;
    float phase;
};

struct CardWeb
{
    juce::Array<WebNode> nodes;
    float hoverIntensity = 0.0f;

    static constexpr int BASE_COUNT  = 35;
    static constexpr int HOVER_EXTRA = 25;

    void init(float cardW, float cardH, juce::Random& rng)
    {
        nodes.clear();
        for (int i = 0; i < BASE_COUNT; ++i)
            spawnNode(cardW, cardH, rng);
    }

    void spawnNode(float cardW, float cardH, juce::Random& rng)
    {
        WebNode n;
        n.x     = 6.0f + rng.nextFloat() * (cardW - 12.0f);
        n.y     = 6.0f + rng.nextFloat() * (cardH - 12.0f);
        n.vx    = (rng.nextFloat() - 0.5f) * 0.4f;
        n.vy    = (rng.nextFloat() - 0.5f) * 0.4f;
        n.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        nodes.add(n);
    }

    void update(float cardW, float cardH, bool hovered, juce::Random& rng)
    {
        float target = hovered ? 1.0f : 0.0f;
        hoverIntensity += (target - hoverIntensity) * 0.07f;

        float speedMult   = 1.0f + hoverIntensity * 4.0f;
        int   targetCount = BASE_COUNT + (int)(hoverIntensity * HOVER_EXTRA);

        while (nodes.size() < targetCount) spawnNode(cardW, cardH, rng);
        while (nodes.size() > targetCount) nodes.removeLast();

        const float margin = 6.0f;
        for (auto& n : nodes)
        {
            n.phase += 0.02f;
            n.x += n.vx * speedMult;
            n.y += n.vy * speedMult;

            if (n.x < margin)         { n.x = margin;         n.vx =  std::abs(n.vx); }
            if (n.x > cardW - margin) { n.x = cardW - margin; n.vx = -std::abs(n.vx); }
            if (n.y < margin)         { n.y = margin;         n.vy =  std::abs(n.vy); }
            if (n.y > cardH - margin) { n.y = cardH - margin; n.vy = -std::abs(n.vy); }
        }
    }
};

class LandingPage : public juce::Component,
                    private juce::Timer
{
public:
    LandingPage(MainComponent& mc);
    ~LandingPage() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    void setOverlayDim (bool dimmed);

private:
    juce::Image logoImage;
    juce::Image iconImage;

    float bootTime = 0.0f;
    const float BOOT_DELAY_1  = 1.5f;
    const float BOOT_LOGO_IN  = 3.5f;
    const float BOOT_LOGO_OUT = 6.0f;
    const float BOOT_DELAY_2  = 7.0f;
    const float BOOT_UI_IN    = 9.0f;

    // Overlay dim animation
    float overlayDimAlpha  { 0.0f };
    float overlayDimTarget { 0.0f };

    void timerCallback () override;
    void initParticles ();
    void initCardWebs  (int cardW, int cardH);

    void drawGrid     (juce::Graphics&);
    void drawParticles(juce::Graphics&);
    void drawScanline (juce::Graphics&);
    void drawTitle    (juce::Graphics&);
    void drawCardWeb  (juce::Graphics&, juce::Rectangle<int>, CardWeb&);
    void drawGameCard (juce::Graphics&, juce::Rectangle<int>,
                       const juce::String& name, int cardIndex);

    MainComponent& mainComponent;

    // Utility nav buttons only — card hit-testing done manually via cardRects
    juce::TextButton settingsButton { "SETTINGS" };
    juce::TextButton helpButton     { "ABOUT" };
    juce::TextButton exitButton     { "EXIT" };

    SomatunLookAndFeel laf;

    juce::Array<Particle>   particles;
    float scanlineY    = 0.0f;
    float flickerAlpha = 1.0f;
    int   flickerTimer = 0;
    int   hoveredCard  = -1;

    CardWeb              cardWebs[3];
    juce::Rectangle<int> cardRects[3];
    juce::Random         rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LandingPage)
};