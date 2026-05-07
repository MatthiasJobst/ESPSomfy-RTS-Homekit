// SomfyRoom.h — SomfyRoom: named room with sort order, NVS persistence, MQTT
// publishing and socket-emit helpers.
#pragma once
#include "SomfyFrame.h"

class SomfyRoom {
  public:
    uint8_t roomId = 0;
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
