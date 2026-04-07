#include "MainComponent.h"
#include "LandingPage.h"
#include "SettingsPage.h"
#include "HelpOverlay.h"
#include "FleshSynthpage.h"
#include "Dualcastpage.h"
#include "Pulsefieldpage.h"

MainComponent::MainComponent()
{
    setSize (800, 600);

    landingPage    = std::make_unique<LandingPage>    (*this);
    settingsPage   = std::make_unique<SettingsPage>   (*this);
    helpOverlay    = std::make_unique<HelpOverlay>    (*this);
    fleshSynthPage = std::make_unique<FleshSynthPage> (*this);
    dualcastPage   = std::make_unique<DualcastPage>   (*this);
    pulsefieldPage = std::make_unique<PulsefieldPage> (*this);

    addAndMakeVisible (*landingPage);
    addChildComponent (*settingsPage);
    addChildComponent (*helpOverlay);
    addChildComponent (*fleshSynthPage);
    addChildComponent (*dualcastPage);
    addChildComponent (*pulsefieldPage);

    landingPage->setBounds (getLocalBounds());

    // Bind OSC port ONCE — never disconnect until app closes
    if (sharedOSC.connect (9000))
    {
        sharedOSC.addListener (this);
        oscBound = true;
        DBG ("[MainComponent] Shared OSC bound to port 9000");
    }
    else
    {
        DBG ("[MainComponent] WARNING: Could not bind OSC port 9000");
    }
}

MainComponent::~MainComponent()
{
    if (oscBound)
    {
        sharedOSC.removeListener (this);
        sharedOSC.disconnect();
    }

    if (visionProcess.isRunning())
        visionProcess.kill();
}

// ── OSC dispatch ─────────────────────────────────────────────────────────────
void MainComponent::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto addr = m.getAddressPattern().toString();
    if      (addr == "/pose"  && poseCallback)  poseCallback  (m);
    else if (addr == "/hands" && handsCallback) handsCallback (m);
}

void MainComponent::setPoseCallback (std::function<void(const juce::OSCMessage&)> fn)
{
    poseCallback = std::move (fn);
}

void MainComponent::setHandsCallback (std::function<void(const juce::OSCMessage&)> fn)
{
    handsCallback = std::move (fn);
}

void MainComponent::clearOSCCallbacks()
{
    poseCallback  = nullptr;
    handsCallback = nullptr;
}

// ── paint / resized ───────────────────────────────────────────────────────────
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050505));
}

void MainComponent::resized()
{
    if (landingPage    != nullptr) landingPage->setBounds (getLocalBounds());

    if (fleshSynthPage != nullptr && fleshSynthPage->isVisible())
        fleshSynthPage->setBounds (getLocalBounds());

    if (dualcastPage != nullptr && dualcastPage->isVisible())
        dualcastPage->setBounds (getLocalBounds());

    if (pulsefieldPage != nullptr && pulsefieldPage->isVisible())
        pulsefieldPage->setBounds (getLocalBounds());

    auto overlayBounds = getLocalBounds().reduced (80, 60);

    if (settingsPage != nullptr && settingsPage->isVisible())
        settingsPage->setBounds (overlayBounds);

    if (helpOverlay != nullptr && helpOverlay->isVisible())
        helpOverlay->setBounds (overlayBounds);
}

// ── Navigation ────────────────────────────────────────────────────────────────
void MainComponent::showLanding()
{
    // Clear OSC callbacks FIRST before stopping pages
    clearOSCCallbacks();

    settingsPage->setVisible (false);
    helpOverlay->setVisible  (false);

    if (fleshSynthPage->isVisible())
    {
        fleshSynthPage->stop();
        fleshSynthPage->setVisible (false);
    }
    if (dualcastPage->isVisible())
    {
        dualcastPage->stop();
        dualcastPage->setVisible (false);
    }
    if (pulsefieldPage->isVisible())
    {
        pulsefieldPage->stop();
        pulsefieldPage->setVisible (false);
    }

    if (visionProcess.isRunning())
        visionProcess.kill();

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
    // Clear OSC callbacks before switching modes
    clearOSCCallbacks();

    settingsPage->setVisible (false);
    helpOverlay->setVisible  (false);
    landingPage->setVisible  (false);

    // Stop any currently active page
    if (fleshSynthPage->isVisible()) { fleshSynthPage->stop(); fleshSynthPage->setVisible (false); }
    if (dualcastPage->isVisible())   { dualcastPage->stop();   dualcastPage->setVisible   (false); }
    if (pulsefieldPage->isVisible()) { pulsefieldPage->stop(); pulsefieldPage->setVisible (false); }

    launchVisionProcess();

    if (cardIndex == 0)       // FleshSynth
    {
        fleshSynthPage->setBounds  (getLocalBounds());
        fleshSynthPage->setVisible (true);
        fleshSynthPage->toFront    (true);
        fleshSynthPage->start();   // start() registers its pose callback
    }
    else if (cardIndex == 1)  // Pulsefield
    {
        pulsefieldPage->setBounds  (getLocalBounds());
        pulsefieldPage->setVisible (true);
        pulsefieldPage->toFront    (true);
        pulsefieldPage->start();   // start() registers its hands callback
    }
    else if (cardIndex == 2)  // Dualcast
    {
        dualcastPage->setBounds  (getLocalBounds());
        dualcastPage->setVisible (true);
        dualcastPage->toFront    (true);
        dualcastPage->start();    // start() registers both callbacks
    }
}

void MainComponent::launchVisionProcess()
{
    // Always kill and restart — ensures clean Python process each time
    if (visionProcess.isRunning())
    {
        DBG ("[MainComponent] Killing existing vision process before relaunch");
        visionProcess.kill();
        juce::Thread::sleep (400);  // let OS release port 9001
    }

    juce::File appBundle      = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::File scriptInBundle = appBundle.getChildFile ("Contents/Resources/somatun_vision.py");

    DBG ("App bundle path: " + appBundle.getFullPathName());
    DBG ("Looking for vision script in bundle at: " + scriptInBundle.getFullPathName());

    juce::File scriptToRun = scriptInBundle;

    if (! scriptInBundle.existsAsFile())
    {
        juce::File scriptInSource ("/Users/doma367/sound-minigames/somatun_JUCE/somatun_juce/somatun_vision.py");
        DBG ("Bundle script not found. Checking source path: " + scriptInSource.getFullPathName());

        if (scriptInSource.existsAsFile())
        {
            scriptToRun = scriptInSource;
            DBG ("Using source-tree vision script: " + scriptToRun.getFullPathName());
        }
        else
        {
            DBG ("[ERROR] somatun_vision.py not found.");
            return;
        }
    }

    juce::String    pythonPath = "/Users/doma367/sound-minigames/.venv/bin/python";
    juce::StringArray cmd;
    cmd.add (pythonPath);
    cmd.add (scriptToRun.getFullPathName());

    DBG ("Launching vision helper: " + cmd.joinIntoString (" "));

    if (visionProcess.start (cmd))
        DBG ("Vision helper launched successfully");
    else
        DBG ("[ERROR] ChildProcess::start failed.");
}
