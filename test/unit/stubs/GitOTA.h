// Minimal stub — replaces main/GitOTA.h for unit tests.
#pragma once
#include <cstdint>

#define GIT_MAX_RELEASES 5
#define GIT_STATUS_READY 0
#define GIT_STATUS_CHECK 1
#define GIT_AWAITING_UPDATE 2
#define GIT_UPDATING 3
#define GIT_UPDATE_COMPLETE 4
#define GIT_UPDATE_CANCELLING 5
#define GIT_UPDATE_CANCELLED 6

class GitRelease {
  public:
    uint64_t id = 0;
};
class GitUpdater {
  public:
    uint8_t status = GIT_STATUS_READY;
    bool lockFS = false;
    bool begin() { return false; }
    void loop() {}
    void end() {}
    bool check() { return false; }
    bool update() { return false; }
};
