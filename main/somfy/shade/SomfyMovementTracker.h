#pragma once
#include <cstdint>
#include "SomfyMotionState.h"

class SomfyShade;

class SomfyMovementTracker {
  public:
    SomfyShade *shade = nullptr;
    // Interpolation scratch-state (owned here; reset at the start of each command)
    uint64_t moveStart = 0;
    uint64_t tiltStart = 0;
    float startPos = 0.0f;
    float startTiltPos = 0.0f;
    // Direction of last completed movement (-1=up, 1=down)
    int8_t lastMovement = 0;
    // In-progress programmatic motion flags
    MotionState motionState;
    void checkMovement();
    void setMovement(int8_t dir);
    void setTiltMovement(int8_t dir);

  private:
    // Returns tilt_first flag; sets shade->direction and shade->tiltDirection.
    bool computeDirections();
    // Ticks sun/wind flag timers and adjusts shade target/tiltTarget accordingly.
    void tickFlagTimers(uint64_t curTime);
    // Interpolates position along dir (+1=down, -1=up) given elapsed ms and total travel time.
    float calcInterpolatedPos(float startPct, uint64_t elapsed, int32_t totalTime, int8_t dir);
    // Snaps pos to target, optionally sends My, resets direction. endpoint = 100.0 (down) or 0.0 (up).
    void handlePosTargetReached(float endpoint, uint64_t curTime);
    // Snaps tiltPos to tiltTarget, optionally sends My, resets tiltDirection. endpoint = 100.0 or 0.0.
    void handleTiltTargetReached(float endpoint);
};
