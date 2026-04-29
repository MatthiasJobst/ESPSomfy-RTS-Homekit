// SomfyMQTTPublisher.h — Socket-emit and HomeKit state publishing for a SomfyShade.
// Owns emitState(), emitCommand(), publishState() (HomeKit notification),
// and the flipPosition flag. Reads shade state through a back-pointer set by the
// SomfyShade() constructor; writes nothing back to the shade.
#pragma once
#include "SomfyFrame.h"

class SomfyShade;

class SomfyMQTTPublisher {
public:
    SomfyShade *shade = nullptr;
    bool flipPosition = false;

    void publishState();
    void emitState(uint8_t num, const char *evt);
    void emitCommand(uint8_t num, somfy_commands cmd, const char *source,
                     uint32_t sourceAddress, const char *evt);
};
