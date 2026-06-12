// SomfyShadeController.h — Top-level aggregate model: owns the shades plus the
// room/group/repeater controllers, the transceiver and the command dispatcher.
// Owns config persistence (LittleFS/NVS), frame dispatch and state broadcast.
// Driven by SomfyStateMachine (boot/poll).  SOMFY_MAX_* limits come from SomfyFrame.h.
#pragma once
#include <ArduinoJson.h>
#include "SomfyFrame.h"
#include "SomfyTransceiver.h"
#include "SomfyRoom.h"
#include "SomfyGroup.h"
#include "SomfyShade.h"
#include "SomfyRoomController.h"
#include "SomfyGroupController.h"
#include "SomfyRepeaterController.h"
#include "SomfyCommandDispatcher.h"
#include "SomfyCommandQueue.h"

/**
 * @brief In-memory model of the whole Somfy system: shade array plus the
 *        room/group/repeater controllers, transceiver and command dispatcher.
 *
 * Holds the canonical config that ShadeConfigFile serialises, dispatches inbound
 * RF frames to shades, and broadcasts state to MQTT/WebSocket/HomeKit. Periodic
 * work (movement, auto-commit) is sequenced externally by SomfyStateMachine.
 */
class SomfyShadeController {
  protected:
    /**
     * @brief roomController callback: clears shade/group references to a deleted room.
     * @param roomId Id of the room that was removed.
     */
    void onRoomRemoved(uint8_t roomId);

  public:
    // ── Fields ──────────────────────────────────────────────────────────────
    SomfyCommandDispatcher commandDispatcher;   /**< Command queue + RF send path. */
    SomfyGroupController groupController;       /**< Owns the group collection. */
    bool isDirty = false;                       /**< In-memory config differs from disk. */
    uint32_t lastCommit = 0;                    /**< millis() of last commit (auto-commit throttle). */
    SomfyRepeaterController repeaterController; /**< Owns the repeater address list. */
    SomfyRoomController roomController;         /**< Owns the room collection. */
    SomfyShade shades[SOMFY_MAX_SHADES];        /**< Shade records (primary domain). */
    uint32_t startingAddress = 0;               /**< Base remote address, derived from the efuse MAC. */
    SomfyTransceiver transceiver;               /**< CC1101 RF transceiver. */

    /**
     * @brief Construct the model and wire the sub-controllers' callbacks.
     */
    SomfyShadeController();

    // ── Methods ─────────────────────────────────────────────────────────────
    /**
     * @brief Allocate and register a new empty shade.
     * @return Pointer to the new shade, or nullptr if no slot is free.
     */
    SomfyShade *addShade();

    /**
     * @brief Allocate a new shade and initialise it from JSON (also bridges it to HomeKit).
     * @param obj Shade configuration.
     * @return Pointer to the new shade.
     */
    SomfyShade *addShade(JsonObject &obj);

    /**
     * @brief Set bitLength from the transceiver config on shades that lack it; commit if any changed.
     */
    void applyDefaultBitLengths();

    /**
     * @brief Persist the config if dirty and more than 1 s has elapsed since the last commit.
     */
    void autoCommit();

    /**
     * @brief Write the full config to LittleFS and clear the dirty flag.
     */
    void commit();

    /**
     * @brief Remove a shade: unbridge from HomeKit, reset the command queue, commit.
     * @param shadeId Id of the shade to delete.
     * @return Always true.
     */
    bool deleteShade(uint8_t shadeId);

    /**
     * @brief Broadcast the state of every active shade over the WebSocket.
     * @param num Target socket number, or 255 to broadcast to all.
     */
    void emitState(uint8_t num = 255);

    /**
     * @brief Find a shade by its primary or any linked-remote address.
     * @param address Remote address to match.
     * @return Pointer to the matching shade, or nullptr if none.
     */
    SomfyShade *findShadeByRemoteAddress(uint32_t address);

    /**
     * @brief Highest sort order currently in use across active shades.
     * @return The max sort order, or -1 if no shades exist.
     */
    int8_t getMaxShadeOrder();

    /**
     * @brief Allocate a free remote address (unique across shades and groups).
     * @param shadeId Offset added to the starting address.
     * @return An unused remote address.
     */
    uint32_t getNextRemoteAddress(uint8_t shadeId);

    /**
     * @brief Find the lowest unused shade id.
     * @return A free shade id, or 255 if none is available.
     */
    uint8_t getNextShadeId();

    /**
     * @brief Find a shade by its id.
     * @param shadeId Id to look up.
     * @return Pointer to the matching shade, or nullptr if none.
     */
    SomfyShade *getShadeById(uint8_t shadeId);

    /**
     * @brief Load shade config from a named file.
     * @param filename Path of the config file.
     * @return True on success.
     */
    bool loadShadesFile(const char *filename);

    /**
     * @brief Dispatch an inbound RF frame to every active shade.
     * @param frame The received frame.
     * @param internal True if the frame originated from this controller (echo).
     */
    void processFrame(somfy_frame_t &frame, bool internal = false);

    /**
     * @brief Process any deferred (waiting) frame on active shades.
     */
    void processWaitingFrame();

    /**
     * @brief Publish all shade and group state to MQTT.
     */
    void publish();

    /**
     * @brief Count the shades with an assigned id.
     * @return Number of active shades.
     */
    uint8_t shadeCount();

    /**
     * @brief Advance movement tracking and GPIO outputs for every active shade (per loop tick).
     */
    void tickShades();

    /**
     * @brief Serialise all active shades into a JSON array.
     * @param json Response to append shade objects to.
     */
    void toJSONShades(JsonResponse &json);

    /**
     * @brief Write a backup snapshot of the config.
     */
    void writeBackup();
};
