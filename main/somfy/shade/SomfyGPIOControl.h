// SomfyGPIOControl.h — Encapsulates the GPIO relay/remote output state and logic
// for a SomfyShade.  Owns the six GPIO fields and provides the three operations:
//   setGPIOs   — drive relay/release outputs each loop tick
//   triggerGPIOs — fire GPIO pins on incoming RF command (GP_Remote)
//   usesPin    — check whether a given pin number is occupied
//
// Shade-level state (proto, shadeType, tiltType, direction, tiltDirection,
// currentPos, isToggle) is passed by the caller; this class owns no shade state.
#pragma once
#include "SomfyFrame.h"

class SomfyGPIOControl {
public:
    uint8_t  gpioUp      = 0;
    uint8_t  gpioDown    = 0;
    uint8_t  gpioMy      = 0;
    int8_t   gpioDir     = 0;
    uint8_t  gpioFlags   = 0;
    uint32_t gpioRelease = 0;

    // Drive relay/release lines.  Call once per loop tick.
    void setGPIOs(radio_proto proto,
                  float currentPos,
                  int8_t direction,
                  int8_t tiltDirection,
                  shade_types shadeType,
                  tilt_types tiltType);

    // Pulse GP_Remote output pins for the command in frame.
    void triggerGPIOs(somfy_frame_t &frame,
                      radio_proto proto,
                      shade_types shadeType,
                      bool isToggle);

    // Return true if this shade is using the given pin number.
    bool usesPin(uint8_t pin,
                 radio_proto proto,
                 shade_types shadeType,
                 bool isToggle) const;
};
