// Minimal stub — replaces main/MQTT.h for unit tests.
#pragma once
#include <cstdint>
#include <ArduinoJson.h>

class MQTTClass {
public:
    bool suspended   = false;
    bool connected() { return false; }
    bool publish(const char *, const char *, bool = false)  { return false; }
    bool publish(const char *, uint8_t,      bool = false)  { return false; }
    bool publish(const char *, int8_t,       bool = false)  { return false; }
    bool publish(const char *, uint32_t,     bool = false)  { return false; }
    bool publish(const char *, uint16_t,     bool = false)  { return false; }
    bool publish(const char *, bool,         bool = false)  { return false; }
    bool publishBuffer(const char *, uint8_t *, uint16_t, bool = false) { return false; }
    bool publishDisco(const char *, JsonObject &, bool = false) { return false; }
    bool unpublish(const char *) { return false; }
    bool subscribe(const char *) { return false; }
    bool unsubscribe(const char *) { return false; }
};
