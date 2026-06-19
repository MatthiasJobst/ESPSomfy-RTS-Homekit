#pragma once
#include <cstdint>
#include "SomfyFrame.h"

class SomfyShade;
class SomfyMovementTracker;

class SomfyCommandProcessor {
  public:
    SomfyShade *shade = nullptr;
    // Sibling that owns the motion state (lastMovement, motionState) and interpolation
    // reset this processor coordinates with. Wired by the SomfyShade constructor so the
    // collaboration is direct instead of routed through SomfyShade delegators.
    SomfyMovementTracker *movementTracker = nullptr;
    // Timing and step config (owned here; exposed via SomfyShade getters/setters)
    uint32_t upTime = 10000;
    uint32_t downTime = 10000;
    uint32_t tiltTime = 7000;
    uint16_t stepSize = 100;
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat = 1);
    void processWaitingFrame();
    void processSensorCommand(somfy_frame_t &frame, uint64_t curTime);
    void processFlagCommand(bool internal, somfy_frame_t &frame);
    void processSunFlagCommand(bool internal, somfy_frame_t &frame);
    void processMyCommand(bool internal, somfy_frame_t &frame, uint64_t curTime);
    void processUpDownCommand(somfy_commands cmd, int8_t moveDir, bool internal, somfy_frame_t &frame,
                              uint64_t curTime);
    void processStepCommand(somfy_commands cmd, int8_t stepDir, bool internal, somfy_frame_t &frame);
  private:
    bool isDryContact();
    bool isNotFromLinkedRemote(somfy_frame_t &frame);
    void setShadeTargetToggle();
    void setShadeCmdSunFlag();
    void setShadeCmdFlag();
    bool setShadeCmdStepDown();
    bool setShadeCmdStepUp();
    void setShadeCmdMy();
    void setShadeCmdDown(const uint64_t curTime, uint8_t repeat);
    void setShadeCmdUp(uint8_t repeat);
};
