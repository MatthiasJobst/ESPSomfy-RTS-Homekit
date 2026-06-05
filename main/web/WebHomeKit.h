#pragma once
// WebHomeKit.h — HTTP handler module for HomeKit status and pairing control.
//
// Concrete WebHandler owning the HomeKit routes on both the public HTTP server
// and the REST API server. begin() registers the routes; end() is a no-op
// (WebServer has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebHomeKit : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every HomeKit route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Return the current HomeKit accessory state as JSON.
     * @param server The server the request arrived on.
     */
    void handleHomeKit(WebServer &server);

    /**
     * @brief Reset all HomeKit pairings (POST).
     * @param server The server the request arrived on.
     */
    void handleHomeKitResetPairings(WebServer &server);
};
