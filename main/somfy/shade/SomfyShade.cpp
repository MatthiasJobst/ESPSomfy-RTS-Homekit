// SomfyShade.cpp — SomfyShade method implementations: movement control (open/close/stop/
// my/tilt), position interpolation and transformation, internal-command processing,
// frame emission and relay, JSON and MQTT I/O, NVS load/save, HomeKit bridge callbacks.
#include "SomfyShade.h"

#include <driver/gpio.h>
#include <esp_log.h>

#include "ConfigFile.h"
#include "GitOTA.h"
#include "SomfyShadeController.h"
#include "SomfyTransceiver.h"
#include "compat/preferences.h"

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern GitUpdater git;

void SomfyShade::clear()
{
    this->setShadeId(SomfyShade::NO_ID);
    this->setRemoteAddress(0);
    this->sortOrder = 0;
    this->roomId = SomfyRoom::NO_ID;
    movementTracker.reset();
    this->flagManager = SomfyFlagManager{};
    mqttPublisher.flipPosition = false;
    this->flipCommands = false;
    this->lastRollingCode = 0;
    this->shadeType = shade_types::roller;
    this->tiltType = tilt_types::none;
    this->currentPos = 0.0f;
    this->currentTiltPos = 0.0f;
    this->direction = 0;
    this->tiltDirection = 0;
    this->target = 0.0f;
    this->tiltTarget = 0.0f;
    targetSequencer.myPos = -1.0f;
    targetSequencer.myTiltPos = -1.0f;
    this->bitLength = somfy.transceiver.config.type;
    this->proto = somfy.transceiver.config.proto;
    linkedRemotes.clear();
    this->paired = false;
    this->name[0] = 0x00;
    commandProcessor.upTime = 10000;
    commandProcessor.downTime = 10000;
    commandProcessor.tiltTime = 7000;
    commandProcessor.stepSize = 100;
    this->repeats = 1;
}

bool SomfyShade::linkRemote(uint32_t address, uint16_t rollingCode)
{
    bool ok = linkedRemotes.linkRemote(address, rollingCode);
    if (ok) markShadeDataDirty();
    return ok;
}

void SomfyShade::markShadeDataDirty()
{
    somfy.store.markDirty();
}

void SomfyShade::commitShadePosition()
{
    markShadeDataDirty();
}

void SomfyShade::commitMyPosition()
{
    markShadeDataDirty();
}

void SomfyShade::commitTiltPosition()
{
    markShadeDataDirty();
}

bool SomfyShade::unlinkRemote(uint32_t address)
{
    bool ok = linkedRemotes.unlinkRemote(address);
    if (ok) markShadeDataDirty();
    return ok;
}

bool SomfyShade::isAtTarget()
{
    constexpr float epsilon = 0.00001f;
    if (this->tiltType == tilt_types::tiltonly)
        return fabsf(this->currentTiltPos - this->tiltTarget) < epsilon;
    else if (this->tiltType == tilt_types::none)
        return fabsf(this->currentPos - this->target) < epsilon;
    return fabsf(this->currentPos - this->target) < epsilon && fabsf(this->currentTiltPos - this->tiltTarget) < epsilon;
}

bool SomfyShade::isInGroup()
{
    if (this->getShadeId() == 255) return false;
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup &group = somfy.groupController.groupSlot(i);
        if (group.getGroupId() != 255 && group.hasShadeId(this->getShadeId())) return true;
    }
    return false;
}

void SomfyShade::setGPIOs()
{
    this->gpioControl.setGPIOs(this->proto, this->currentPos, this->direction, this->tiltDirection, this->shadeType,
                               this->tiltType);
}

void SomfyShade::triggerGPIOs(somfy_frame_t &frame)
{
    this->gpioControl.triggerGPIOs(frame, this->proto, this->shadeType, this->isToggle());
}

void SomfyShade::checkMovement()
{
    movementTracker.checkMovement();
}

void SomfyShade::publishState()
{
    mqttPublisher.publishState();
}

void SomfyShade::publishDisco()
{
    mqttPublisher.publishDisco();
}

void SomfyShade::unpublishDisco()
{
    mqttPublisher.unpublishDisco();
}

void SomfyShade::publish()
{
    mqttPublisher.publish();
}

void SomfyShade::unpublish()
{
    mqttPublisher.unpublish();
}

void SomfyShade::unpublish(uint8_t id)
{
    SomfyMQTTPublisher::unpublish(id);
}

void SomfyShade::unpublish(uint8_t id, const char *topic)
{
    SomfyMQTTPublisher::unpublish(id, topic);
}

