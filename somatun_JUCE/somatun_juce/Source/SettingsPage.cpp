#include "SettingsPage.h"
#include "MainComponent.h"

//==============================================================================
// Colours
static const juce::Colour S_ACCENT     { 0xffff3333 };
static const juce::Colour S_ACCENT_DIM { 0x55ff3333 };
static const juce::Colour S_BORDER_DIM { 0x33ff3333 };
static const juce::Colour S_BORDER_HOT { 0xaaff3333 };
static const juce::Colour S_BG_DARK    { 0xf2050505 };
static const juce::Colour S_BG_ROW     { 0xff0a0a0a };
static const juce::Colour S_TEXT_PRI   { 0xffe8e8e8 };
static const juce::Colour S_TEXT_DIM   { 0x66e8e8e8 };

static constexpr int ROW_H       = 52;
static constexpr int SECTION_H   = 36;
static constexpr int GAP         = 6;
static constexpr int PREVIEW_H   = 140;
static constexpr int SIDE_PAD    = 20;

//==============================================================================
// SettingsContent
//==============================================================================

void SettingsContent::drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    // Left accent bar
    g.setColour(S_ACCENT);
    g.fillRect(bounds.getX(), bounds.getY() + 8, 2, bounds.getHeight() - 16);

    // Section label
    g.setColour(S_ACCENT);
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    g.drawText(title, bounds.getX() + 12, bounds.getY(), bounds.getWidth() - 12, bounds.getHeight(),
               juce::Justification::centredLeft);

    // Hairline to the right
    g.setColour(juce::Colour(0x22ff3333));
    int lineX = bounds.getX() + 12 + g.getCurrentFont().getStringWidth(title) + 10;
    g.drawHorizontalLine(bounds.getCentreY(), (float)lineX, (float)bounds.getRight());
}

void SettingsContent::drawRowCard(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto bf = bounds.toFloat();
    g.setColour(S_BG_ROW);
    g.fillRect(bf);
    g.setColour(juce::Colour(0x18ff3333));
    g.drawRect(bf, 1.0f);
    // Left accent sliver
    g.setColour(juce::Colour(0x33ff3333));
    g.fillRect(bf.getX(), bf.getY(), 2.0f, bf.getHeight());
}

void SettingsContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);

    // We compute the same layout as resized() to draw row cards behind controls
    auto area = getLocalBounds().reduced(SIDE_PAD, 0);
    area.removeFromTop(GAP);

    auto consumeSection = [&]() { area.removeFromTop(SECTION_H + GAP); };
    auto consumeRow     = [&]() -> juce::Rectangle<int>
    {
        auto r = area.removeFromTop(ROW_H);
        area.removeFromTop(GAP);
        return r;
    };
    auto consumePreview = [&]() { area.removeFromTop(PREVIEW_H + GAP); };

    // === GENERAL ===
    consumeSection();
    drawRowCard(g, consumeRow()); // window mode

    area.removeFromTop(GAP * 2);

    // === INPUT ===
    consumeSection();
    drawRowCard(g, consumeRow()); // camera device
    // preview area — draw a bordered dark box
    if (cameraPreviewActive)
    {
        auto prev = area.removeFromTop(PREVIEW_H);
        area.removeFromTop(GAP);
        g.setColour(juce::Colour(0xff080808));
        g.fillRect(prev);
        g.setColour(juce::Colour(0x33ff3333));
        g.drawRect(prev.toFloat(), 1.0f);
        g.setColour(S_TEXT_DIM);
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        g.drawText("CAMERA FEED — NOT YET CONNECTED",
                   prev, juce::Justification::centred);
        // scanline hint
        g.setColour(juce::Colour(0x0aff3333));
        for (int y = prev.getY(); y < prev.getBottom(); y += 4)
            g.drawHorizontalLine(y, (float)prev.getX(), (float)prev.getRight());
    }
    drawRowCard(g, consumeRow()); // camera resolution
    drawRowCard(g, consumeRow()); // flip

    area.removeFromTop(GAP * 2);

    // === OUTPUT ===
    consumeSection();
    drawRowCard(g, consumeRow()); // audio device
    drawRowCard(g, consumeRow()); // volume
    drawRowCard(g, consumeRow()); // sample rate
    drawRowCard(g, consumeRow()); // buffer size

    // Row labels drawn over the cards
    // (labels are actual Label components, so nothing extra needed here)
}

