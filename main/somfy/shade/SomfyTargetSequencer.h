// SomfyTargetSequencer.h — Programmatic position-seek logic for SomfyShade.
// Owns moveToTarget, moveToTiltTarget, moveToMyPosition, and setMyPosition.
// All four methods write the shared MotionState flags (settingPos / settingTiltPos /
// settingMyPos) to coordinate with MovementTracker (checkMovement) and CommandProcessor
// (processFrame external-command abort).
#pragma once

#include <stdint.h>            // uint8_t / int8_t
#include "../SomfyFrame.h"     // somfy_commands

class SomfyShade;  // forward declaration — full type in SomfyShade.h

class SomfyTargetSequencer {
    somfy_commands moveDirection(float &pos, float tilt);
    // Lift moves need a ~1 s burst so the motor registers a sustained press
    // rather than a tap. Tilt-only modes keep the short-burst path; euromode
    // still needs its TILT_REPEATS signalling.
    uint8_t        repeatCount() const;
    void           applyMove(somfy_commands cmd, float pos, float tilt, uint8_t repeats);
public:
    SomfyShade* shade = nullptr;
    // Favorite/My position (owned here; read by MQTT, Persistence, JSON as shade->targetSequencer.myPos)
    float myPos     = -1.0f;
    float myTiltPos = -1.0f;
    void moveToTarget(float pos, float tilt = -1.0f);
    void moveToTargetForced(float pos, float tilt = -1.0f);
    void moveToTiltTarget(float target);
    void moveToMyPosition();
    void setMyPosition(int8_t pos, int8_t tilt = -1);
};
