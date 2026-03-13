#pragma once
#include <stdint.h>

// Forward declarations — avoid pulling in heavy headers here.
class SomfyShade;
class JsonResponse;

class HomeKitClass {
public:
    // Call once from setup(), after net.setup() and somfy.begin().
    // Initialises the HAP stack, creates the bridge and all shade accessories,
    // sets the pairing code and starts hap_start().
    void begin();

    // Push current position/target/direction for one shade to Apple Home.
    // Called from SomfyShade::publishState().
    void notifyShadeState(SomfyShade *shade);

    // Add / remove a shade from the HAP bridge at runtime (called when a shade
    // is added or deleted via the web UI).
    void addShade(SomfyShade *shade);
    void removeShade(SomfyShade *shade);

    // Reset all pairings (removes all paired controllers).
    void resetPairings();

    // Serialize HomeKit status to JSON for the /homekit API endpoint.
    void toJSON(JsonResponse &resp);

    bool isStarted() const { return _started; }

private:
    bool _started = false;
    char _qrPayload[64] = {};
};

extern HomeKitClass homekit;