void SettingsContent::resized()
{
    auto area = getLocalBounds().reduced(SIDE_PAD, 0);
    area.removeFromTop(GAP);

    auto labelFont = juce::Font(juce::FontOptions().withHeight(9.0f));

    auto layoutRow = [&](juce::Label& rowLabel, const juce::String& text,
                         std::function<void(juce::Rectangle<int>)> controlLayout)
    {
        auto row = area.removeFromTop(ROW_H);
        area.removeFromTop(GAP);

        // label sits in left half, vertically centred
        rowLabel.setText(text, juce::dontSendNotification);
        rowLabel.setFont(labelFont);
        rowLabel.setColour(juce::Label::textColourId, S_TEXT_DIM);
        rowLabel.setBounds(row.reduced(10, 0).removeFromLeft(160));

        controlLayout(row.reduced(10, 0));
    };

    // === GENERAL ===
    sectionGeneralLabel.setBounds(area.removeFromTop(SECTION_H));
    area.removeFromTop(GAP);

    layoutRow(windowModeLabel, "WINDOW MODE", [&](juce::Rectangle<int> row)
    {
        auto right = row.removeFromRight(row.getWidth() - 160);
        windowModeCombo.setBounds(right.reduced(0, 10).removeFromLeft(200));
    });

    area.removeFromTop(GAP * 2);

    // === INPUT ===
    sectionInputLabel.setBounds(area.removeFromTop(SECTION_H));
    area.removeFromTop(GAP);

    // Camera device row — combo + test button
    {
        auto row = area.removeFromTop(ROW_H);
        area.removeFromTop(GAP);
        auto inner = row.reduced(10, 0);
        cameraDeviceLabel.setText("CAMERA DEVICE", juce::dontSendNotification);
        cameraDeviceLabel.setFont(labelFont);
        cameraDeviceLabel.setColour(juce::Label::textColourId, S_TEXT_DIM);
        cameraDeviceLabel.setBounds(inner.removeFromLeft(160));
        cameraTestButton.setBounds(inner.removeFromRight(60).reduced(0, 10));
        cameraCombo.setBounds(inner.reduced(0, 10).removeFromLeft(200));
    }

    // Camera preview (only takes space when active)
    if (cameraPreviewActive)
    {
        auto prev = area.removeFromTop(PREVIEW_H);
        area.removeFromTop(GAP);
        cameraPreviewLabel.setBounds(prev);
        cameraPreviewLabel.setVisible(true);
    }
    else
    {
        cameraPreviewLabel.setVisible(false);
        cameraPreviewLabel.setBounds(0, 0, 0, 0);
    }

    layoutRow(cameraResLabel, "CAMERA RESOLUTION", [&](juce::Rectangle<int> row)
    {
        auto right = row.removeFromRight(row.getWidth() - 160);
        cameraResCombo.setBounds(right.reduced(0, 10).removeFromLeft(200));
    });

    {
        auto row = area.removeFromTop(ROW_H);
        area.removeFromTop(GAP);
        auto inner = row.reduced(10, 0);
        cameraFlipLabel.setText("FLIP HORIZONTAL", juce::dontSendNotification);
        cameraFlipLabel.setFont(labelFont);
        cameraFlipLabel.setColour(juce::Label::textColourId, S_TEXT_DIM);
        cameraFlipLabel.setBounds(inner.removeFromLeft(160));
        cameraFlipToggle.setBounds(inner.removeFromLeft(40).reduced(0, 14));
    }

    area.removeFromTop(GAP * 2);

    // === OUTPUT ===
    sectionOutputLabel.setBounds(area.removeFromTop(SECTION_H));
    area.removeFromTop(GAP);

    // Audio device row — combo + test button
    {
        auto row = area.removeFromTop(ROW_H);
        area.removeFromTop(GAP);
        auto inner = row.reduced(10, 0);
        audioDeviceLabel.setText("AUDIO DEVICE", juce::dontSendNotification);
        audioDeviceLabel.setFont(labelFont);
        audioDeviceLabel.setColour(juce::Label::textColourId, S_TEXT_DIM);
        audioDeviceLabel.setBounds(inner.removeFromLeft(160));
        audioTestButton.setBounds(inner.removeFromRight(60).reduced(0, 10));
        audioCombo.setBounds(inner.reduced(0, 10).removeFromLeft(200));
    }

    layoutRow(volumeLabel, "MASTER VOLUME", [&](juce::Rectangle<int> row)
    {
        auto right = row.removeFromRight(row.getWidth() - 160);
        volumeSlider.setBounds(right.reduced(0, 12).removeFromLeft(220));
    });

    layoutRow(sampleRateLabel, "SAMPLE RATE", [&](juce::Rectangle<int> row)
    {
        auto right = row.removeFromRight(row.getWidth() - 160);
        sampleRateCombo.setBounds(right.reduced(0, 10).removeFromLeft(160));
    });

    layoutRow(bufferSizeLabel, "BUFFER SIZE", [&](juce::Rectangle<int> row)
    {
        auto right = row.removeFromRight(row.getWidth() - 160);
        bufferSizeCombo.setBounds(right.reduced(0, 10).removeFromLeft(160));
    });
}

