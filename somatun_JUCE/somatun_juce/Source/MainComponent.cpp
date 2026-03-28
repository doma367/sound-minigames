#include "MainComponent.h"
#include "LandingPage.h"
#include "SettingsPage.h"
#include "HelpOverlay.h"
#include "FleshSynthPage.h"

MainComponent::MainComponent()
{
    setSize (800, 600);

    landingPage    = std::make_unique<LandingPage>    (*this);
    settingsPage   = std::make_unique<SettingsPage>   (*this);
    helpOverlay    = std::make_unique<HelpOverlay>    (*this);
    fleshSynthPage = std::make_unique<FleshSynthPage> (*this);

    addAndMakeVisible (*landingPage);
    addChildComponent (*settingsPage);
    addChildComponent (*helpOverlay);
    addChildComponent (*fleshSynthPage);

    landingPage->setBounds (getLocalBounds());
}

MainComponent::~MainComponent() {}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050505));
}

void MainComponent::resized()
{
    if (landingPage != nullptr)
        landingPage->setBounds (getLocalBounds());

    if (fleshSynthPage != nullptr && fleshSynthPage->isVisible())
        fleshSynthPage->setBounds (getLocalBounds());

    auto overlayBounds = getLocalBounds().reduced (80, 60);

    if (settingsPage != nullptr && settingsPage->isVisible())
        settingsPage->setBounds (overlayBounds);

    if (helpOverlay != nullptr && helpOverlay->isVisible())
        helpOverlay->setBounds (overlayBounds);
}

// ============================================================
void MainComponent::showLanding()
{
    settingsPage->setVisible   (false);
    helpOverlay->setVisible    (false);

    if (fleshSynthPage->isVisible())
    {
        fleshSynthPage->stop();
        fleshSynthPage->setVisible (false);
    }

    landingPage->setVisible (true);
    landingPage->toFront    (false);
}

void MainComponent::showSettings()
{
    helpOverlay->setVisible (false);
    settingsPage->setBounds (getLocalBounds().reduced (80, 60));
    settingsPage->initialise();
    settingsPage->setVisible (true);
    settingsPage->toFront   (true);
}

void MainComponent::hideSettings()
{
    settingsPage->setVisible (false);
}

void MainComponent::showHelp()
{
    settingsPage->setVisible (false);
    helpOverlay->setBounds   (getLocalBounds().reduced (80, 60));
    helpOverlay->setVisible  (true);
    helpOverlay->toFront     (true);
}

void MainComponent::hideHelp()
{
    helpOverlay->setVisible (false);
}

void MainComponent::launchMode (int cardIndex)
{
    // Only FleshSynth (card 0) is implemented so far
    if (cardIndex == 0)
    {
        settingsPage->setVisible (false);
        helpOverlay->setVisible  (false);
        landingPage->setVisible  (false);

        fleshSynthPage->setBounds (getLocalBounds());
        fleshSynthPage->setVisible (true);
        fleshSynthPage->toFront   (true);
        fleshSynthPage->start();
    }
    // cardIndex 1 (Pulsefield) and 2 (Dualcast) — not yet implemented
}