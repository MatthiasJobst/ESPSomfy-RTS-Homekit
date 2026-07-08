#include "SomfyGroupController.h"
#include <cassert>
#include "SomfyGroup.h"
#include "SlotArray.h"
#include "WResp.h" // JsonResponse, used by toJSONGroups()

SomfyGroupController::SomfyGroupController(std::function<void()> markDirty, std::function<void()> resetCommands)
    : markDirty(std::move(markDirty)), resetCommands(std::move(resetCommands))
{}

SomfyGroup &SomfyGroupController::groupSlot(uint8_t i)
{
    assert(i < SOMFY_MAX_GROUPS);
    return this->groups[i];
}

SomfyGroup *SomfyGroupController::getGroupById(uint8_t groupId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        if (this->groups[i].getGroupId() == groupId) return &this->groups[i];
    }
    return nullptr;
}

SomfyGroup *SomfyGroupController::findGroupByRemoteAddress(uint32_t address)
{
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup &group = this->groups[i];
        if (group.getRemoteAddress() == address) return &group;
    }
    return nullptr;
}

uint8_t SomfyGroupController::getNextGroupId()
{
    return slots::lowestFreeId(this->groups);
}

int8_t SomfyGroupController::getMaxGroupOrder()
{
    int16_t order = -1;
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup *group = &this->groups[i];
        if (group->getGroupId() == SomfyGroup::NO_ID) continue;
        if (order < group->sortOrder) order = group->sortOrder;
    }
    assert(order <= 127);
    return static_cast<int8_t>(order);
}

uint8_t SomfyGroupController::groupCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        if (this->groups[i].getGroupId() != SomfyGroup::NO_ID) count++;
    }
    return count;
}

SomfyGroup *SomfyGroupController::addGroup()
{
    uint8_t groupId = this->getNextGroupId();
    if (groupId == 0 || groupId == SomfyGroup::NO_ID) return nullptr;
    // The slot index is NOT groupId - 1: persistence compacts groups into the
    // front slots on load, so slot N does not hold groupId N+1 after a reboot.
    // Place the new group in the first empty slot and look it up by id (see
    // addShade() for the shade equivalent of this bug).
    SomfyGroup *group = slots::firstEmptySlot(this->groups);
    if (!group) return nullptr;
    // Compute max before assigning id so the new (still id=255) slot is skipped
    // by getMaxGroupOrder().
    group->sortOrder = static_cast<int8_t>(this->getMaxGroupOrder() + 1);
    group->setGroupId(groupId);
    if (this->markDirty) this->markDirty();
    if (this->resetCommands) this->resetCommands(); // drop commands bound to now-stale group slots
    return group;
}

SomfyGroup *SomfyGroupController::addGroup(JsonObject &obj)
{
    SomfyGroup *group = this->addGroup();
    if (!group) return nullptr;
    group->fromJSON(obj);
    group->save();
    group->emitState("groupAdded");
    return group;
}

bool SomfyGroupController::deleteGroup(uint8_t groupId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        if (this->groups[i].getGroupId() == groupId) {
            this->groups[i].emitState("groupRemoved");
            this->groups[i].unpublish();
            this->groups[i].clear();
        }
    }
    if (this->resetCommands) this->resetCommands(); // drop commands bound to the removed group
    if (this->markDirty) this->markDirty();
    return true;
}

void SomfyGroupController::toJSONGroups(JsonResponse &json)
{
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup &group = this->groups[i];
        if (group.getGroupId() != SomfyGroup::NO_ID) {
            json.beginObject();
            group.toJSON(json);
            json.endObject();
        }
    }
}

void SomfyGroupController::updateGroupFlags()
{
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup *group = &this->groups[i];
        if (group->getGroupId() != SomfyGroup::NO_ID) {
            SomfyFlag oldFlags = group->flags;
            group->updateFlags();
            if (oldFlags != group->flags) group->emitState();
        }
    }
}
