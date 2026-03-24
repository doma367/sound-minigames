#include "LandingPage.h"
#include "MainComponent.h"

static const juce::Colour ACCENT     { 0xffff3333 };
static const juce::Colour ACCENT_DIM { 0x55ff3333 };
static const juce::Colour BORDER_DIM { 0x33ff3333 };
static const juce::Colour BORDER_HOT { 0xaaff3333 };
static const juce::Colour BG_DARK    { 0xff050505 };
static const juce::Colour TEXT_PRI   { 0xffe8e8e8 };
static const juce::Colour TEXT_DIM   { 0x66e8e8e8 };

LandingPage::LandingPage(MainComponent& mc) : mainComponent(mc)
{
    setLookAndFeel(&laf);
    setInterceptsMouseClicks(true, true);

    // Game buttons — transparent, just handle mouse
    game1Button.setButtonText("FLESHSYNTH");
    game2Button.setButtonText("PULSEFIELD");
    game3Button.setButtonText("DUALCAST");
    addAndMakeVisible(game1Button);
    addAndMakeVisible(game2Button);
    addAndMakeVisible(game3Button);

    settingsButton.onClick = [this] { mainComponent.showSettings(); };
    helpButton.onClick     = [this] { mainComponent.showHelp(); };
    addAndMakeVisible(settingsButton);
    addAndMakeVisible(helpButton);

    initParticles();
    startTimerHz(40);
}

LandingPage::~LandingPage()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void LandingPage::initParticles()
{
    particles.clear();
    juce::Random rng;
    for (int i = 0; i < 35; ++i)
    {
        Particle p;
        p.x     = rng.nextFloat() * 800.0f;
        p.y     = rng.nextFloat() * 600.0f;
        p.vx    = (rng.nextFloat() - 0.5f) * 0.4f;
        p.vy    = -(rng.nextFloat() * 0.5f + 0.1f);
        p.alpha = rng.nextFloat() * 0.7f + 0.3f;
        p.size  = rng.nextFloat() * 2.0f + 1.0f;
        particles.add(p);
    }
}

void LandingPage::timerCallback()
{
    // Move particles
    for (auto& p : particles)
    {
        p.x += p.vx;
        p.y += p.vy;
        p.alpha -= 0.003f;

        if (p.alpha <= 0.0f || p.y < -5.0f)
        {
            juce::Random rng;
            p.x     = rng.nextFloat() * (float)getWidth();
            p.y     = (float)getHeight() + 5.0f;
            p.vx    = (rng.nextFloat() - 0.5f) * 0.4f;
            p.vy    = -(rng.nextFloat() * 0.5f + 0.1f);
            p.alpha = rng.nextFloat() * 0.7f + 0.3f;
            p.size  = rng.nextFloat() * 2.0f + 1.0f;
        }
    }

    // Scanline
    scanlineY += 1.8f;
    if (scanlineY > (float)getHeight())
        scanlineY = 0.0f;

    // Flicker
    flickerTimer++;
    if (flickerTimer > 180 && juce::Random::getSystemRandom().nextFloat() < 0.04f)
    {
        flickerAlpha = 0.75f + juce::Random::getSystemRandom().nextFloat() * 0.25f;
        flickerTimer = 0;
    }
    else
    {
        flickerAlpha = juce::jmin(1.0f, flickerAlpha + 0.05f);
    }

    repaint();
}

