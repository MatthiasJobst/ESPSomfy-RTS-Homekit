#include "SomfyRoomController.h"
#include <cassert>
#include "SomfyRoom.h"
#include "SlotArray.h"
#include "WResp.h" // JsonResponse, used by toJSONRooms()

SomfyRoomController::SomfyRoomController(std::function<void()> markDirty, std::function<void(uint8_t)> onRoomRemoved)
    : markDirty(std::move(markDirty)), onRoomRemoved(std::move(onRoomRemoved))
{}

SomfyRoom &SomfyRoomController::roomSlot(uint8_t i)
{
    assert(i < SOMFY_MAX_ROOMS);
    return this->rooms[i];
}

SomfyRoom *SomfyRoomController::getRoomById(uint8_t roomId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
        if (this->rooms[i].roomId == roomId) return &this->rooms[i];
    }
    return nullptr;
}

uint8_t SomfyRoomController::getNextRoomId()
{
    return slots::lowestFreeId(this->rooms);
}

int8_t SomfyRoomController::getMaxRoomOrder()
{
    int16_t order = -1;
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
        SomfyRoom *room = &this->rooms[i];
        if (room->roomId == SomfyRoom::NO_ID) continue;
        if (order < room->sortOrder) order = room->sortOrder;
    }
    assert(order <= 127);
    return static_cast<int8_t>(order);
}

uint8_t SomfyRoomController::roomCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
        if (this->rooms[i].roomId != SomfyRoom::NO_ID) count++;
    }
    return count;
}

SomfyRoom *SomfyRoomController::addRoom()
{
    uint8_t roomId = this->getNextRoomId();
    if (roomId == SomfyRoom::NO_ID) return nullptr;
    // The slot index is NOT roomId - 1: persistence compacts rooms into the front
    // slots on load, so slot N does not hold roomId N+1 after a reboot. Place the
    // new room in the first empty slot and look it up by id (see addShade() for
    // the shade equivalent of this bug).
    SomfyRoom *room = slots::firstEmptySlot(this->rooms);
    if (!room) return nullptr;
    room->sortOrder = static_cast<int8_t>(this->getMaxRoomOrder() + 1);
    room->roomId = roomId;
    if (this->markDirty) this->markDirty();
    return room;
}

SomfyRoom *SomfyRoomController::addRoom(JsonObject &obj)
{
    SomfyRoom *room = this->addRoom();
    if (room) {
        room->fromJSON(obj);
        room->save();
        room->emitState("roomAdded");
    }
    return room;
}

bool SomfyRoomController::deleteRoom(uint8_t roomId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
        if (this->rooms[i].roomId == roomId) {
            this->rooms[i].unpublish();
            this->rooms[i].emitState("roomRemoved");
            // Invoke the callback while room data is still available.
            if (this->onRoomRemoved) this->onRoomRemoved(roomId);
            this->rooms[i].clear();
        }
    }
    if (this->markDirty) this->markDirty();
    return true;
}

void SomfyRoomController::toJSONRooms(JsonResponse &json)
{
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
        SomfyRoom *room = &this->rooms[i];
        if (room->roomId != SomfyRoom::NO_ID) {
            json.beginObject();
            room->toJSON(json);
            json.endObject();
        }
    }
}
