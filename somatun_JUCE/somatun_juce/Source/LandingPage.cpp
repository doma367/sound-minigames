#include "LandingPage.h"
#include "MainComponent.h"

static const juce::Colour ACCENT     { 0xffff3333 };
static const juce::Colour ACCENT_DIM { 0x55ff3333 };
static const juce::Colour BG_DARK    { 0xff050505 };
static const juce::Colour TEXT_PRI   { 0xffe8e8e8 };
static const juce::Colour TEXT_DIM   { 0x66e8e8e8 };

// Edges drawn between nodes closer than this
static constexpr float EDGE_DIST    = 90.0f;
static constexpr float EDGE_DIST_SQ = EDGE_DIST * EDGE_DIST;

// ============================================================
LandingPage::LandingPage(MainComponent& mc) : mainComponent(mc)
{
    setLookAndFeel(&laf);

    // This component handles ALL mouse events — children must not intercept
    setInterceptsMouseClicks(true, false);

    logoImage = juce::ImageCache::getFromMemory(BinaryData::somatun_logo_png,
        BinaryData::somatun_logo_pngSize);
    iconImage = juce::ImageCache::getFromMemory(BinaryData::somatun_icon_png,
        BinaryData::somatun_icon_pngSize);

    settingsButton.onClick = [this] { mainComponent.showSettings(); };
    helpButton.onClick     = [this] { mainComponent.showHelp(); };
    exitButton.onClick     = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };

    addAndMakeVisible(settingsButton);
    addAndMakeVisible(helpButton);
    addAndMakeVisible(exitButton);

    // Hidden during boot
    settingsButton.setAlpha(0.0f);
    helpButton.setAlpha(0.0f);
    exitButton.setAlpha(0.0f);

    initParticles();
    initCardWebs(200, 300); // sensible fallback; overwritten in resized()
    startTimerHz(40);
}

LandingPage::~LandingPage()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

// ============================================================
void LandingPage::initParticles()
{
    particles.clear();
    for (int i = 0; i < 60; ++i)
    {
        Particle p;
        p.x     = rng.nextFloat() * 800.0f;
        p.y     = rng.nextFloat() * 600.0f;
        p.vx    = (rng.nextFloat() - 0.5f) * 0.6f;
        p.vy    = -(rng.nextFloat() * 0.8f + 0.2f);
        p.alpha = rng.nextFloat() * 0.6f + 0.4f;
        p.size  = rng.nextFloat() * 3.5f + 1.0f;
        particles.add(p);
    }
}

void LandingPage::initCardWebs(int cardW, int cardH)
{
    for (int i = 0; i < 3; ++i)
        cardWebs[i].init((float)cardW, (float)cardH, rng);
}

