// SomfyCommandDispatcher.h — Owns the command queue and the RF send path: builds
// queued commands for shades/groups, drains the queue on each loop tick
// (dispatching to the target objects), and transmits raw frames via the transceiver.
#pragma once

#include "SomfyCommandQueue.h"
#include "SomfyFrame.h" // somfy_frame_t, somfy_commands

class SomfyShade;
class SomfyGroup;
class SomfyTransceiver;

/**
 * @brief Owns the command queue and dispatches queued shade/group commands.
 */
class SomfyCommandDispatcher {
  public:
    /**
     * @brief Construct over the owner's transceiver.
     * @param transceiver Used to guard against a busy TX and to send frames.
     */
    explicit SomfyCommandDispatcher(SomfyTransceiver &transceiver);

    SomfyCommandQueue cmdQueue; /**< Pending commands; drained on each loop tick. */

    /**
     * @brief Queue a command (Up/Down/My/…) for a shade.
     * @return False if the queue is full.
     */
    bool enqueueShadeCommand(SomfyShade *shade, somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);

    /** @brief Queue a move-to-position for a shade. @return False if the queue is full. */
    bool enqueueShadeTarget(SomfyShade *shade, float target);

    /** @brief Queue a forced move-to-position for a shade. @return False if the queue is full. */
    bool enqueueShadeTargetForced(SomfyShade *shade, float target);

    /** @brief Queue a move-to-tilt for a shade. @return False if the queue is full. */
    bool enqueueShadeTiltTarget(SomfyShade *shade, float target);

    /** @brief Queue a tilt command for a shade. @return False if the queue is full. */
    bool enqueueShadeTiltCommand(SomfyShade *shade, somfy_commands cmd);

    /** @brief Queue a sun/wind sensor command for a shade. @return False if the queue is full. */
    bool enqueueShadeSensor(SomfyShade *shade, int8_t isWindy, int8_t isSunny, uint8_t repeat);

    /** @brief Queue a command for a group. @return False if the queue is full. */
    bool enqueueGroupCommand(SomfyGroup *group, somfy_commands cmd, uint8_t repeat);

    /** @brief Queue a sun/wind sensor command for a group. @return False if the queue is full. */
    bool enqueueGroupSensor(SomfyGroup *group, int8_t isWindy, int8_t isSunny, uint8_t repeat);

    /**
     * @brief Pop and dispatch one ready command if the transceiver is free.
     * @note Called from the controller loop; returns immediately if TX is busy.
     */
    void drainCommandQueue();

    /**
     * @brief Transmit a frame immediately (skips if TX is busy, to avoid blocking app_main).
     * @param frame Frame to send.
     * @param repeat Number of extra repeats.
     */
    void sendFrame(somfy_frame_t &frame, uint8_t repeat = 0);

  private:
    SomfyTransceiver &transceiver; /**< Owner's transceiver (not owned). */
};
