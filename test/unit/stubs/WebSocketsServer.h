#pragma once
#include <cstdint>
#include <cstddef>

enum WStype_t {
    WStype_ERROR, WStype_DISCONNECTED, WStype_CONNECTED,
    WStype_TEXT, WStype_BIN, WStype_PING, WStype_PONG, WStype_FRAGMENT
};

class WebSocketsServer {
public:
    explicit WebSocketsServer(int) {}
    void begin() {}
    void loop() {}
    void onEvent(void(*)(uint8_t, WStype_t, uint8_t *, size_t)) {}
    void sendTXT(uint8_t, const char *) {}
    void sendTXT(uint8_t, const uint8_t *, size_t) {}
    void disconnect(uint8_t) {}
    bool isConnected(uint8_t) { return false; }
    uint8_t connectedClients() { return 0; }
};
