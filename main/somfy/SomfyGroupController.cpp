#include "SomfyGroupController.h"
#include <cassert>
#include <esp_log.h>
#include "SomfyGroup.h"
#include "WResp.h" // JsonResponse, used by toJSONGroups()

static const char *s_TAG = "SomfyGroupController";

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
    // There is no shortcut for this since the deletion of
    // a group in the middle makes all of this very difficult.
    for (uint8_t i = 1; i < SOMFY_MAX_GROUPS - 1; i++) {
        bool id_exists = false;
        for (uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
            SomfyGroup *group = &this->groups[j];
            if (group->getGroupId() == i) {
                id_exists = true;
                break;
            }
        }
        if (!id_exists) {
            ESP_LOGI(s_TAG, "Got next Group Id:%d", i);
            return i;
        }
    }
    return 255;
}

int8_t SomfyGroupController::getMaxGroupOrder()
{
    int16_t order = -1;
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup *group = &this->groups[i];
        if (group->getGroupId() == 255) continue;
        if (order < group->sortOrder) order = group->sortOrder;
    }
    assert(order <= 127);
    return static_cast<int8_t>(order);
}

uint8_t SomfyGroupController::groupCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        if (this->groups[i].getGroupId() != 255) count++;
    }
    return count;
}

SomfyGroup *SomfyGroupController::addGroup()
{
    uint8_t groupId = this->getNextGroupId();
    assert((groupId != 0) && (groupId != 255)); // getNextGroupId() returns 255 on failure
    // The next id is the first slot with an id of 255, so a deleted middle slot
    // gets reused. See addShade(): compute max before assigning id so the new
    // (still id=255) slot is skipped by getMaxGroupOrder().
    SomfyGroup *group = &this->groups[groupId - 1];
    group->sortOrder = static_cast<int8_t>(this->getMaxGroupOrder() + 1);
    group->setGroupId(groupId);
    if (this->markDirty) this->markDirty();
    if (this->resetCommands) this->resetCommands(); // drop commands bound to now-stale group slots
    return group;
}

SomfyGroup *SomfyGroupController::addGroup(JsonObject &obj)
{
    SomfyGroup *group = this->addGroup();
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
        if (group.getGroupId() != 255) {
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
        if (group->getGroupId() != 255) {
            SomfyFlag oldFlags = group->flags;
            group->updateFlags();
            if (oldFlags != group->flags) group->emitState();
        }
    }
}
