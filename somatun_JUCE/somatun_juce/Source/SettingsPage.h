// SettingsPage.h
#pragma once
#include <JuceHeader.h>
#include "SomatunLookAndFeel.h"

class MainComponent;

//==============================================================================
// Inner scrollable content panel
class SettingsContent : public juce::Component
{
public:
    juce::Label    sectionGeneralLabel;
    juce::Label    windowModeLabel;
    juce::ComboBox windowModeCombo;

    juce::Label    sectionInputLabel;
    juce::Label    cameraDeviceLabel;
    juce::ComboBox cameraCombo;
    juce::TextButton cameraTestButton { "TEST" };
    juce::Label    cameraPreviewLabel;
    juce::Label    cameraResLabel;
    juce::ComboBox cameraResCombo;
    juce::Label    cameraFlipLabel;
    juce::ToggleButton cameraFlipToggle;

    juce::Label    sectionOutputLabel;
    juce::Label    audioDeviceLabel;
    juce::ComboBox audioCombo;
    juce::TextButton audioTestButton { "TEST" };
    juce::Label    volumeLabel;
    juce::Slider   volumeSlider;
    juce::Label    sampleRateLabel;
    juce::ComboBox sampleRateCombo;
    juce::Label    bufferSizeLabel;
    juce::ComboBox bufferSizeCombo;

    bool cameraPreviewActive = false;
    juce::Rectangle<int> lastPreviewBounds;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    void drawRowCard(juce::Graphics& g, juce::Rectangle<int> bounds);
};

//==============================================================================
class SettingsPage : public juce::Component
{
public:
    SettingsPage(MainComponent& mc);
    ~SettingsPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void initialise();

private:
    MainComponent& mainComponent;
    SomatunLookAndFeel laf;

    juce::Label      titleLabel;
    juce::TextButton closeButton { "CLOSE" };

    juce::Viewport   viewport;
    SettingsContent  content;

    juce::AudioDeviceManager       deviceManager;
    juce::ToneGeneratorAudioSource toneSource;
    juce::AudioSourcePlayer        audioPlayer;
    bool                           toneIsPlaying = false;

    std::unique_ptr<juce::CameraDevice> cameraDevice;
    std::unique_ptr<juce::Component>    cameraPreviewComponent;

    void openCamera();
    void closeCamera();
    void startTone();
    void stopTone();
    void populateAudioDevices();
    void populateCameraDevices();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};