// ============================================================
void LandingPage::timerCallback()
{
    if (bootTime < BOOT_UI_IN)
    {
        bootTime += 1.0f / 40.0f;

        float uiAlpha = 0.0f;
        if (bootTime > BOOT_DELAY_2)
            uiAlpha = juce::jlimit(0.0f, 1.0f,
                (bootTime - BOOT_DELAY_2) / (BOOT_UI_IN - BOOT_DELAY_2));

        settingsButton.setAlpha(uiAlpha);
        helpButton.setAlpha(uiAlpha);
        exitButton.setAlpha(uiAlpha);
    }

    // Animate overlay dim
    if (overlayDimAlpha != overlayDimTarget)
    {
        float speed = 0.18f;  // higher = faster fade
        overlayDimAlpha += (overlayDimTarget - overlayDimAlpha) * speed;
        if (std::abs (overlayDimAlpha - overlayDimTarget) < 0.005f)
            overlayDimAlpha = overlayDimTarget;
    }

    // Particles
    for (auto& p : particles)
    {
        p.x += p.vx;
        p.y += p.vy;
        p.alpha -= 0.003f;

        if (p.alpha <= 0.0f || p.y < -5.0f)
        {
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
    if (scanlineY > (float)getHeight()) scanlineY = 0.0f;

    // Logo flicker
    flickerTimer++;
    if (flickerTimer > 180 && rng.nextFloat() < 0.04f)
    {
        flickerAlpha = 0.75f + rng.nextFloat() * 0.25f;
        flickerTimer = 0;
    }
    else
    {
        flickerAlpha = juce::jmin(1.0f, flickerAlpha + 0.05f);
    }

    // Card webs — always simulating
    {
        auto ca = getLocalBounds().reduced(32);
        ca.removeFromTop(160);
        ca.removeFromBottom(60);
        float cw = (float)juce::jmax(1, (ca.getWidth() - 24) / 3);
        float ch = (float)juce::jmax(1, ca.getHeight());

        for (int i = 0; i < 3; ++i)
            cardWebs[i].update(cw, ch, hoveredCard == i, rng);
    }

    repaint();
}

void LandingPage::setOverlayDim (bool dimmed)
{
    overlayDimTarget = dimmed ? 1.0f : 0.0f;
}

// ============================================================
void LandingPage::paint(juce::Graphics& g)
{
    g.fillAll(BG_DARK);

    // ---- Boot logo sequence ----
    if (bootTime < BOOT_DELAY_2)
    {
        if (logoImage.isValid() && bootTime > BOOT_DELAY_1)
        {
            auto w = getWidth(), h = getHeight();
            float progress   = (bootTime - BOOT_DELAY_1) / (BOOT_LOGO_OUT - BOOT_DELAY_1);
            float scale      = 0.85f + progress * 0.10f;
            float logoAlpha  = bootTime < BOOT_LOGO_IN
                ? (bootTime - BOOT_DELAY_1) / (BOOT_LOGO_IN - BOOT_DELAY_1)
                : 1.0f - (bootTime - BOOT_LOGO_IN) / (BOOT_LOGO_OUT - BOOT_LOGO_IN);
            logoAlpha = juce::jlimit(0.0f, 1.0f, logoAlpha);

            int baseH = 280;
            int baseW = (int)((float)logoImage.getWidth() / (float)logoImage.getHeight() * baseH);
            int lw = (int)(baseW * scale), lh = (int)(baseH * scale);
            g.setOpacity(logoAlpha);
            g.drawImage(logoImage, (w - lw) / 2, (h - lh) / 2, lw, lh,
                        0, 0, logoImage.getWidth(), logoImage.getHeight());
            g.setOpacity(1.0f);
        }
        return;
    }

    // ---- Fade-in UI ----
    float uiAlpha = bootTime < BOOT_UI_IN
        ? juce::jlimit(0.0f, 1.0f, (bootTime - BOOT_DELAY_2) / (BOOT_UI_IN - BOOT_DELAY_2))
        : 1.0f;

    g.beginTransparencyLayer(uiAlpha);

    drawGrid(g);
    drawParticles(g);
    drawScanline(g);

    int w = getWidth(), h = getHeight();

    // Corner brackets
    auto corner = [&](int x, int y, int dx, int dy)
    {
        g.setColour(ACCENT_DIM);
        g.drawLine((float)x, (float)y, (float)(x + dx * 20), (float)y, 1.0f);
        g.drawLine((float)x, (float)y, (float)x, (float)(y + dy * 20), 1.0f);
    };
    corner(12, 12, 1, 1);  corner(w - 12, 12, -1, 1);
    corner(12, h - 12, 1, -1);  corner(w - 12, h - 12, -1, -1);

    // Status
    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("SYS:ONLINE   CAM:READY   AUDIO:ACTIVE",
               0, 14, w, 14, juce::Justification::centred);

    drawTitle(g);

    // Cards
    auto ca = getLocalBounds().reduced(32);
    ca.removeFromTop(160);
    ca.removeFromBottom(60);
    int cw = (ca.getWidth() - 24) / 3;
    int ch = ca.getHeight();

    const char* names[3] = { "FLESHSYNTH", "PULSEFIELD", "DUALCAST" };
    for (int i = 0; i < 3; ++i)
        drawGameCard(g, cardRects[i], names[i], i);

    // Version
    g.setColour(TEXT_DIM.withAlpha(0.3f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("v0.1.0-alpha", 32, h - 30, 100, 20, juce::Justification::centredLeft);

    // Overlay dim (settings / help open)
    if (overlayDimAlpha > 0.001f)
    {
        // Dark frosted layer
        g.setColour (juce::Colour (0xff050505).withAlpha (overlayDimAlpha * 0.72f));
        g.fillAll();

        // Subtle noise/scanline texture on top of the dim
        g.setColour (juce::Colour (0xffff3333).withAlpha (overlayDimAlpha * 0.03f));
        for (int y = 0; y < getHeight(); y += 3)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
    }

    g.endTransparencyLayer();
}

// ============================================================
void LandingPage::drawGrid(juce::Graphics& g)
{
    g.setColour(juce::Colour(0x08ff3333));
    for (int x = 0; x < getWidth();  x += 40) g.drawVerticalLine  (x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40) g.drawHorizontalLine(y, 0.0f, (float)getWidth());
}
void LandingPage::drawParticles(juce::Graphics& g)
{
    for (const auto& p : particles)
    {
        float a = juce::jlimit(0.0f, 1.0f, p.alpha);
        g.setColour(ACCENT.withAlpha(a * 0.25f));
        g.fillEllipse(p.x - p.size * 1.5f, p.y - p.size * 1.5f, p.size * 3.0f, p.size * 3.0f);
        g.setColour(ACCENT.withAlpha(a));
        g.fillEllipse(p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size);
    }
}

void LandingPage::drawScanline(juce::Graphics& g)
{
    float w = (float)getWidth();
    juce::ColourGradient grad(juce::Colours::transparentBlack, 0.0f, scanlineY,
                               juce::Colours::transparentBlack, w,   scanlineY, false);
    grad.addColour(0.3, juce::Colour(0x08ff2222));
    grad.addColour(0.5, juce::Colour(0x18ff2222));
    grad.addColour(0.7, juce::Colour(0x08ff2222));
    g.setGradientFill(grad);
    g.fillRect(0.0f, scanlineY - 1.0f, w, 3.0f);
}

void LandingPage::drawTitle(juce::Graphics& g)
{
    int w = getWidth();

    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("MOTION  *  SOUND  *  INTERFACE", 0, 44, w, 14, juce::Justification::centred);

    if (logoImage.isValid())
    {
        int lh = 280;
        int lw = (int)((float)logoImage.getWidth() / (float)logoImage.getHeight() * lh);
        int lx = (w - lw) / 2;
        int ly = 58 + (100 - lh) / 2;
        g.setOpacity(flickerAlpha);
        g.drawImage(logoImage, lx, ly, lw, lh, 0, 0, logoImage.getWidth(), logoImage.getHeight());
        g.setOpacity(1.0f);
    }

    int lineW = 320, lineX = (w - lineW) / 2;
    juce::ColourGradient gr(juce::Colours::transparentBlack, (float)lineX, 146.0f,
                             juce::Colours::transparentBlack, (float)(lineX + lineW), 146.0f, false);
    gr.addColour(0.2, ACCENT_DIM); gr.addColour(0.5, ACCENT); gr.addColour(0.8, ACCENT_DIM);
    g.setGradientFill(gr);
    g.fillRect(lineX, 146, lineW, 1);

    g.setColour(TEXT_DIM);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.drawText("Gesture-Driven Audio Playground", 0, 152, w, 16, juce::Justification::centred);
}

// ============================================================
void LandingPage::drawCardWeb(juce::Graphics& g, juce::Rectangle<int> bounds, CardWeb& web)
{
    if (web.nodes.isEmpty()) return;

    float ox  = (float)bounds.getX();
    float oy  = (float)bounds.getY();
    float hi  = web.hoverIntensity;

    // Vibrant at rest, blazing on hover
    float edgeBaseAlpha = 0.28f + hi * 0.55f;
    float nodeBaseAlpha = 0.55f + hi * 0.45f;
    float thickness     = 0.8f  + hi * 1.2f;

    auto& nodes = web.nodes;
    int   n     = nodes.size();

    // Edges
    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            float dx = nodes[j].x - nodes[i].x;
            float dy = nodes[j].y - nodes[i].y;
            float dSq = dx * dx + dy * dy;
            if (dSq >= EDGE_DIST_SQ) continue;

            float t     = 1.0f - std::sqrt(dSq) / EDGE_DIST;   // 0→1 as nodes get closer
            float pulse = 0.5f + 0.5f * std::sin(nodes[i].phase + nodes[j].phase);
            float alpha = edgeBaseAlpha * t * t + hi * 0.15f * pulse;
            alpha = juce::jlimit(0.0f, 1.0f, alpha);

            g.setColour(ACCENT.withAlpha(alpha));
            g.drawLine(ox + nodes[i].x, oy + nodes[i].y,
                       ox + nodes[j].x, oy + nodes[j].y, thickness);
        }
    }

    // Nodes
    for (int i = 0; i < n; ++i)
    {
        float nx    = ox + nodes[i].x;
        float ny    = oy + nodes[i].y;
        float pulse = 0.5f + 0.5f * std::sin(nodes[i].phase * 1.4f);

        // Outer glow
        float glowR = (4.0f + hi * 7.0f) * (0.75f + 0.25f * pulse);
        g.setColour(ACCENT.withAlpha(nodeBaseAlpha * 0.25f));
        g.fillEllipse(nx - glowR, ny - glowR, glowR * 2.0f, glowR * 2.0f);

        // Core
        float coreR = (2.0f + hi * 2.5f) * (0.8f + 0.2f * pulse);
        g.setColour(ACCENT.withAlpha(nodeBaseAlpha));
        g.fillEllipse(nx - coreR, ny - coreR, coreR * 2.0f, coreR * 2.0f);

        // White hot centre on hover
        if (hi > 0.05f)
        {
            g.setColour(juce::Colour(0xffffffff).withAlpha(hi * 0.75f * (0.6f + 0.4f * pulse)));
            g.fillEllipse(nx - 1.0f, ny - 1.0f, 2.0f, 2.0f);
        }
    }
}

void LandingPage::drawGameCard(juce::Graphics& g, juce::Rectangle<int> bounds,
                                const juce::String& name, int cardIndex)
{
    CardWeb& web = cardWebs[cardIndex];
    bool     hov = (hoveredCard == cardIndex);
    float    hi  = web.hoverIntensity;

    // ── Background ───────────────────────────────────────────
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRect  (bounds);

    // ── Web fills the entire card — clipped to bounds ────────
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (bounds);
        drawCardWeb (g, bounds, web);
    }

    // ── Border ───────────────────────────────────────────────
    float borderAlpha = 0.25f + hi * 0.65f;
    g.setColour (ACCENT.withAlpha (borderAlpha));
    g.drawRect  (bounds.toFloat(), 1.0f);

    // ── Top glow line ────────────────────────────────────────
    juce::ColourGradient topGrad (
        juce::Colours::transparentBlack, (float) bounds.getX(),    (float) bounds.getY(),
        juce::Colours::transparentBlack, (float) bounds.getRight(), (float) bounds.getY(), false);
    topGrad.addColour (0.2, ACCENT.withAlpha (0.2f + hi * 0.4f));
    topGrad.addColour (0.5, ACCENT.withAlpha (0.5f + hi * 0.5f));
    topGrad.addColour (0.8, ACCENT.withAlpha (0.2f + hi * 0.4f));
    g.setGradientFill (topGrad);
    g.fillRect ((float) bounds.getX(), (float) bounds.getY(),
                (float) bounds.getWidth(), 1.5f);

    // ── Corner brackets ──────────────────────────────────────
    float bracketAlpha = 0.4f + hi * 0.6f;
    float bracketLen   = 12.0f;
    g.setColour (ACCENT.withAlpha (bracketAlpha));
    // TL
    g.drawLine ((float) bounds.getX(), (float) bounds.getY(),
                (float) bounds.getX() + bracketLen, (float) bounds.getY(), 1.5f);
    g.drawLine ((float) bounds.getX(), (float) bounds.getY(),
                (float) bounds.getX(), (float) bounds.getY() + bracketLen, 1.5f);
    // TR
    g.drawLine ((float) bounds.getRight() - bracketLen, (float) bounds.getY(),
                (float) bounds.getRight(), (float) bounds.getY(), 1.5f);
    g.drawLine ((float) bounds.getRight(), (float) bounds.getY(),
                (float) bounds.getRight(), (float) bounds.getY() + bracketLen, 1.5f);
    // BL
    g.drawLine ((float) bounds.getX(), (float) bounds.getBottom(),
                (float) bounds.getX() + bracketLen, (float) bounds.getBottom(), 1.5f);
    g.drawLine ((float) bounds.getX(), (float) bounds.getBottom() - bracketLen,
                (float) bounds.getX(), (float) bounds.getBottom(), 1.5f);
    // BR
    g.drawLine ((float) bounds.getRight() - bracketLen, (float) bounds.getBottom(),
                (float) bounds.getRight(), (float) bounds.getBottom(), 1.5f);
    g.drawLine ((float) bounds.getRight(), (float) bounds.getBottom() - bracketLen,
                (float) bounds.getRight(), (float) bounds.getBottom(), 1.5f);

    // ── Index number (top-left, small) ───────────────────────
    g.setColour (ACCENT.withAlpha (0.3f + hi * 0.4f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("0" + juce::String (cardIndex + 1),
                 bounds.getX() + 10, bounds.getY() + 8,
                 30, 12, juce::Justification::centredLeft);

    // ── Mode label — bottom left, not centered ────────────────
    // Prefix tag
    g.setColour (ACCENT.withAlpha (0.5f + hi * 0.5f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  ("//", bounds.getX() + 10, bounds.getBottom() - 38,
                 20, 14, juce::Justification::centredLeft);

    // Main name
    g.setColour (hov ? ACCENT : TEXT_PRI.withAlpha (0.6f + hi * 0.4f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (13.0f)));
    g.drawText  (name, bounds.getX() + 24, bounds.getBottom() - 38,
                 bounds.getWidth() - 34, 14, juce::Justification::centredLeft);

    // Subtle description line
    static const char* descs[3] = {
        "body  >>  waveform",
        "gesture  >>  rhythm",
        "hands  >>  dual voice"
    };
    g.setColour (TEXT_DIM.withAlpha (0.35f + hi * 0.35f));
    g.setFont   (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.drawText  (descs[cardIndex], bounds.getX() + 10, bounds.getBottom() - 22,
                 bounds.getWidth() - 20, 12, juce::Justification::centredLeft);
}

// ============================================================
void LandingPage::resized()
{
    int w = getWidth(), h = getHeight();

    auto ca = getLocalBounds().reduced(32);
    ca.removeFromTop(160);
    ca.removeFromBottom(60);
    int cw = (ca.getWidth() - 24) / 3;
    int ch = ca.getHeight();

    cardRects[0] = { ca.getX(),                    ca.getY(), cw, ch };
    cardRects[1] = { ca.getX() + cw + 12,          ca.getY(), cw, ch };
    cardRects[2] = { ca.getX() + (cw + 12) * 2,   ca.getY(), cw, ch };

    initCardWebs(juce::jmax(1, cw), juce::jmax(1, ch));

    int btnY = h - 44;
    exitButton    .setBounds(w - 44 - 80,   btnY, 70,  30);
    helpButton    .setBounds(w - 44 - 190,  btnY, 100, 30);
    settingsButton.setBounds(w - 44 - 310,  btnY, 110, 30);
}

// ============================================================
void LandingPage::mouseMove(const juce::MouseEvent& e)
{
    if (bootTime < BOOT_UI_IN) return;

    int prev = hoveredCard;
    auto pt  = e.getPosition();

    hoveredCard = -1;
    for (int i = 0; i < 3; ++i)
        if (cardRects[i].contains(pt)) { hoveredCard = i; break; }

    if (hoveredCard != prev)
        repaint();
}

void LandingPage::mouseExit(const juce::MouseEvent&)
{
    hoveredCard = -1;
    repaint();
}

void LandingPage::mouseUp (const juce::MouseEvent& e)
{
    if (bootTime < BOOT_UI_IN) return;
 
    for (int i = 0; i < 3; ++i)
    {
        if (cardRects[i].contains (e.getPosition()))
        {
            mainComponent.launchMode (i);
            break;
        }
    }
}
 
