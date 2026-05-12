#pragma once
#include <cstdint>
#include "SomfyFrame.h"

class SomfyShade;

class SomfyCommandProcessor {
  public:
    SomfyShade *shade = nullptr;
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
};
