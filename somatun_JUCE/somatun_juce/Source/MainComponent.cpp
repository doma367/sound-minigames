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
    pulsefieldPage = std::make_unique<PulseFieldPage> (*this);

    addAndMakeVisible (*landingPage);
    addChildComponent (*settingsPage);
    addChildComponent (*helpOverlay);
    addChildComponent (*fleshSynthPage);
    addChildComponent (*dualcastPage);
    addChildComponent (*pulsefieldPage);

    landingPage->setBounds (getLocalBounds());

    // Force-clear ports 9000 and 9001 before binding.
    // After a crash the previous process often holds the UDP port open.
    // lsof finds any PID bound to these ports and kills it, then we retry
    // the bind in a small loop so a momentary OS delay doesn't fail us.
    {
        juce::ChildProcess killer;
        killer.start (juce::StringArray { "/bin/sh", "-c",
            "lsof -tiUDP:9000 2>/dev/null | xargs kill -9 2>/dev/null; "
            "lsof -tiUDP:9001 2>/dev/null | xargs kill -9 2>/dev/null; "
            "pkill -f somatun_vision 2>/dev/null; true" });
        killer.waitForProcessToFinish (600);
        juce::Thread::sleep (150);
        DBG ("[MainComponent] Port flush complete");
    }

    // Bind OSC port ONCE — retry up to 5x in case the OS needs a moment
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if (sharedOSC.connect (9000))
        {
            sharedOSC.addListener (this);
            oscBound = true;
            DBG ("[MainComponent] Shared OSC bound to port 9000 (attempt "
                 + juce::String (attempt + 1) + ")");
            break;
        }
        juce::Thread::sleep (120);
    }

    if (! oscBound)
        DBG ("[MainComponent] WARNING: Could not bind OSC port 9000 after retries");
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
    if (visionProcess.isRunning())
    {
        DBG ("[MainComponent] Killing existing vision process before relaunch");
        visionProcess.kill();
    }

    {
        juce::ChildProcess killOrphans;
        killOrphans.start (juce::StringArray { "/bin/sh", "-c",
            "pkill -9 -f somatun_vision 2>/dev/null; "
            "lsof -tiTCP:9001 2>/dev/null | xargs kill -9 2>/dev/null; true" });
        killOrphans.waitForProcessToFinish (500);
    }
    juce::Thread::sleep (300);

    // ── Locate somatun_vision.py ───────────────────────────────────────
    juce::File scriptToRun;

    // 1. Bundled inside the .app (production)
    juce::File appBundle     = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::File bundledScript = appBundle.getChildFile ("Contents/Resources/somatun_vision.py");

    if (bundledScript.existsAsFile())
    {
        scriptToRun = bundledScript;
        DBG ("[Vision] Using bundled script: " + scriptToRun.getFullPathName());
    }
    else
    {
        // 2. Walk up from the executable to find the project root (dev builds)
        juce::File sourceDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                   .getParentDirectory()   // Debug/ or Release/
                                   .getParentDirectory()   // build/
                                   .getParentDirectory()   // MacOSX/
                                   .getParentDirectory()   // Builds/
                                   .getParentDirectory();  // somatun_juce/ (project root)

        juce::File devScript = sourceDir.getChildFile ("somatun_vision.py");

        if (devScript.existsAsFile())
        {
            scriptToRun = devScript;
            DBG ("[Vision] Using dev script: " + scriptToRun.getFullPathName());
        }
        else
        {
            DBG ("[ERROR] somatun_vision.py not found in bundle or project root.");
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                "Vision script not found",
                "Could not locate somatun_vision.py.\n\n"
                "Please reinstall Somatun.");
            return;
        }
    }

    // ── Locate python3 ────────────────────────────────────────────────
    juce::StringArray pythonCandidates {
        "/usr/local/bin/python3",      // Intel Homebrew — confirmed working
        "/opt/homebrew/bin/python3",   // Apple Silicon Homebrew
        "/usr/bin/python3"             // system fallback
    };

    juce::String pythonPath;
    for (auto& candidate : pythonCandidates)
        if (juce::File (candidate).existsAsFile())
            { pythonPath = candidate; break; }

    if (pythonPath.isEmpty())
    {
        DBG ("[ERROR] python3 not found.");
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "Python not found",
            "Somatun requires Python 3 with mediapipe installed.\n\n"
            "Please install Python 3.11 from python.org, then run:\n"
            "pip3 install mediapipe opencv-python pythonosc numpy");
        return;
    }

    DBG ("[Vision] Using Python: " + pythonPath);

    juce::StringArray cmd { pythonPath, scriptToRun.getFullPathName() };
    DBG ("[Vision] Launching: " + cmd.joinIntoString (" "));

    if (visionProcess.start (cmd))
        DBG ("[Vision] Process launched successfully");
    else
        DBG ("[ERROR] ChildProcess::start failed");
}