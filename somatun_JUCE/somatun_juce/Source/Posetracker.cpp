// PoseTracker.cpp
//
// Links against:
//   opencv_core  opencv_imgproc  opencv_videoio
//   pose_api   (libpose_api.dylib — Google's pose_dylib example)
//
// pose_api.h declares roughly:
//   typedef struct PoseTrackerOpaque PoseTrackerOpaque;
//   PoseTrackerOpaque* pose_tracker_create(const char* model_path);
//   void               pose_tracker_destroy(PoseTrackerOpaque*);
//
//   // Returns 1 if landmarks were detected, 0 otherwise.
//   // landmarks_out: float array of 33 * 5 floats:
//   //   [x, y, z, visibility, presence] per landmark, normalised 0-1
//   int pose_tracker_process(PoseTrackerOpaque*,
//                            int width, int height, int stride,
//                            const uint8_t* rgba_pixels,
//                            float* landmarks_out);  // caller allocs 33*5 floats
//
// Landmark index layout is identical to Python mediapipe.solutions.pose:
//   7=LEFT_EAR  8=RIGHT_EAR
//  11=LEFT_SHOULDER  12=RIGHT_SHOULDER
//  13=LEFT_ELBOW     14=RIGHT_ELBOW
//  15=LEFT_WRIST     16=RIGHT_WRIST

#include "PoseTracker.h"
#include "FleshSynthPage.h"   // PoseFrame, PoseLandmark

// pose_dylib plain C API
#include "pose_api.h"

// OpenCV — only the modules we actually use
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

// ============================================================
//  Helper: OpenCV BGR Mat  ->  juce::Image (RGB)
// ============================================================
static juce::Image matToJuceImage (const cv::Mat& bgr)
{
    const int w = bgr.cols;
    const int h = bgr.rows;

    juce::Image img (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
    {
        const uchar* src = bgr.ptr<uchar> (y);
        uint8_t*     dst = bd.getLinePointer (y);

        for (int x = 0; x < w; ++x)
        {
            dst[0] = src[2];  // R  (BGR -> RGB)
            dst[1] = src[1];  // G
            dst[2] = src[0];  // B
            src += 3;
            dst += 3;
        }
    }

    return img;
}

// ============================================================
PoseTracker::PoseTracker (int camIdx,
                           PoseCallback  cb,
                           FrameCallback fcb)
    : juce::Thread ("PoseTracker"),
      cameraIndex   (camIdx),
      poseCallback   (std::move (cb)),
      frameCallback  (std::move (fcb))
{
}

PoseTracker::~PoseTracker()
{
    signalThreadShouldExit();
    waitForThreadToExit (3000);
}

// ============================================================
void PoseTracker::run()
{
    // ---- Create pose tracker via dylib C API ----
    // Pass nullptr to use the model bundled inside the dylib
    PoseTrackerOpaque* tracker = pose_tracker_create (nullptr);
    if (tracker == nullptr)
    {
        DBG ("PoseTracker: pose_tracker_create() returned null");
        return;
    }

    // ---- Open camera ----
    cv::VideoCapture cap (cameraIndex);
    if (! cap.isOpened())
    {
        DBG ("PoseTracker: could not open camera " << cameraIndex);
        pose_tracker_destroy (tracker);
        return;
    }
    cap.set (cv::CAP_PROP_FRAME_WIDTH,  640);
    cap.set (cv::CAP_PROP_FRAME_HEIGHT, 480);

    // Landmark buffer: 33 landmarks * 5 floats (x, y, z, visibility, presence)
    constexpr int kLandmarkCount = 33;
    constexpr int kFloatsPerLandmark = 5;
    float landmarkBuf[kLandmarkCount * kFloatsPerLandmark] {};

    cv::Mat frameBGR, frameRGBA;

    while (! threadShouldExit())
    {
        // ---- Grab frame ----
        if (! cap.read (frameBGR) || frameBGR.empty())
            continue;

        // Mirror horizontally — matches Python: cv2.flip(frame, 1)
        cv::flip (frameBGR, frameBGR, 1);

        // ---- Push camera frame to UI ----
        if (frameCallback)
            frameCallback (matToJuceImage (frameBGR));

        // ---- Convert BGR -> RGBA for pose_tracker_process ----
        cv::cvtColor (frameBGR, frameRGBA, cv::COLOR_BGR2RGBA);

        const int w      = frameRGBA.cols;
        const int h      = frameRGBA.rows;
        const int stride = (int) frameRGBA.step;          // bytes per row

        // ---- Run inference ----
        const int detected = pose_tracker_process (
            tracker,
            w, h, stride,
            frameRGBA.data,
            landmarkBuf);

        // ---- Pack result into PoseFrame ----
        PoseFrame pf;
        pf.valid = (detected == 1);

        if (pf.valid)
        {
            for (int i = 0; i < kLandmarkCount; ++i)
            {
                // Layout: x y z visibility presence  (5 floats per landmark)
                pf.lm[i].x = landmarkBuf[i * kFloatsPerLandmark + 0];
                pf.lm[i].y = landmarkBuf[i * kFloatsPerLandmark + 1];
                // z and visibility are available if needed later
            }
        }

        if (poseCallback) poseCallback (pf);
    }

    // ---- Clean shutdown ----
    cap.release();
    pose_tracker_destroy (tracker);
}