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

    // ---- launch Python vision helper ----
    juce::File appBundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::File scriptInBundle = appBundle.getChildFile ("Contents/Resources/somatun_vision.py");

    DBG ("App bundle path: " + appBundle.getFullPathName());
    DBG ("Looking for vision script in bundle at: " + scriptInBundle.getFullPathName());

    juce::File scriptToRun = scriptInBundle;

    if (! scriptInBundle.existsAsFile())
    {
        // Fallback to running directly from the source tree (dev setup)
        juce::File scriptInSource ("/Users/doma367/sound-minigames/somatun_JUCE/somatun_juce/somatun_vision.py");
        DBG ("Bundle script not found. Checking source path: " + scriptInSource.getFullPathName());

        if (scriptInSource.existsAsFile())
        {
            scriptToRun = scriptInSource;
            DBG ("Using source-tree vision script: " + scriptToRun.getFullPathName());
        }
        else
        {
            DBG ("[ERROR] somatun_vision.py not found in bundle AND not at source path.");
            DBG ("Expected bundle path: " + scriptInBundle.getFullPathName());
            DBG ("Expected source path:  " + scriptInSource.getFullPathName());
        }
    }

    if (scriptToRun.existsAsFile())
    {
        juce::String pythonPath = "/Users/doma367/sound-minigames/.venv/bin/python";

        juce::StringArray cmd;
        cmd.add (pythonPath);
        cmd.add (scriptToRun.getFullPathName());

        DBG ("Launching vision helper with: " + cmd.joinIntoString (" "));

        if (visionProcess.start (cmd))
        {
            DBG ("Vision helper launched successfully using venv Python");
        }
        else
        {
            DBG ("[ERROR] ChildProcess::start failed to launch the vision helper.");
        }
    }
    else
    {
        DBG ("[ERROR] No valid somatun_vision.py found to run.");
    }
}

MainComponent::~MainComponent()
{
    if (visionProcess.isRunning())
        visionProcess.kill();
}

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