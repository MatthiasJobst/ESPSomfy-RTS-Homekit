#pragma once

#include "SomfyFrame.h"

class SomfyRemote;

#define CMD_QUEUE_SIZE 16
#define CMD_QUEUE_DRAIN_MS 50

/**
 * @brief Command type discriminator for entries in the command queue.
 */
enum class cmd_queue_type_t : uint8_t {
    ShadeCommand,      /**< Direct command to a shade. */
    ShadeTarget,       /**< Move shade to a target position. */
    ShadeTargetForced, /**< Move shade to target, bypassing debounce. */
    ShadeTiltTarget,   /**< Move shade tilt to a target angle. */
    ShadeTiltCommand,  /**< Direct tilt command to a shade. */
    ShadeSensor,       /**< Sensor state update for a shade. */
    GroupCommand,      /**< Direct command to a group. */
    GroupSensor,       /**< Sensor state update for a group. */
};

/**
 * @brief A single entry in the outbound command ring buffer.
 */
struct queued_cmd_t {
    cmd_queue_type_t type = cmd_queue_type_t::ShadeCommand; /**< Discriminates the target's concrete type and payload. */
    SomfyRemote *remote = nullptr;                          /**< Target shade or group; downcast per `type`. */
    somfy_commands cmd = somfy_commands::My;                /**< Command to send. */
    uint8_t repeat = 1;                                     /**< Transmission repeat count. */
    uint8_t stepSize = 0;                                   /**< Step size for stepped moves. */
    float target = 0.0f;                                    /**< Target position 0–100 %. */
    int8_t isWindy = -1;                                    /**< Wind state; -1 = unchanged. */
    int8_t isSunny = -1;                                    /**< Sun state; -1 = unchanged. */
};

/**
 * @brief Fixed-size ring buffer for outbound shade and group commands.
 *
 * Commands are pushed by HTTP and MQTT handlers and drained one-per-tick
 * in SomfyShadeController::loop() to prevent RF transmission collisions.
 */
struct SomfyCommandQueue {
    queued_cmd_t entries[CMD_QUEUE_SIZE] = {}; /**< Storage array. */
    uint8_t head = 0;                          /**< Read index. */
    uint8_t tail = 0;                          /**< Write index. */
    uint8_t count = 0;                         /**< Number of occupied slots. */
    uint32_t lastDrain = 0;                    /**< Timestamp of last successful pop, in ms. */

    /**
     * @brief Push a command onto the queue.
     *
     * @param cmd Command to enqueue.
     * @return True on success, false if the queue is full.
     */
    bool push(const queued_cmd_t &cmd)
    {
        if (count >= CMD_QUEUE_SIZE) return false; // drop newest rather than oldest to avoid stale commands
        entries[tail] = cmd;
        tail = (tail + 1) % CMD_QUEUE_SIZE;
        count++;
        return true;
    }

    /**
     * @brief Pop the oldest command from the queue.
     *
     * @param out Receives the dequeued command.
     * @return True if a command was returned, false if the queue was empty.
     */
    bool pop(queued_cmd_t &out)
    {
        if (count == 0) return false;
        out = entries[head];
        head = (head + 1) % CMD_QUEUE_SIZE;
        count--;
        return true;
    }

    /**
     * @brief Discard all queued commands.
     *
     * Called when a shade or group is added or removed so no entry can hold a
     * pointer to a slot whose target has since changed.
     */
    void reset()
    {
        head = tail = count = 0;
    }

    /** @brief Return true if the queue has no entries. */
    bool empty() const { return count == 0; }
    /** @brief Return true if enough time has elapsed since the last drain. */
    bool ready() const { return millis() - lastDrain >= CMD_QUEUE_DRAIN_MS; }
};
