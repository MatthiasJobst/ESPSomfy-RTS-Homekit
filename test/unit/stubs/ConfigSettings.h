// Minimal stub — replaces main/ConfigSettings.h for unit tests.
#pragma once
#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>
#include "ETH.h"
#include "Arduino.h"  // provides byte, String

// Arduino defines 'byte' as uint8_t — Arduino.h above covers this, but
// keep explicit alias in case include order varies.
using byte = uint8_t;

// Forward-declare JsonResponse / JsonSockEvent so other headers compile.
class JsonResponse;
class JsonSockEvent;

#define FW_VERSION "v0.0.0-test"

enum class conn_types_t : uint8_t { unset=0, wifi=1, ethernet=2, ethernetpref=3, ap=4 };
enum DeviceStatus { DS_OK=0, DS_ERROR=1, DS_FWUPDATE=2 };

// Mirror of main/ConfigSettings.h security model (only the fields AuthService reads).
enum class security_types : byte { None = 0x00, PinEntry = 0x01, Password = 0x02 };

struct restore_options_t {
    bool settings=false, shades=false, network=false, transceiver=false, repeaters=false, mqtt=false;
    void fromJSON(JsonObject &) {}
};
struct appver_t {
    char name[15]=""; uint8_t major=0,minor=0,build=0; char suffix[4]="";
    void parse(const char *) {}
    void toJSON(JsonResponse &) {}
    void toJSON(JsonSockEvent *) {}
    int8_t compare(appver_t &) { return 0; }
    void copy(appver_t &o) { memcpy(this, &o, sizeof(*this)); }
};

class BaseSettings {
public:
    bool loadFile(const char *) { return false; }
    bool fromJSON(JsonObject &) { return false; }
    bool toJSON(JsonObject &) { return false; }
    bool saveFile(const char *) { return false; }
    bool save() { return false; }
    bool load() { return false; }
};

// Minimal ConfigSettings — enough for extern ConfigSettings settings to compile/link.
class MQTTSettings : public BaseSettings {
public:
    char rootTopic[64]  = "SomfyController";
    char discoTopic[64] = "homeassistant";
    char server[64]     = "";
    char user[32]       = "";
    char password[32]   = "";
    uint16_t port       = 1883;
    bool enabled        = false;
    bool pubDisco       = false;
};
// Security model fields read by AuthService.
class SecuritySettings : public BaseSettings {
public:
    security_types type = security_types::None;
    char username[33] = "";
    char password[33] = "";
    char pin[5]       = "";
    uint8_t permissions = 0;
};
class NTPSettings    : public BaseSettings {};
class NetworkSettings: public BaseSettings {};
class EthernetSettings : public BaseSettings {
public:
    bool usesPin(uint8_t) const { return false; }
};

class ConfigSettings : public BaseSettings {
public:
    char hostname[32]        = "somfy-test";
    char serverId[32]        = "test-server-id";
    char apPassword[32]      = "";
    conn_types_t connType    = conn_types_t::wifi;
    appver_t fwVersion;
    MQTTSettings     MQTT;
    NTPSettings      ntp;
    NetworkSettings  network;
    EthernetSettings Ethernet;
    SecuritySettings Security;

    const char *getTopicPrefix() const { return MQTT.rootTopic; }
    const char *getHostname()    const { return hostname; }
};
