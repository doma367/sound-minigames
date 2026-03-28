#pragma once
#include <JuceHeader.h>

struct PoseFrame;  // defined in FleshSynthPage.h

// ============================================================
//  PoseTracker
//
//  Runs on its own juce::Thread.  Each iteration:
//    1. Grabs a frame from OpenCV VideoCapture
//    2. Flips it horizontally (mirrors the user, matching the Python)
//    3. Converts BGR -> RGBA and calls pose_tracker_process()
//       from libpose_api.dylib (Google's pose_dylib example)
//    4. Packs the 33 returned landmarks into a PoseFrame
//    5. Fires onPoseReady  (pose data)  on the tracker thread
//       and onFrameReady   (BGR juce::Image) for the UI to display
//
//  Thread safety: both callbacks are called from the tracker thread.
//  FleshSynthPage stores results under a CriticalSection and reads
//  them on the message thread via its 40 Hz Timer.
//
//  Build requirements:
//    - Link: opencv_core  opencv_imgproc  opencv_videoio  pose_api
//    - Add libpose_api.dylib next to your app bundle
//    - Add pose_api.h to your include path
// ============================================================
class PoseTracker : public juce::Thread
{
public:
    using PoseCallback  = std::function<void (const PoseFrame&)>;
    using FrameCallback = std::function<void (juce::Image)>;

    // cameraIndex  — OpenCV device index (0 = system default)
    // onPoseReady  — receives a PoseFrame every processed frame
    // onFrameReady — receives a juce::Image (RGB, already flipped)
    PoseTracker (int cameraIndex,
                 PoseCallback  onPoseReady,
                 FrameCallback onFrameReady = {});

    ~PoseTracker() override;

    void run() override;

    void setCameraIndex (int idx) { cameraIndex = idx; }

private:
    int           cameraIndex;
    PoseCallback  poseCallback;
    FrameCallback frameCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoseTracker)
};