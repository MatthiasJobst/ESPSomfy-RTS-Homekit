#pragma once
#include <cstdint>
#include "SomfyMotionState.h"

class SomfyShade;

class SomfyMovementTracker {
public:
    SomfyShade *shade = nullptr;
    // Interpolation scratch-state (owned here; reset at the start of each command)
    uint64_t moveStart    = 0;
    uint64_t tiltStart    = 0;
    float    startPos     = 0.0f;
    float    startTiltPos = 0.0f;
    // Direction of last completed movement (-1=up, 1=down)
    int8_t   lastMovement = 0;
    // In-progress programmatic motion flags
    MotionState motionState;
    void checkMovement();
    void setMovement(int8_t dir);
    void setTiltMovement(int8_t dir);
};
