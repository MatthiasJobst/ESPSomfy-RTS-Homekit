// SomfyMotionState.h — Shared flag set that tracks whether the shade is executing
// a programmatic position-seek (settingPos), tilt-seek (settingTiltPos), or
// my-position capture (settingMyPos).  Grouped here so that TargetSequencer,
// CommandProcessor, and MovementTracker can all reference the same struct without
// needing a back-pointer to all of SomfyShade for these three fields alone.
#pragma once

struct MotionState {
    bool settingPos     = false;
    bool settingTiltPos = false;
    bool settingMyPos   = false;
    void clear() { settingPos = settingTiltPos = settingMyPos = false; }
};
