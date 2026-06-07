#pragma once
// WebSettings.h — HTTP handler module for device configuration.
//
// Concrete WebHandler owning the settings routes (radio, general, network/Wi-Fi,
// MQTT, module). All routes are on the public HTTP server only.
// begin() registers the routes; end() is a no-op (WebServer has no per-route
// removal). Security/auth handlers stay in Web (they need the private API-token
// helpers).

#include <WebServer.h>
#include "WebHandler.h"

class WebSettings : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every settings route on the public server. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Save transceiver/radio configuration.
     * @param server The server the request arrived on.
     */
    void handleSaveRadio(WebServer &server);

    /**
     * @brief Return the current transceiver/radio configuration.
     * @param server The server the request arrived on.
     */
    void handleGetRadio(WebServer &server);

    /**
     * @brief Save general device settings (hostname, update check, NTP).
     * @param server The server the request arrived on.
     */
    void handleSetGeneral(WebServer &server);

    /**
     * @brief Save network settings (connection type, Wi-Fi, Ethernet).
     * @param server The server the request arrived on.
     */
    void handleSetNetwork(WebServer &server);

    /**
     * @brief Save static IP settings.
     * @param server The server the request arrived on.
     */
    void handleSetIP(WebServer &server);

    /**
     * @brief Validate and apply a Wi-Fi SSID/passphrase, rebooting if changed.
     * @param server The server the request arrived on.
     */
    void handleConnectWifi(WebServer &server);

    /**
     * @brief Return module-level settings (firmware/build version, NTP).
     * @param server The server the request arrived on.
     */
    void handleModuleSettings(WebServer &server);

    /**
     * @brief Return network settings (Ethernet, Wi-Fi, IP).
     * @param server The server the request arrived on.
     */
    void handleNetworkSettings(WebServer &server);

    /**
     * @brief Save MQTT broker configuration and reconnect.
     * @param server The server the request arrived on.
     */
    void handleConnectMQTT(WebServer &server);

    /**
     * @brief Return the current MQTT broker configuration.
     * @param server The server the request arrived on.
     */
    void handleMQTTSettings(WebServer &server);
};