// Helper: total height of all content so the Viewport knows how tall to make us
static int computeContentHeight(bool previewActive)
{
    int h = GAP;
    // GENERAL section
    h += SECTION_H + GAP + ROW_H + GAP + GAP * 2;
    // INPUT section
    h += SECTION_H + GAP + ROW_H + GAP; // camera device
    if (previewActive) h += PREVIEW_H + GAP;
    h += ROW_H + GAP + ROW_H + GAP + GAP * 2; // res + flip
    // OUTPUT section
    h += SECTION_H + GAP + (ROW_H + GAP) * 4; // device + vol + sr + buf
    h += 16; // bottom breathing room
    return h;
}

//==============================================================================
// SettingsPage
//==============================================================================

SettingsPage::SettingsPage(MainComponent& mc) : mainComponent(mc)
{
    setLookAndFeel(&laf);

    // Title
    titleLabel.setText("// SETTINGS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    titleLabel.setColour(juce::Label::textColourId, S_ACCENT);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Close
    closeButton.onClick = [this] { mainComponent.hideSettings(); };
    addAndMakeVisible(closeButton);

    // Viewport + content
    viewport.setScrollBarsShown(true, false);
    viewport.setViewedComponent(&content, false);
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, S_ACCENT_DIM);
    addAndMakeVisible(viewport);

    // Section header labels
    auto initSection = [](juce::Label& l, const juce::String& t)
    {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        l.setColour(juce::Label::textColourId, S_ACCENT);
    };
    initSection(content.sectionGeneralLabel, "// GENERAL");
    initSection(content.sectionInputLabel,   "// INPUT");
    initSection(content.sectionOutputLabel,  "// OUTPUT");
    content.addAndMakeVisible(content.sectionGeneralLabel);
    content.addAndMakeVisible(content.sectionInputLabel);
    content.addAndMakeVisible(content.sectionOutputLabel);

    // Window mode
    content.windowModeCombo.addItem("Fullscreen",          1);
    content.windowModeCombo.addItem("Windowed Fullscreen", 2);
    content.windowModeCombo.addItem("Windowed",            3);
    content.windowModeCombo.setSelectedId(3);
    content.windowModeCombo.onChange = [this]
    {
        auto* w = juce::Desktop::getInstance().getComponent(0);
        if (w == nullptr) return;
        int mode = content.windowModeCombo.getSelectedId();
        if (auto* dw = dynamic_cast<juce::ResizableWindow*>(w))
        {
            if (mode == 1 || mode == 2)
                dw->setFullScreen(true);
            else
            {
                dw->setFullScreen(false);
                dw->setResizable(true, true);
            }
        }
    };
    content.addAndMakeVisible(content.windowModeLabel);
    content.addAndMakeVisible(content.windowModeCombo);

    // Camera device
    content.cameraCombo.addItem("(no camera selected)", 1);
    content.cameraCombo.setSelectedId(1);
    content.cameraTestButton.onClick = [this]
    {
        content.cameraPreviewActive = !content.cameraPreviewActive;
        content.cameraTestButton.setButtonText(content.cameraPreviewActive ? "HIDE" : "TEST");
        content.setSize(content.getWidth(), computeContentHeight(content.cameraPreviewActive));
        content.resized();
        content.repaint();
    };
    content.addAndMakeVisible(content.cameraDeviceLabel);
    content.addAndMakeVisible(content.cameraCombo);
    content.addAndMakeVisible(content.cameraTestButton);
    content.addAndMakeVisible(content.cameraPreviewLabel);

    // Camera resolution
    content.cameraResCombo.addItem("1920 x 1080", 1);
    content.cameraResCombo.addItem("1280 x 720",  2);
    content.cameraResCombo.addItem("640 x 480",   3);
    content.cameraResCombo.setSelectedId(2);
    content.addAndMakeVisible(content.cameraResLabel);
    content.addAndMakeVisible(content.cameraResCombo);

    // Flip toggle
    content.cameraFlipToggle.setToggleState(false, juce::dontSendNotification);
    content.addAndMakeVisible(content.cameraFlipLabel);
    content.addAndMakeVisible(content.cameraFlipToggle);

    // Audio device
    content.audioCombo.addItem("(no audio device)", 1);
    content.audioCombo.setSelectedId(1);
    content.audioTestButton.onClick = [this]
    {
        if (toneIsPlaying) stopTone(); else startTone();
    };
    content.addAndMakeVisible(content.audioDeviceLabel);
    content.addAndMakeVisible(content.audioCombo);
    content.addAndMakeVisible(content.audioTestButton);

    // Volume slider
    content.volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    content.volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    content.volumeSlider.setRange(0.0, 1.0);
    content.volumeSlider.setValue(0.8);
    content.volumeSlider.setColour(juce::Slider::thumbColourId,           S_ACCENT);
    content.volumeSlider.setColour(juce::Slider::trackColourId,           S_ACCENT_DIM);
    content.volumeSlider.setColour(juce::Slider::backgroundColourId,      juce::Colour(0xff1a0000));
    content.addAndMakeVisible(content.volumeLabel);
    content.addAndMakeVisible(content.volumeSlider);

    // Sample rate
    content.sampleRateCombo.addItem("44100 Hz", 1);
    content.sampleRateCombo.addItem("48000 Hz", 2);
    content.sampleRateCombo.addItem("96000 Hz", 3);
    content.sampleRateCombo.setSelectedId(1);
    content.addAndMakeVisible(content.sampleRateLabel);
    content.addAndMakeVisible(content.sampleRateCombo);

    // Buffer size
    content.bufferSizeCombo.addItem("128",  1);
    content.bufferSizeCombo.addItem("256",  2);
    content.bufferSizeCombo.addItem("512",  3);
    content.bufferSizeCombo.addItem("1024", 4);
    content.bufferSizeCombo.setSelectedId(2);
    content.addAndMakeVisible(content.bufferSizeLabel);
    content.addAndMakeVisible(content.bufferSizeCombo);
}

