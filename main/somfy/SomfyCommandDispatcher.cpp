#include "SomfyCommandDispatcher.h"
#include <esp_log.h>
#include "SomfyShade.h"
#include "SomfyGroup.h"
#include "SomfyTransceiver.h"

static const char *s_TAG = "SomfyCommandDispatcher";

SomfyCommandDispatcher::SomfyCommandDispatcher(SomfyTransceiver &transceiver) : transceiver(transceiver)
{
}

bool SomfyCommandDispatcher::enqueueShadeCommand(SomfyShade *shade, somfy_commands cmd, uint8_t repeat,
                                                 uint8_t stepSize)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeCommand;
    c.remote = shade;
    c.cmd = cmd;
    c.repeat = repeat;
    c.stepSize = stepSize;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueShadeTarget(SomfyShade *shade, float target)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeTarget;
    c.remote = shade;
    c.target = target;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueShadeTargetForced(SomfyShade *shade, float target)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeTargetForced;
    c.remote = shade;
    c.target = target;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueShadeTiltTarget(SomfyShade *shade, float target)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeTiltTarget;
    c.remote = shade;
    c.target = target;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueShadeTiltCommand(SomfyShade *shade, somfy_commands cmd)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeTiltCommand;
    c.remote = shade;
    c.cmd = cmd;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueShadeSensor(SomfyShade *shade, int8_t isWindy, int8_t isSunny, uint8_t repeat)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::ShadeSensor;
    c.remote = shade;
    c.isWindy = isWindy;
    c.isSunny = isSunny;
    c.repeat = repeat;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueGroupCommand(SomfyGroup *group, somfy_commands cmd, uint8_t repeat)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::GroupCommand;
    c.remote = group;
    c.cmd = cmd;
    c.repeat = repeat;
    return this->cmdQueue.push(c);
}

bool SomfyCommandDispatcher::enqueueGroupSensor(SomfyGroup *group, int8_t isWindy, int8_t isSunny, uint8_t repeat)
{
    queued_cmd_t c;
    c.type = cmd_queue_type_t::GroupSensor;
    c.remote = group;
    c.isWindy = isWindy;
    c.isSunny = isSunny;
    c.repeat = repeat;
    return this->cmdQueue.push(c);
}

void SomfyCommandDispatcher::drainCommandQueue()
{
    if (this->cmdQueue.empty() || !this->cmdQueue.ready()) return;
    if (this->transceiver.txBusy()) return; // Don't block app_main — loop() will drain on the next tick
    this->cmdQueue.lastDrain = millis();
    queued_cmd_t c;
    if (!this->cmdQueue.pop(c)) return;
    if (!c.remote) return;
    // c.type tells us the concrete subclass behind c.remote; downcast per case.
    switch (c.type) {
    case cmd_queue_type_t::ShadeCommand:
        static_cast<SomfyShade *>(c.remote)->sendCommand(c.cmd, c.repeat, c.stepSize);
        break;
    case cmd_queue_type_t::ShadeTarget:
        static_cast<SomfyShade *>(c.remote)->moveToTarget(c.target);
        break;
    case cmd_queue_type_t::ShadeTargetForced:
        static_cast<SomfyShade *>(c.remote)->moveToTargetForced(c.target);
        break;
    case cmd_queue_type_t::ShadeTiltTarget:
        static_cast<SomfyShade *>(c.remote)->moveToTiltTarget(c.target);
        break;
    case cmd_queue_type_t::ShadeTiltCommand:
        static_cast<SomfyShade *>(c.remote)->sendTiltCommand(c.cmd);
        break;
    case cmd_queue_type_t::ShadeSensor:
        static_cast<SomfyShade *>(c.remote)->sendSensorCommand(c.isWindy, c.isSunny, c.repeat);
        break;
    case cmd_queue_type_t::GroupCommand:
        static_cast<SomfyGroup *>(c.remote)->sendCommand(c.cmd, c.repeat);
        break;
    case cmd_queue_type_t::GroupSensor:
        static_cast<SomfyGroup *>(c.remote)->sendSensorCommand(c.isWindy, c.isSunny, c.repeat);
        break;
    }
}

void SomfyCommandDispatcher::sendFrame(somfy_frame_t &frame, uint8_t repeat)
{
    // drainCommandQueue() guards with txBusy() before calling here, so this
    // should never be true. If it is, skip rather than blocking app_main and
    // triggering the task watchdog.
    if (this->transceiver.txBusy()) {
        ESP_LOGW(s_TAG, "sendFrame: TX busy — skipping frame to avoid blocking app_main");
        return;
    }
    this->transceiver.beginTransmit();
    this->transceiver.beginFrameTx(frame, repeat);
    // endTransmit() is deferred: Transceiver::loop() calls it when txBusy() clears.
}
