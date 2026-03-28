#ifndef POSE_API_H_
#define POSE_API_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PoseTracker PoseTracker;

typedef struct {
    float x;
    float y;
    float z;
    float visibility;
} PoseLandmark;

// Returns opaque handle or NULL on failure
PoseTracker* pose_tracker_create(const char* model_path);

// Returns number of landmarks written (33 or 0 on error)
int pose_tracker_process(PoseTracker* tracker,
                        const unsigned char* rgb_data,   // tightly packed RGB
                        int width,
                        int height,
                        PoseLandmark* out_landmarks);     // must have space for 33

void pose_tracker_destroy(PoseTracker* tracker);

#ifdef __cplusplus
}
#endif

#endif