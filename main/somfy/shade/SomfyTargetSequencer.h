// SomfyTargetSequencer.h — Programmatic position-seek logic for SomfyShade.
// Owns moveToTarget, moveToTiltTarget, moveToMyPosition, and setMyPosition.
// All four methods write the shared MotionState flags (settingPos / settingTiltPos /
// settingMyPos) to coordinate with MovementTracker (checkMovement) and CommandProcessor
// (processFrame external-command abort).
#pragma once

class SomfyShade;  // forward declaration — full type in SomfyShade.h

class SomfyTargetSequencer {
public:
    SomfyShade* shade = nullptr;
    // Favorite/My position (owned here; read by MQTT, Persistence, JSON as shade->targetSequencer.myPos)
    float myPos     = -1.0f;
    float myTiltPos = -1.0f;
    void moveToTarget(float pos, float tilt = -1.0f);
    void moveToTiltTarget(float target);
    void moveToMyPosition();
    void setMyPosition(int8_t pos, int8_t tilt = -1);
};
