// Minimal stub — replaces main/MQTT.h for unit tests.
// mqtt_published records every (topic, value) pair written via publish() so
// that tests can assert which topics were written and with what values.
// mqtt_connected_flag controls the return value of connected().
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <ArduinoJson.h>

extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;
extern std::unordered_map<std::string, std::string> mqtt_unpublished;

class MQTTClass {
public:
    bool suspended = false;
    bool connected() { return mqtt_connected_flag; }

    bool publish(const char *topic, const char *val, bool = false) {
        mqtt_published[topic] = val ? val : "";
        return true;
    }
    bool publish(const char *topic, uint8_t val, bool = false) {
        mqtt_published[topic] = std::to_string(val);
        return true;
    }
    bool publish(const char *topic, int8_t val, bool = false) {
        mqtt_published[topic] = std::to_string(val);
        return true;
    }
    bool publish(const char *topic, uint32_t val, bool = false) {
        mqtt_published[topic] = std::to_string(val);
        return true;
    }
    bool publish(const char *topic, uint16_t val, bool = false) {
        mqtt_published[topic] = std::to_string(val);
        return true;
    }
    bool publish(const char *topic, bool val, bool = false) {
        mqtt_published[topic] = val ? "1" : "0";
        return true;
    }
    bool publishBuffer(const char *, uint8_t *, uint16_t, bool = false) { return false; }
    bool publishDisco(const char *topic, JsonObject &, bool = false) {
        mqtt_published[topic] = "disco";
        return true;
    }
    bool unpublish(const char *topic) {
        mqtt_unpublished[topic] = "";
        return true;
    }
    bool subscribe(const char *)   { return false; }
    bool unsubscribe(const char *) { return false; }
};
