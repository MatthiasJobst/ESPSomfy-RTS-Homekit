// SomfyMQTTPublisher.h — Encapsulates all MQTT and socket-emit publishing for a SomfyShade.
// Owns the low-level publish() overloads, publishState(), publishDisco(), unpublish(),
// emitState(), and emitCommand().  Reads shade state through a back-pointer set by the
// SomfyShade() constructor; writes nothing back to the shade.
#pragma once
#include "SomfyFrame.h"

class SomfyShade; // forward declaration — full definition in SomfyMQTTPublisher.cpp

class SomfyMQTTPublisher {
  public:
    SomfyShade *shade = nullptr; // set by SomfyShade() constructor
    // Position display inversion (owned here; exposed via SomfyShade getters/setters)
    bool flipPosition = false;

    // ── Low-level topic builders ───────────────────────────────────────────────
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);

    // ── Full publish / unpublish ───────────────────────────────────────────────
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);

    // ── State snapshots ───────────────────────────────────────────────────────
    void publishState();
    void publishDisco();
    void unpublishDisco();

    // ── Socket + MQTT event emitters ─────────────────────────────────────────
    void emitState(uint8_t num, const char *evt);
    void emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt);
};
