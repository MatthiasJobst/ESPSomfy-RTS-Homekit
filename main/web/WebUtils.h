#pragma once
// WebUtils.h — HTTP handler module for utility and scan operations.
//
// Concrete WebHandler owning the miscellaneous endpoints that don't belong to a
// single domain (Wi-Fi scan, raw remote command, frequency scan, FS recovery).
// All routes are on the public HTTP server only. begin() registers the routes;
// end() is a no-op (WebServer has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebUtils : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every utility route on the public server. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Scan for nearby Wi-Fi access points and report the current link.
     * @param server The server the request arrived on.
     */
    void handleScanAPs(WebServer &server);

    /**
     * @brief Transmit a raw Somfy frame from an explicit address/rolling code.
     * @param server The server the request arrived on.
     */
    void handleSendRemoteCommand(WebServer &server);

    /**
     * @brief Start a transceiver frequency scan and return its state.
     * @param server The server the request arrived on.
     */
    void handleBeginFrequencyScan(WebServer &server);

    /**
     * @brief Stop a transceiver frequency scan and return its state.
     * @param server The server the request arrived on.
     */
    void handleEndFrequencyScan(WebServer &server);

    /**
     * @brief Re-download the LittleFS image from GitHub when idle.
     * @param server The server the request arrived on.
     */
    void handleRecoverFilesystem(WebServer &server);
};
