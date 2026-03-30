// Minimal stub — replaces main/Sockets.h for unit tests.
#pragma once
#include <cstdint>
#include "WResp.h"

#define SOCK_MAX_ROOMS 1
#define ROOM_EMIT_FRAME 0

class SocketEmitter {
public:
    JsonSockEvent json;
    JsonSockEvent *beginEmit(const char *) { return &json; }
    void endEmit(uint8_t = 255) {}
    void endEmitRoom(uint8_t) {}
    void begin() {}
    void loop() {}
    void end() {}
    void disconnect() {}
    void initClients() {}
    void startup() {}
    uint8_t activeClients(uint8_t) { return 0; }
};
