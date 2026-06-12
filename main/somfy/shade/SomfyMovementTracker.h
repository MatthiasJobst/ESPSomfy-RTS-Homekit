#pragma once
#include <cstdint>
#include "SomfyMotionState.h"

class SomfyShade;

/**
 * @brief Interpolation scratch-state for one moving axis (shade or tilt):
 *        when the current move began and the position it began from.
 */
struct MoveAxis {
    uint64_t startMs = 0;  /**< millis() when the current move began. */
    float startPct = 0.0f; /**< Position % (0..100) at move start. */
};

/**
 * @brief How an axis is moving this tick: full-range travel time and direction.
 */
struct TravelSpec {
    int32_t totalMs; /**< Full-range travel duration in ms. */
    int8_t dir;      /**< +1 = toward 100%, -1 = toward 0%. */
};

class SomfyMovementTracker {
  public:
    SomfyShade *shade = nullptr;
    // Per-axis interpolation scratch-state. Public only as a unit-test seam
    // (TestableShade seeds startMs/startPct directly); production code goes
    // through reset()/resetMovement()/setMovement()/setTiltMovement().
    MoveAxis shadeAxis;
    MoveAxis tiltAxis;
    // Direction of last completed movement (-1=up, 1=down)
    int8_t lastMovement = 0;
    // In-progress programmatic motion flags
    MotionState motionState;
    void checkMovement();
    void setMovement(int8_t dir);
    void setTiltMovement(int8_t dir);
    // Reset all interpolation scratch-state (used by SomfyShade::clear()).
    void reset();
    // Seed both axes to start a fresh movement at time t from the shade's current position.
    void resetMovement(uint64_t t);

  private:
    // Returns tilt_first flag; sets shade->direction and shade->tiltDirection.
    bool computeDirections();
    // Ticks sun/wind flag timers and adjusts shade target/tiltTarget accordingly.
    void tickFlagTimers(uint64_t curTime);
    // Interpolated position % for `axis` sampled at `now`, given the travel spec.
    float calcInterpolatedPos(const MoveAxis &axis, uint64_t now, TravelSpec travel);
    // Snaps pos to target, optionally sends My, resets direction.
    // The full-travel endpoint (0 or 100) is derived from shade->direction.
    void handlePosTargetReached(uint64_t curTime);
    // Snaps tiltPos to tiltTarget, optionally sends My, resets tiltDirection.
    // The full-travel endpoint (0 or 100) is derived from shade->tiltDirection.
    void handleTiltTargetReached();
};