SettingsPage::~SettingsPage()
{
    stopTone();
    setLookAndFeel(nullptr);
}

void SettingsPage::initialise()
{
    if (!deviceManager.getCurrentAudioDevice())
        deviceManager.initialiseWithDefaultDevices(0, 2);

    populateAudioDevices();
}

void SettingsPage::populateAudioDevices()
{
    content.audioCombo.clear();
    auto& types = deviceManager.getAvailableDeviceTypes();
    int id = 1;
    for (auto* t : types)
    {
        t->scanForDevices();
        for (auto& name : t->getDeviceNames())
            content.audioCombo.addItem(name, id++);
    }
    if (content.audioCombo.getNumItems() == 0)
        content.audioCombo.addItem("(no audio device)", 1);
    content.audioCombo.setSelectedId(1);
}

void SettingsPage::startTone()
{
    toneSource.setAmplitude(0.3f);
    toneSource.setFrequency(440.0);
    audioPlayer.setSource(&toneSource);
    deviceManager.addAudioCallback(&audioPlayer);
    toneIsPlaying = true;
    content.audioTestButton.setButtonText("STOP");
}

void SettingsPage::stopTone()
{
    if (!toneIsPlaying) return;
    deviceManager.removeAudioCallback(&audioPlayer);
    audioPlayer.setSource(nullptr);
    toneIsPlaying = false;
    content.audioTestButton.setButtonText("TEST");
}

