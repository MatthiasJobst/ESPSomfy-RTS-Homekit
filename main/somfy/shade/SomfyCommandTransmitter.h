// SomfyCommandTransmitter.h — Encapsulates outbound RF command logic for a SomfyShade.
// Owns sendCommand() and sendTiltCommand(). Reads/writes shade state through the
// back-pointer set by SomfyShade(). Linked-remote management lives in SomfyLinkedRemotes.
#pragma once
#include "SomfyFrame.h"
#include "SomfyRemote.h"

class SomfyShade; // forward declaration

class SomfyCommandTransmitter {
  public:
    SomfyShade *shade = nullptr; // set by SomfyShade() constructor

    // High-level outbound commands (called by the web/MQTT layer).
    void sendCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    void sendTiltCommand(somfy_commands cmd);
};
