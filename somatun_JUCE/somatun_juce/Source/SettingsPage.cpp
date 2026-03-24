#include "SettingsPage.h"
#include "MainComponent.h"

SettingsPage::SettingsPage(MainComponent& mc) : mainComponent(mc)
{
    // Title
    titleLabel.setText("Settings", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(32.0f)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    // Camera label + combo
    cameraLabel.setText("Camera Input:", juce::dontSendNotification);
    cameraLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(cameraLabel);

    cameraComboBox.addItem("(no camera selected)", 1);
    cameraComboBox.setSelectedId(1);
    addAndMakeVisible(cameraComboBox);

    // Audio label + combo
    audioLabel.setText("Audio Output:", juce::dontSendNotification);
    audioLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(audioLabel);

    deviceManager.initialiseWithDefaultDevices(0, 2);
    auto& currentDevice = *deviceManager.getCurrentAudioDevice();
    audioComboBox.addItem(currentDevice.getName(), 1);
    audioComboBox.setSelectedId(1);
    addAndMakeVisible(audioComboBox);

    // Back button
    backButton.onClick = [this] { mainComponent.showLanding(); };
    backButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f3460));
    backButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(backButton);
}

void SettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced(60);

    titleLabel.setBounds(area.removeFromTop(60));
    area.removeFromTop(30);

    auto row1 = area.removeFromTop(30);
    cameraLabel.setBounds(row1.removeFromLeft(150));
    cameraComboBox.setBounds(row1.removeFromLeft(300));

    area.removeFromTop(20);

    auto row2 = area.removeFromTop(30);
    audioLabel.setBounds(row2.removeFromLeft(150));
    audioComboBox.setBounds(row2.removeFromLeft(300));

    backButton.setBounds(area.removeFromBottom(40).removeFromLeft(100));
}