void LandingPage::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(BG_DARK);

    drawGrid(g);
    drawParticles(g);
    drawScanline(g);

    auto w = getWidth();
    auto h = getHeight();

    // Corner brackets
    auto drawCorner = [&](int x, int y, int dx, int dy)
    {
        g.setColour(ACCENT_DIM);
        g.drawLine((float)x, (float)y, (float)(x + dx * 20), (float)y, 1.0f);
        g.drawLine((float)x, (float)y, (float)x, (float)(y + dy * 20), 1.0f);
    };
    drawCorner(12, 12, 1, 1);
    drawCorner(w - 12, 12, -1, 1);
    drawCorner(12, h - 12, 1, -1);
    drawCorner(w - 12, h - 12, -1, -1);

    // Status line top
    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("SYS:ONLINE   CAM:READY   AUDIO:ACTIVE",
               0, 14, w, 14, juce::Justification::centred);

    drawTitle(g);

    // Game cards (drawn manually under transparent buttons)
    auto cardArea = getLocalBounds().reduced(32);
    cardArea.removeFromTop(160);
    cardArea.removeFromBottom(60);

    int cardW = (cardArea.getWidth() - 24) / 3;
    int cardH = cardArea.getHeight();

    struct CardDef { const char* tag; const char* name; const char* desc; };
    CardDef cards[3] = {
        { "01 / SYNTH",   "FLESHSYNTH", "Body points shape a live waveform.\nMove to sculpt sound in real time." },
        { "02 / BEAT",    "PULSEFIELD", "Step sequencer + body FX.\nDraw beats, warp with motion." },
        { "03 / SAMPLER", "DUALCAST",   "Two hands, two voices. Span controls\nfilter. Height controls volume." }
    };

    for (int i = 0; i < 3; ++i)
    {
        auto cardBounds = juce::Rectangle<int>(
            cardArea.getX() + i * (cardW + 12),
            cardArea.getY(),
            cardW, cardH);

        drawGameCard(g, cardBounds,
                     cards[i].tag, cards[i].name, cards[i].desc,
                     hoveredCard == i);
    }

    // Version tag
    g.setColour(TEXT_DIM.withAlpha(0.3f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("v0.1.0-alpha", 32, h - 30, 100, 20, juce::Justification::centredLeft);
}

void LandingPage::drawGrid(juce::Graphics& g)
{
    auto w = getWidth();
    auto h = getHeight();
    g.setColour(juce::Colour(0x08ff3333));

    for (int x = 0; x < w; x += 40)
        g.drawVerticalLine(x, 0.0f, (float)h);
    for (int y = 0; y < h; y += 40)
        g.drawHorizontalLine(y, 0.0f, (float)w);
}

void LandingPage::drawParticles(juce::Graphics& g)
{
    for (const auto& p : particles)
    {
        g.setColour(ACCENT.withAlpha(p.alpha * 0.6f));
        g.fillEllipse(p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size);
    }
}

void LandingPage::drawScanline(juce::Graphics& g)
{
    auto w = (float)getWidth();
    juce::ColourGradient grad(juce::Colours::transparentBlack, 0.0f, scanlineY,
                               juce::Colours::transparentBlack, w, scanlineY, false);
    grad.addColour(0.3, juce::Colour(0x08ff2222));
    grad.addColour(0.5, juce::Colour(0x18ff2222));
    grad.addColour(0.7, juce::Colour(0x08ff2222));
    g.setGradientFill(grad);
    g.fillRect(0.0f, scanlineY - 1.0f, w, 3.0f);
}

void LandingPage::drawTitle(juce::Graphics& g)
{
    auto w = getWidth();

    // Subtitle above
    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("MOTION  *  SOUND  *  INTERFACE", 0, 44, w, 14, juce::Justification::centred);

    // Main title with flicker
    g.setColour(ACCENT.withAlpha(flickerAlpha));
    g.setFont(juce::Font(juce::FontOptions().withHeight(54.0f)));
    g.drawText("SOMATUN", 0, 58, w, 64, juce::Justification::centred);

    // Divider line
    int lineW = 320;
    int lineX = (w - lineW) / 2;
    juce::ColourGradient grad(juce::Colours::transparentBlack, (float)lineX, 126.0f,
                               juce::Colours::transparentBlack, (float)(lineX + lineW), 126.0f, false);
    grad.addColour(0.2, ACCENT_DIM);
    grad.addColour(0.5, ACCENT);
    grad.addColour(0.8, ACCENT_DIM);
    g.setGradientFill(grad);
    g.fillRect(lineX, 126, lineW, 1);

    // Tagline below
    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.drawText("Gesture-Driven Audio Playground", 0, 132, w, 16, juce::Justification::centred);
}

void LandingPage::drawGameCard(juce::Graphics& g, juce::Rectangle<int> bounds,
                                const juce::String& tag, const juce::String& name,
                                const juce::String& desc, bool hovered)
{
    auto bf = bounds.toFloat();

    // Card background
    g.setColour(hovered ? juce::Colour(0xff130808) : juce::Colour(0xff0d0505));
    g.fillRect(bf);

    // Border
    g.setColour(hovered ? BORDER_HOT : BORDER_DIM);
    g.drawRect(bf, 1.0f);

    // Top glow line
    juce::ColourGradient topGrad(juce::Colours::transparentBlack, bf.getX(), bf.getY(),
                                  juce::Colours::transparentBlack, bf.getRight(), bf.getY(), false);
    topGrad.addColour(0.2, hovered ? ACCENT : ACCENT_DIM);
    topGrad.addColour(0.5, hovered ? ACCENT : BORDER_DIM);
    topGrad.addColour(0.8, hovered ? ACCENT : ACCENT_DIM);
    g.setGradientFill(topGrad);
    g.fillRect(bf.getX(), bf.getY(), bf.getWidth(), 1.0f);

    // Corner brackets
    float cx = bf.getX(), cy = bf.getY();
    float cr = bf.getRight(), cb = bf.getBottom();
    auto c = hovered ? BORDER_HOT : BORDER_DIM;
    g.setColour(c);
    g.drawLine(cx, cy, cx + 10, cy, 1.5f);
    g.drawLine(cx, cy, cx, cy + 10, 1.5f);
    g.drawLine(cr - 10, cb, cr, cb, 1.5f);
    g.drawLine(cr, cb - 10, cr, cb, 1.5f);

    // Tag
    g.setColour(hovered ? ACCENT_DIM : BORDER_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText(tag, bounds.reduced(14, 12).removeFromTop(12), juce::Justification::topLeft);

    // Name
    g.setColour(hovered ? ACCENT : TEXT_PRI);
    g.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)));
    auto nameArea = bounds.reduced(14, 0);
    nameArea.setY(bounds.getY() + 34);
    nameArea.setHeight(26);
    g.drawText(name, nameArea, juce::Justification::topLeft);

    // Desc
    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    auto descArea = bounds.reduced(14, 0);
    descArea.setY(bounds.getY() + 64);
    descArea.setHeight(bounds.getHeight() - 78);
    g.drawMultiLineText(desc, descArea.getX(), descArea.getY(), descArea.getWidth());
}

