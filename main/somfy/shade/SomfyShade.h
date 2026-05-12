// SomfyShade.h — Individual shade abstraction: SomfyShade extends SomfyRemote with
// position tracking (current/target/my), tilt support, up/down/tilt timing, sun-sensor
// and light flags, GPIO relay outputs, MQTT state publishing, and HomeKit integration.
#pragma once
#include "SomfyRemote.h"
#include "SomfyFlagManager.h"
#include "SomfyGPIOControl.h"
#include "SomfyMQTTPublisher.h"
#include "SomfyCommandTransmitter.h"
#include "SomfyJSONSerializer.h"
#include "SomfyMotionState.h"
#include "SomfyTargetSequencer.h"
#include "SomfyCommandProcessor.h"
#include "SomfyMovementTracker.h"

class SomfyShade : public SomfyRemote {
  protected:
    uint32_t awaitMy = 0;

  public:
    uint8_t shadeId = 255;
    SomfyShade()
    {
        mqttPublisher.shade = this;
        commandTransmitter.shade = this;
        jsonSerializer.shade = this;
        targetSequencer.shade = this;
        commandProcessor.shade = this;
        movementTracker.shade = this;
    }
    SomfyFlagManager flagManager;
    SomfyGPIOControl gpioControl;
    SomfyMQTTPublisher mqttPublisher;
    SomfyCommandTransmitter commandTransmitter;
    SomfyJSONSerializer jsonSerializer;
    SomfyTargetSequencer targetSequencer;
    SomfyCommandProcessor commandProcessor;
    SomfyMovementTracker movementTracker;
    uint8_t roomId = 0;
    uint8_t sortOrder = 0;
    shade_types shadeType = shade_types::roller;
    tilt_types tiltType = tilt_types::none;
    float currentPos = 0.0f;
    float currentTiltPos = 0.0f;
    int8_t direction = 0;     // 0 = stopped, 1=down, -1=up.
    int8_t tiltDirection = 0; // 0=stopped, 1=clockwise, -1=counter clockwise
    float target = 0.0f;
    float tiltTarget = 0.0f;
    bool paired = false;
    int8_t validateJSON(JsonObject &obj);
    void toJSONRef(JsonResponse &json);
    int8_t fromJSON(JsonObject &obj);
    void toJSON(JsonResponse &json) override;

    char name[21] = "";
    void setShadeId(uint8_t id) { shadeId = id; }
    uint8_t getShadeId() { return shadeId; }
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
    void moveToTargetForced(float pos, float tilt = -1.0f);
    void moveToTiltTarget(float target);
    void sendTiltCommand(somfy_commands cmd);
    void sendCommand(somfy_commands cmd) override;
    void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0) override;
    bool linkRemote(uint32_t remoteAddress, uint16_t rollingCode = 0);
    bool unlinkRemote(uint32_t remoteAddress);
    virtual void emitState(const char *evt = "shadeState");
    virtual void emitState(uint8_t num, const char *evt = "shadeState");
    virtual void emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress,
                             const char *evt = "shadeCommand");
    virtual void emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress,
                             const char *evt = "shadeCommand");
    void setMyPosition(int8_t pos, int8_t tilt = -1);
    void moveToMyPosition();
    // Favorite-position getters (owned by SomfyTargetSequencer)
    float getMyPos() const;
    float getMyTiltPos() const;
    // Motion-state flag setters (owned by SomfyMovementTracker)
    void setSettingPos(bool v);
    void setSettingTiltPos(bool v);
    void setSettingMyPos(bool v);
    void setBoostedStop(bool v);
    bool getBoostedStop() const;
    void clearMotionState();
    // Movement interpolation reset — called at the start of every command frame
    void resetMovement(uint64_t t);
    // Timing and step config (owned by SomfyCommandProcessor)
    uint32_t getUpTime() const;
    uint32_t getDownTime() const;
    uint32_t getTiltTime() const;
    uint16_t getStepSize() const;
    void setUpTime(uint32_t v);
    void setDownTime(uint32_t v);
    void setTiltTime(uint32_t v);
    void setStepSize(uint16_t v);
    // Last movement direction (owned by SomfyMovementTracker)
    int8_t getLastMovement() const;
    void setLastMovement(int8_t v);
    // Linked remotes (owned by SomfyCommandTransmitter)
    SomfyLinkedRemote &getLinkedRemote(uint8_t i);
    // Position display inversion (owned by SomfyMQTTPublisher)
    bool getFlipPosition() const;
    void setFlipPosition(bool v);
    void processWaitingFrame();
    void processStepCommand(somfy_commands cmd, int8_t stepDir, bool internal, somfy_frame_t &frame);
    void processSensorCommand(somfy_frame_t &frame, uint64_t curTime);
    void processFlagCommand(bool internal, somfy_frame_t &frame);
    void processSunFlagCommand(bool internal, somfy_frame_t &frame);
    void processMyCommand(bool internal, somfy_frame_t &frame, uint64_t curTime);
    void processUpDownCommand(somfy_commands cmd, int8_t moveDir, bool internal, somfy_frame_t &frame,
                              uint64_t curTime);
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
    void triggerGPIOs(somfy_frame_t &frame) override;
    bool usesPin(uint8_t pin);
    // State Setters
    int8_t p_direction(int8_t dir);
    int8_t p_tiltDirection(int8_t dir);
    float p_target(float target);
    float p_tiltTarget(float target);
    float p_myPos(float pos);
    float p_myTiltPos(float pos);
    bool p_demoMode(bool val);
    bool p_sunFlag(bool val);
    bool p_sunny(bool val);
    bool p_windy(bool val);
    float p_currentPos(float pos);
    float p_currentTiltPos(float pos);
    uint16_t p_lastRollingCode(uint16_t code) override;
    bool publish(const char *topic, const char *val, bool retain = false);
    bool publish(const char *topic, uint8_t val, bool retain = false);
    bool publish(const char *topic, int8_t val, bool retain = false);
    bool publish(const char *topic, uint32_t val, bool retain = false);
    bool publish(const char *topic, uint16_t val, bool retain = false);
    bool publish(const char *topic, bool val, bool retain = false);
    void publishDisco();
    void unpublishDisco();
};