void SettingsPage::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(S_BG_DARK);
    g.fillRect(bounds);

    // Subtle grid
    g.setColour(juce::Colour(0x06ff3333));
    for (int x = 0; x < getWidth(); x += 40)
        g.drawVerticalLine(x, 0.0f, (float)getHeight());
    for (int y = 0; y < getHeight(); y += 40)
        g.drawHorizontalLine(y, 0.0f, (float)getWidth());

    // Border
    g.setColour(S_BORDER_DIM);
    g.drawRect(bounds, 1.0f);

    // Top glow line
    juce::ColourGradient grad(juce::Colours::transparentBlack, bounds.getX(), bounds.getY(),
                               juce::Colours::transparentBlack, bounds.getRight(), bounds.getY(), false);
    grad.addColour(0.2, juce::Colour(0x55ff3333));
    grad.addColour(0.5, juce::Colour(0xffff3333));
    grad.addColour(0.8, juce::Colour(0x55ff3333));
    g.setGradientFill(grad);
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.5f);

    // Corner brackets
    g.setColour(juce::Colour(0xaaff3333));
    g.drawLine(bounds.getX(),          bounds.getY(),           bounds.getX() + 20,    bounds.getY(),       1.5f);
    g.drawLine(bounds.getX(),          bounds.getY(),           bounds.getX(),          bounds.getY() + 20,  1.5f);
    g.drawLine(bounds.getRight() - 20, bounds.getY(),           bounds.getRight(),      bounds.getY(),       1.5f);
    g.drawLine(bounds.getRight(),      bounds.getY(),           bounds.getRight(),      bounds.getY() + 20,  1.5f);
    g.drawLine(bounds.getX(),          bounds.getBottom(),      bounds.getX() + 20,    bounds.getBottom(),  1.5f);
    g.drawLine(bounds.getX(),          bounds.getBottom() - 20, bounds.getX(),          bounds.getBottom(),  1.5f);
    g.drawLine(bounds.getRight() - 20, bounds.getBottom(),      bounds.getRight(),      bounds.getBottom(),  1.5f);
    g.drawLine(bounds.getRight(),      bounds.getBottom() - 20, bounds.getRight(),      bounds.getBottom(),  1.5f);

    // Divider under title bar
    float divY = 52.0f;
    juce::ColourGradient divGrad(juce::Colours::transparentBlack, bounds.getX(), divY,
                                  juce::Colours::transparentBlack, bounds.getRight(), divY, false);
    divGrad.addColour(0.1, S_ACCENT_DIM);
    divGrad.addColour(0.5, S_ACCENT);
    divGrad.addColour(0.9, S_ACCENT_DIM);
    g.setGradientFill(divGrad);
    g.fillRect(bounds.getX() + 28.0f, divY, bounds.getWidth() - 56.0f, 1.0f);

    // Status tag
    g.setColour(juce::Colour(0x44ff3333));
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText("SYS:ONLINE   BUILD:v0.1.0-alpha",
               28, (int)bounds.getBottom() - 22, (int)bounds.getWidth() - 56, 14,
               juce::Justification::centredLeft);
}

void SettingsPage::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;

    auto area = getLocalBounds().reduced(20);

    // Title bar row
    auto titleRow = area.removeFromTop(32);
    titleLabel.setBounds(titleRow.removeFromLeft(titleRow.getWidth() - 100));
    closeButton.setBounds(titleRow.removeFromRight(80).reduced(0, 2));
    area.removeFromTop(8); // gap below divider

    // Bottom status strip
    area.removeFromBottom(24);

    // Viewport fills remaining space
    viewport.setBounds(area);

    // Size the content panel
    int contentH = computeContentHeight(content.cameraPreviewActive);
    content.setSize(area.getWidth(), juce::jmax(contentH, area.getHeight()));
}