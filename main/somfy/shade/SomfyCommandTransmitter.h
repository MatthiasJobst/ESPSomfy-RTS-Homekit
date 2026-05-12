// SomfyCommandTransmitter.h — Encapsulates outbound RF command logic for a SomfyShade.
// Owns sendCommand(), sendTiltCommand(), linkRemote(), and unlinkRemote().
// Reads/writes shade state through the back-pointer set by SomfyShade().
#pragma once
#include "SomfyFrame.h"
#include "SomfyRemote.h"

class SomfyShade; // forward declaration

class SomfyCommandTransmitter {
  public:
    SomfyShade *shade = nullptr; // set by SomfyShade() constructor
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];

    // High-level outbound commands (called by the web/MQTT layer).
    void sendCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    void sendTiltCommand(somfy_commands cmd);

    // Linked-remote management — persists addresses to NVS when useNVS().
    bool linkRemote(uint32_t address, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t address);
};
