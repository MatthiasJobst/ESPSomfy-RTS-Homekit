// SomfyGroupController.h — Owns the group collection: id allocation, lookup,
// add/delete, flag updates and JSON serialisation.  Groups are command targets,
// so structural changes invoke a callback to reset the owner's command queue.
#pragma once

#include <functional>
#include <ArduinoJson.h>
#include "SomfyGroup.h"

class JsonResponse;

/**
 * @brief Manages the fixed-size array of SomfyGroup records.
 */
class SomfyGroupController {
  public:
    /**
     * @brief Construct an empty group collection.
     * @param markDirty Called on every group mutation so the owner can flag the
     *                  config for persistence.
     * @param resetCommands Called on structural changes (add/delete) so the owner
     *                      can drop queued commands bound to now-stale group slots.
     */
    SomfyGroupController(std::function<void()> markDirty, std::function<void()> resetCommands);

    /**
     * @brief Direct access to a group storage slot by index.
     * @param i Slot index in [0, SOMFY_MAX_GROUPS).
     * @return Reference to the (possibly empty) group record at that slot.
     * @note Low-level seam for persistence (ShadeConfigFile) and tests; prefer
     *       getGroupById()/addGroup()/deleteGroup() for normal use.
     */
    SomfyGroup &groupSlot(uint8_t i);

    /**
     * @brief Find a group by its id.
     * @param groupId Id to look up.
     * @return Pointer to the matching group, or nullptr if none.
     */
    SomfyGroup *getGroupById(uint8_t groupId);

    /**
     * @brief Find a group by its remote address.
     * @param address Remote address to look up.
     * @return Pointer to the matching group, or nullptr if none.
     */
    SomfyGroup *findGroupByRemoteAddress(uint32_t address);

    /**
     * @brief Find the lowest unused group id.
     * @return A free group id, or 255 if none is available.
     */
    uint8_t getNextGroupId();

    /**
     * @brief Highest sort order currently in use across active groups.
     * @return The max sort order, or -1 if no groups exist.
     */
    int8_t getMaxGroupOrder();

    /**
     * @brief Count the groups with an assigned id.
     * @return Number of active groups.
     */
    uint8_t groupCount();

    /**
     * @brief Allocate and register a new empty group.
     * @return Pointer to the new group.
     * @note Asserts a free id is available; callers must ensure groupCount() < SOMFY_MAX_GROUPS.
     */
    SomfyGroup *addGroup();

    /**
     * @brief Allocate a new group and initialise it from JSON.
     * @param obj Group configuration.
     * @return Pointer to the new group.
     */
    SomfyGroup *addGroup(JsonObject &obj);

    /**
     * @brief Remove a group; resets the command queue and flags the config dirty.
     * @param groupId Id of the group to delete.
     * @return Always true.
     */
    bool deleteGroup(uint8_t groupId);

    /**
     * @brief Serialise all active groups into a JSON array.
     * @param json Response to append group objects to.
     */
    void toJSONGroups(JsonResponse &json);

    /**
     * @brief Recompute sensor flags for every active group, emitting on change.
     */
    void updateGroupFlags();

  private:
    SomfyGroup groups[SOMFY_MAX_GROUPS]; /**< Group records, owned by this controller. */
    std::function<void()> markDirty;     /**< Owner callback, invoked on group mutations. */
    std::function<void()> resetCommands; /**< Owner callback, invoked on structural changes. */
};