void LandingPage::resized()
{
    auto w = getWidth();
    auto h = getHeight();

    // Card click areas (invisible buttons sit over drawn cards)
    auto cardArea = getLocalBounds().reduced(32);
    cardArea.removeFromTop(160);
    cardArea.removeFromBottom(60);

    int cardW = (cardArea.getWidth() - 24) / 3;
    int cardH = cardArea.getHeight();

    game1Button.setBounds(cardArea.getX(),                    cardArea.getY(), cardW, cardH);
    game2Button.setBounds(cardArea.getX() + cardW + 12,       cardArea.getY(), cardW, cardH);
    game3Button.setBounds(cardArea.getX() + (cardW + 12) * 2, cardArea.getY(), cardW, cardH);

    // Make buttons fully transparent so our custom paint shows through
    game1Button.setAlpha(0.0f);
    game2Button.setAlpha(0.0f);
    game3Button.setAlpha(0.0f);

    // Bottom bar buttons
    int btnY = h - 44;
    helpButton.setBounds    (w - 44 - 110, btnY, 100, 30);
    settingsButton.setBounds(w - 44 - 230, btnY, 110, 30);
}

void LandingPage::mouseMove(const juce::MouseEvent& e)
{
    int prev = hoveredCard;
    hoveredCard = game1Button.getBounds().contains(e.getPosition()) ? 0
                : game2Button.getBounds().contains(e.getPosition()) ? 1
                : game3Button.getBounds().contains(e.getPosition()) ? 2 : -1;
    if (hoveredCard != prev)
        repaint();
}

void LandingPage::mouseExit(const juce::MouseEvent&)
{
    hoveredCard = -1;
    repaint();
}   