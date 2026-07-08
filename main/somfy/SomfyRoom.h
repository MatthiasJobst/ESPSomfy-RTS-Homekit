// SomfyRoom.h — SomfyRoom: named room with sort order, NVS persistence, MQTT
// publishing and socket-emit helpers.
#pragma once
#include "SomfyFrame.h"

class SomfyRoom {
  public:
    /// Sentinel id marking an empty room slot (and the "no room" foreign-key
    /// value stored on shades/groups). Rooms are allocated ids 1..SOMFY_MAX_ROOMS.
    static constexpr uint8_t NO_ID = 0;
    uint8_t roomId = NO_ID;
    char name[21] = "";
    uint8_t sortOrder = 0;
    void clear();
    bool save();
    bool fromJSON(JsonObject &obj);
    void toJSON(JsonResponse &json);
    void emitState(const char *evt = "roomState");
    void emitState(uint8_t num, const char *evt = "roomState");
    void publish();
    void unpublish();
};

/// ADL id accessor for the SlotArray helpers (slots::lowestFreeId / firstEmptySlot).
inline uint8_t slotId(SomfyRoom &room) { return room.roomId; }