bool SomfyShade::publish(const char *topic, int8_t val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

bool SomfyShade::publish(const char *topic, const char *val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

bool SomfyShade::publish(const char *topic, uint8_t val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

bool SomfyShade::publish(const char *topic, uint32_t val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

bool SomfyShade::publish(const char *topic, uint16_t val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

bool SomfyShade::publish(const char *topic, bool val, bool retain)
{
    return mqttPublisher.publish(topic, val, retain);
}

float SomfyShade::p_currentPos(float pos)
{
    float old = this->currentPos;
    this->currentPos = pos;
    if (floorf(old) != floorf(pos))
        this->publish("position", this->transformPosition(static_cast<uint8_t>(floorf(this->currentPos))));
    return old;
}

float SomfyShade::p_currentTiltPos(float pos)
{
    float old = this->currentTiltPos;
    this->currentTiltPos = pos;
    if (floorf(old) != floorf(pos))
        this->publish("tiltPosition", this->transformPosition(static_cast<uint8_t>(floorf(this->currentTiltPos))));
    return old;
}

uint16_t SomfyShade::p_lastRollingCode(uint16_t code)
{
    uint16_t old = SomfyRemote::p_lastRollingCode(code);
    if (old != code) this->publish("lastRollingCode", code);
    return old;
}

bool SomfyShade::p_demoMode(bool val)
{
    bool old = this->flags.setDemoFlagReturnOld(val);
    if (old != val) this->publish("demoMode", static_cast<uint8_t>(val));
    return old;
}

bool SomfyShade::p_sunFlag(bool val)
{
    bool old = this->flags.setSunFlagReturnOld(val);
    if (old != val) this->publish("sunFlag", static_cast<uint8_t>(val));
    return old;
}

bool SomfyShade::p_windy(bool val)
{
    bool old = this->flags.setWindyReturnOld(val);
    if (old != val) this->publish("windy", static_cast<uint8_t>(val));
    return old;
}

bool SomfyShade::p_sunny(bool val)
{
    bool old = this->flags.setSunnyReturnOld(val);
    if (old != val) this->publish("sunny", static_cast<uint8_t>(val));
    return old;
}

int8_t SomfyShade::p_direction(int8_t dir)
{
    int8_t old = this->direction;
    if (old != dir) {
        this->direction = dir;
        this->publish("direction", this->direction, true);
    }
    return old;
}

int8_t SomfyShade::p_tiltDirection(int8_t dir)
{
    int8_t old = this->tiltDirection;
    if (old != dir) {
        this->tiltDirection = dir;
        this->publish("tiltDirection", this->tiltDirection, true);
    }
    return old;
}

float SomfyShade::p_target(float target)
{
    float old = this->target;
    if (old != target) {
        this->target = target;
        if (this->transformPosition(old) != this->transformPosition(target))
            this->publish("target", this->transformPosition(this->target), true);
    }
    return old;
}

float SomfyShade::p_tiltTarget(float target)
{
    float old = this->tiltTarget;
    if (old != target) {
        this->tiltTarget = target;
        if (this->transformPosition(old) != this->transformPosition(target))
            this->publish("tiltTarget", this->transformPosition(this->tiltTarget), true);
    }
    return old;
}

float SomfyShade::p_myPos(float pos)
{
    float old = targetSequencer.myPos;
    if (old != pos) {
        targetSequencer.myPos = pos;
        if (this->transformPosition(old) != this->transformPosition(pos))
            this->publish("mypos", this->transformPosition(targetSequencer.myPos), true);
    }
    return old;
}

float SomfyShade::p_myTiltPos(float pos)
{
    float old = targetSequencer.myTiltPos;
    if (old != pos) {
        targetSequencer.myTiltPos = pos;
        if (this->transformPosition(old) != this->transformPosition(pos))
            this->publish("myTiltPos", this->transformPosition(targetSequencer.myTiltPos), true);
    }
    return old;
}

void SomfyShade::emitState(const char *evt)
{
    this->emitState(255, evt);
}

void SomfyShade::emitState(uint8_t num, const char *evt)
{
    mqttPublisher.emitState(num, evt);
}

void SomfyShade::emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt)
{
    this->emitCommand(255, cmd, source, sourceAddress, evt);
}

void SomfyShade::emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress,
                             const char *evt)
{
    mqttPublisher.emitCommand(num, cmd, source, sourceAddress, evt);
}

int8_t SomfyShade::transformPosition(float fpos)
{
    if (fpos < 0) return -1;
    return static_cast<int8_t>(mqttPublisher.flipPosition && fpos >= 0.00f ? floorf(100.0f - fpos) : floorf(fpos));
}

bool SomfyShade::isIdle()
{
    return this->isAtTarget() && this->direction == 0 && this->tiltDirection == 0;
}

void SomfyShade::processWaitingFrame()
{
    commandProcessor.processWaitingFrame();
}

void SomfyShade::processSensorCommand(somfy_frame_t &frame, uint64_t curTime)
{
    commandProcessor.processSensorCommand(frame, curTime);
}

void SomfyShade::processFlagCommand(bool internal, somfy_frame_t &frame)
{
    commandProcessor.processFlagCommand(internal, frame);
}

void SomfyShade::processSunFlagCommand(bool internal, somfy_frame_t &frame)
{
    commandProcessor.processSunFlagCommand(internal, frame);
}

void SomfyShade::processMyCommand(bool internal, somfy_frame_t &frame, uint64_t curTime)
{
    commandProcessor.processMyCommand(internal, frame, curTime);
}

void SomfyShade::processUpDownCommand(somfy_commands cmd, int8_t moveDir, bool internal, somfy_frame_t &frame,
                                      uint64_t curTime)
{
    commandProcessor.processUpDownCommand(cmd, moveDir, internal, frame, curTime);
}

void SomfyShade::processStepCommand(somfy_commands cmd, int8_t stepDir, bool internal, somfy_frame_t &frame)
{
    commandProcessor.processStepCommand(cmd, stepDir, internal, frame);
}

void SomfyShade::processFrame(somfy_frame_t &frame, bool internal)
{
    commandProcessor.processFrame(frame, internal);
}

void SomfyShade::processInternalCommand(somfy_commands cmd, uint8_t repeat)
{
    commandProcessor.processInternalCommand(cmd, repeat);
}

void SomfyShade::setTiltMovement(int8_t dir)
{
    movementTracker.setTiltMovement(dir);
}

void SomfyShade::setMovement(int8_t dir)
{
    movementTracker.setMovement(dir);
}

void SomfyShade::setMyPosition(int8_t pos, int8_t tilt)
{
    targetSequencer.setMyPosition(pos, tilt);
}

void SomfyShade::moveToMyPosition()
{
    targetSequencer.moveToMyPosition();
}

float SomfyShade::getMyPos() const
{
    return targetSequencer.myPos;
}

float SomfyShade::getMyTiltPos() const
{
    return targetSequencer.myTiltPos;
}

uint32_t SomfyShade::getUpTime() const
{
    return commandProcessor.upTime;
}

uint32_t SomfyShade::getDownTime() const
{
    return commandProcessor.downTime;
}

uint32_t SomfyShade::getTiltTime() const
{
    return commandProcessor.tiltTime;
}

uint16_t SomfyShade::getStepSize() const
{
    return commandProcessor.stepSize;
}

void SomfyShade::setUpTime(uint32_t v)
{
    commandProcessor.upTime = v;
}

void SomfyShade::setDownTime(uint32_t v)
{
    commandProcessor.downTime = v;
}

void SomfyShade::setTiltTime(uint32_t v)
{
    commandProcessor.tiltTime = v;
}

void SomfyShade::setStepSize(uint16_t v)
{
    commandProcessor.stepSize = v;
}

SomfyRemote &SomfyShade::getLinkedRemote(uint8_t i)
{
    return linkedRemotes.get(i);
}

bool SomfyShade::getFlipPosition() const
{
    return mqttPublisher.flipPosition;
}

void SomfyShade::setFlipPosition(bool v)
{
    mqttPublisher.flipPosition = v;
}

void SomfyShade::sendCommand(somfy_commands cmd)
{
    commandTransmitter.sendCommand(cmd);
}

void SomfyShade::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize)
{
    commandTransmitter.sendCommand(cmd, repeat, stepSize);
}

void SomfyShade::sendOrRepeat(somfy_commands cmd, int16_t repeat, uint8_t stepSize)
{
    // A garage door has a single toggle command; a "Prog" press acts as Toggle.
    if (this->shadeType == shade_types::garage1 && cmd == somfy_commands::Prog) cmd = somfy_commands::Toggle;
    SomfyRemote::sendOrRepeat(cmd, repeat, stepSize);
}

void SomfyShade::sendTiltCommand(somfy_commands cmd)
{
    commandTransmitter.sendTiltCommand(cmd);
}

void SomfyShade::moveToTiltTarget(float target)
{
    targetSequencer.moveToTiltTarget(target);
}

void SomfyShade::moveToTarget(float pos, float tilt)
{
    targetSequencer.moveToTarget(pos, tilt);
}

void SomfyShade::moveToTargetForced(float pos, uint8_t repeats, float tilt)
{
    targetSequencer.moveToTarget(pos, tilt, repeats);
}

bool SomfyShade::save()
{
    somfy.store.commit();
    publish();
    return true;
}

bool SomfyShade::isToggle()
{
    switch (this->shadeType) {
    case shade_types::garage1:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
        return true;
    default:
        break;
    }
    return false;
}

bool SomfyShade::usesPin(uint8_t pin)
{
    return this->gpioControl.usesPin(pin, this->proto, this->shadeType, this->isToggle());
}

int8_t SomfyShade::validateJSON(JsonObject &obj)
{
    return jsonSerializer.validateJSON(obj);
}

int8_t SomfyShade::fromJSON(JsonObject &obj)
{
    return jsonSerializer.fromJSON(obj);
}

void SomfyShade::toJSONRef(JsonResponse &json)
{
    jsonSerializer.toJSONRef(json);
}

void SomfyShade::toJSON(JsonResponse &json)
{
    jsonSerializer.toJSON(json);
}
