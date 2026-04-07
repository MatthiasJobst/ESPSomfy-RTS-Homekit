// Minimal stub — replaces main/HomeKit.h for unit tests.
#pragma once
#include <cstdint>
class SomfyShade;
class JsonResponse;

class HomeKitClass {
public:
    void begin() {}
    void notifyShadeState(SomfyShade *) {}
    void addShade(SomfyShade *)    {}
    void removeShade(SomfyShade *) {}
    void resetPairings() {}
    void toJSON(JsonResponse &) {}
    void end() {}
    bool isStarted() const { return false; }
};

extern HomeKitClass homekit;
