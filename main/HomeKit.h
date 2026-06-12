#pragma once
#include <stdint.h>

// Forward declarations — avoid pulling in heavy headers here.
class SomfyShade;
class JsonResponse;

class HomeKitClass {
  public:
    /**
     * @brief Initialise the HAP stack, create the bridge and all shade accessories,
     *        set the pairing code, and call hap_start().
     *
     * @note Must be called after net.setup() and stateMachine.begin(). Safe to call
     *       repeatedly — returns immediately if already started.
     */
    void begin();

    /**
     * @brief Push current position, target, and direction for one shade to Apple Home.
     *
     * @param shade Shade whose state has changed. Must not be NULL.
     */
    void notifyShadeState(SomfyShade *shade);

    /**
     * @brief Add a shade to the HAP bridge at runtime.
     *
     * Called when a new shade is added via the web UI.
     *
     * @param shade Shade to add. Ignored if NULL, unnamed, or id == 255.
     */
    void addShade(SomfyShade *shade);

    /**
     * @brief Remove a shade from the HAP bridge at runtime.
     *
     * Called when a shade is deleted via the web UI.
     *
     * @param shade Shade to remove. Ignored if NULL.
     */
    void removeShade(SomfyShade *shade);

    /**
     * @brief Remove all paired HomeKit controllers.
     */
    void resetPairings();

    /**
     * @brief Serialize HomeKit status to JSON for the /homekit API endpoint.
     *
     * @param resp JSON response object to write into.
     */
    void toJSON(JsonResponse &resp);

    /**
     * @brief Return whether the HAP stack has been started.
     *
     * @return true after begin() has completed successfully, false otherwise.
     */
    bool isStarted() const { return _started; }

    /**
     * @brief Derive a setup code from the chip MAC address, store it internally,
     *        and return a pointer to it.
     *
     * @return Pointer to the null-terminated setup code string (e.g. "123-45-678").
     */
    const char *prefab();

    /**
     * @brief Store a setup code that was previously read from NVS.
     *
     * @param code Null-terminated setup code string. Copied internally.
     */
    void setCode(const char *code);

  private:
    bool _started = false;
    char _setupCode[12] = "459-23-871"; /**< Active pairing code; overridden by prefab() or setCode() before begin(). */
    char _qrPayload[64] = {};           /**< QR-code payload string returned by esp_hap_get_setup_payload(). */
};

extern HomeKitClass homekit;
