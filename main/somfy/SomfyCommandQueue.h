// SomfyCommandQueue.h — Fixed-size ring-buffer for outbound shade/group commands.
// Commands are pushed by HTTP and MQTT handlers and drained one-per-tick in
// SomfyShadeController::loop() to prevent RF transmission collisions.
#pragma once
#include "SomfyFrame.h"

#define CMD_QUEUE_SIZE 16
#define CMD_QUEUE_DRAIN_MS 50   // minimum ms between dequeued commands

enum class cmd_queue_type_t : uint8_t {
    ShadeCommand,
    ShadeTarget,
    ShadeTargetForced,
    ShadeTiltTarget,
    ShadeTiltCommand,
    ShadeSensor,
    GroupCommand,
    GroupSensor,
};

struct queued_cmd_t {
    cmd_queue_type_t type    = cmd_queue_type_t::ShadeCommand;
    uint8_t          entityId = 255;   // shadeId or groupId
    somfy_commands   cmd      = somfy_commands::My;
    uint8_t          repeat   = 1;
    uint8_t          stepSize = 0;
    float            target   = 0.0f;
    int8_t           isWindy  = -1;
    int8_t           isSunny  = -1;
};

struct SomfyCommandQueue {
    queued_cmd_t entries[CMD_QUEUE_SIZE] = {};
    uint8_t      head    = 0;
    uint8_t      tail    = 0;
    uint8_t      count   = 0;
    uint32_t     lastDrain = 0;

    bool push(const queued_cmd_t &cmd) {
        if(count >= CMD_QUEUE_SIZE) return false;   // full — drop oldest? No — drop newest for safety.
        entries[tail] = cmd;
        tail = (tail + 1) % CMD_QUEUE_SIZE;
        count++;
        return true;
    }

    bool pop(queued_cmd_t &out) {
        if(count == 0) return false;
        out = entries[head];
        head = (head + 1) % CMD_QUEUE_SIZE;
        count--;
        return true;
    }

    bool empty() const { return count == 0; }
    bool ready()  const { return millis() - lastDrain >= CMD_QUEUE_DRAIN_MS; }
};
