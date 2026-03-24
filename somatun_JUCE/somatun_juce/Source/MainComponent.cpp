#include "MainComponent.h"
#include "LandingPage.h"
#include "SettingsPage.h"
#include "HelpOverlay.h"

MainComponent::MainComponent()
{
    setSize(800, 600);

    landingPage  = std::make_unique<LandingPage>(*this);
    settingsPage = std::make_unique<SettingsPage>(*this);
    helpOverlay  = std::make_unique<HelpOverlay>(*this);

    addChildComponent(*landingPage);
    addChildComponent(*settingsPage);
    addChildComponent(*helpOverlay);

    showLanding();
}

MainComponent::~MainComponent() {}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e)); // dark background
}

void MainComponent::resized()
{
    if (landingPage != nullptr && landingPage->isVisible())
        landingPage->setBounds(getLocalBounds());

    if (settingsPage != nullptr && settingsPage->isVisible())
        settingsPage->setBounds(getLocalBounds());

    if (helpOverlay != nullptr && helpOverlay->isVisible())
        helpOverlay->setBounds(getLocalBounds().reduced(100));
}

void MainComponent::showLanding()
{
    landingPage->setVisible(true);
    settingsPage->setVisible(false);
    helpOverlay->setVisible(false);
    landingPage->setBounds(getLocalBounds());
}

void MainComponent::showSettings()
{
    settingsPage->setVisible(true);
    landingPage->setVisible(false);
    helpOverlay->setVisible(false);
    settingsPage->setBounds(getLocalBounds());
}

void MainComponent::showHelp()
{
    helpOverlay->setVisible(true);
    helpOverlay->setBounds(getLocalBounds().reduced(100));
    helpOverlay->toFront(true);
}

void MainComponent::hideHelp()
{
    helpOverlay->setVisible(false);
}
