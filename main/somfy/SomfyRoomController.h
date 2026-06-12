// SomfyRoomController.h — Owns the room collection: id allocation, lookup,
// add/delete and JSON serialisation.  Invokes an owner-supplied callback when a
// room is removed so dependent shades/groups can drop their references.
#pragma once

#include <functional>
#include <ArduinoJson.h>
#include "SomfyRoom.h"

class JsonResponse;

/**
 * @brief Manages the fixed-size array of SomfyRoom records.
 */
class SomfyRoomController {
  public:
    /**
     * @brief Construct an empty room collection.
     * @param markDirty Called on every room mutation so the owner can flag the
     *                  config for persistence.
     * @param onRoomRemoved Called with the room id after a room is deleted, so
     *                      the owner can clear dependent shade/group references.
     */
    SomfyRoomController(std::function<void()> markDirty, std::function<void(uint8_t)> onRoomRemoved);

    /**
     * @brief Direct access to a room storage slot by index.
     * @param i Slot index in [0, SOMFY_MAX_ROOMS).
     * @return Reference to the (possibly empty) room record at that slot.
     * @note Low-level seam for persistence (ShadeConfigFile) and tests; prefer
     *       getRoomById()/addRoom()/deleteRoom() for normal use.
     */
    SomfyRoom &roomSlot(uint8_t i);

    /**
     * @brief Find a room by its id.
     * @param roomId Id to look up.
     * @return Pointer to the matching room, or nullptr if none.
     */
    SomfyRoom *getRoomById(uint8_t roomId);

    /**
     * @brief Find the lowest unused room id.
     * @return A free room id, or 0 if none is available.
     */
    uint8_t getNextRoomId();

    /**
     * @brief Highest sort order currently in use across active rooms.
     * @return The max sort order, or -1 if no rooms exist.
     */
    int8_t getMaxRoomOrder();

    /**
     * @brief Count the rooms with an assigned id.
     * @return Number of active rooms.
     */
    uint8_t roomCount();

    /**
     * @brief Allocate and register a new empty room.
     * @return Pointer to the new room.
     * @note Asserts a free id is available; callers must ensure roomCount() < SOMFY_MAX_ROOMS.
     */
    SomfyRoom *addRoom();

    /**
     * @brief Allocate a new room and initialise it from JSON.
     * @param obj Room configuration.
     * @return Pointer to the new room.
     */
    SomfyRoom *addRoom(JsonObject &obj);

    /**
     * @brief Remove a room and invoke the onRoomRemoved callback.
     * @param roomId Id of the room to delete.
     * @return Always true.
     */
    bool deleteRoom(uint8_t roomId);

    /**
     * @brief Serialise all active rooms into a JSON array.
     * @param json Response to append room objects to.
     */
    void toJSONRooms(JsonResponse &json);

  private:
    SomfyRoom rooms[SOMFY_MAX_ROOMS];           /**< Room records, owned by this controller. */
    std::function<void()> markDirty;            /**< Owner callback, invoked on room mutations. */
    std::function<void(uint8_t)> onRoomRemoved; /**< Owner callback, invoked after a room is deleted. */
};
