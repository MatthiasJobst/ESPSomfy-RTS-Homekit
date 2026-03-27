// SomfyShade.h — Individual shade abstraction: SomfyShade extends SomfyRemote with
// position tracking (current/target/my), tilt support, up/down/tilt timing, sun-sensor
// and light flags, GPIO relay outputs, MQTT state publishing, and HomeKit integration.
#pragma once
#include "SomfyRemote.h"

class SomfyShade : public SomfyRemote {
  protected:
    uint8_t shadeId = 255;
    uint64_t moveStart = 0;
    uint64_t tiltStart = 0;
    uint64_t noSunStart = 0;
    uint64_t sunStart = 0;
    uint64_t windStart = 0;
    uint64_t windLast = 0;
    uint64_t noWindStart = 0;
    bool noSunDone = true;
    bool sunDone = true;
    bool windDone = true;
    bool noWindDone = true;
    float startPos = 0.0f;
    float startTiltPos = 0.0f;
    bool settingMyPos = false;
    bool settingPos = false;
    bool settingTiltPos = false;
    uint32_t awaitMy = 0;
  public:
    uint8_t roomId = 0;
    int8_t sortOrder = 0;
    bool flipPosition = false;
    shade_types shadeType = shade_types::roller;
    tilt_types tiltType = tilt_types::none;
    #ifdef USE_NVS
    void load();
    #endif
    float currentPos = 0.0f;
    float currentTiltPos = 0.0f;
    int8_t lastMovement = 0;
    int8_t direction = 0; // 0 = stopped, 1=down, -1=up.
    int8_t tiltDirection = 0; // 0=stopped, 1=clockwise, -1=counter clockwise
    float target = 0.0f;
    float tiltTarget = 0.0f;
    float myPos = -1.0f;
    float myTiltPos = -1.0f;
    SomfyLinkedRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    void toJSONRef(JsonResponse &json);
    int8_t fromJSON(JsonObject &obj);
    void toJSON(JsonResponse &json) override;
    
    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
    uint32_t upTime = 10000;
    uint32_t downTime = 10000;
    uint32_t tiltTime = 7000;
    uint16_t stepSize = 100;
    bool save();
    bool isIdle();
    bool isInGroup();
    void checkMovement();
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void processInternalCommand(somfy_commands cmd, uint8_t repeat = 1);
    void setTiltMovement(int8_t dir);
    void setMovement(int8_t dir);
    void setTarget(float target);
    bool isAtTarget();
    bool isToggle();
    void moveToTarget(float pos, float tilt = -1.0f);
    void moveToTiltTarget(float target);
    void sendTiltCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    void emitState(const char *evt = "shadeState");
    void emitState(uint8_t num, const char *evt = "shadeState");
    void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    void emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt = "shadeCommand");
    void setMyPosition(int8_t pos, int8_t tilt = -1);
    void moveToMyPosition();
    void processWaitingFrame();
    void publish();
    void unpublish();
    static void unpublish(uint8_t id);
    static void unpublish(uint8_t id, const char *topic);
    void publishState();
    void commit();
    void commitShadePosition();
    void commitTiltPosition();
    void commitMyPosition();
    void clear();
    int8_t transformPosition(float fpos);
    void setGPIOs();
    void triggerGPIOs(somfy_frame_t &frame);
    bool usesPin(uint8_t pin);
    // State Setters
    int8_t p_direction(int8_t dir);
    int8_t p_tiltDirection(int8_t dir);
    float p_target(float target);
    float p_tiltTarget(float target);
    float p_myPos(float pos);
    float p_myTiltPos(float pos);
    bool p_flag(somfy_flags_t flag, bool val);
    bool p_sunFlag(bool val);
    bool p_sunny(bool val);
    bool p_windy(bool val);
    float p_currentPos(float pos);
    float p_currentTiltPos(float pos);
    uint16_t p_lastRollingCode(uint16_t code);
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
};

