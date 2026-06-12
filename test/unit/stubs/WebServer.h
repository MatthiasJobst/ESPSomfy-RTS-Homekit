#pragma once
#include <cstdint>
class WebServer {
  public:
    explicit WebServer(int = 80) {}
    void begin() {}
    void handleClient() {}
    void send(int, const char *, const char *) {}
    void on(const char *, void (*)()) {}
    bool authenticate(const char *, const char *) { return true; }
    void requestAuthentication() {}
    const char *arg(const char *) { return ""; }
    bool hasArg(const char *) { return false; }
};
