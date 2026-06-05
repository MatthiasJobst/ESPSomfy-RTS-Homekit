#pragma once
// WebSystem.h — HTTP handler module for system-level and aggregate endpoints.
//
// Concrete WebHandler owning the cross-cutting endpoints that don't belong to a
// single device domain: the aggregate controller/discovery snapshots, the
// not-found fallback, and the backup/reboot system operations. begin() registers
// the routes (including onNotFound on both servers); end() is a no-op (WebServer
// has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebSystem : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every system route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Stream the full controller snapshot (limits, transceiver, rooms, shades, groups).
     * @param server The server the request arrived on.
     */
    void handleController(WebServer &server);

    /**
     * @brief Stream the discovery payload (identity, memory, connectivity, devices).
     * @param server The server the request arrived on.
     */
    void handleDiscovery(WebServer &server);

    /**
     * @brief Fallback for unmatched routes (200 for OPTIONS, otherwise 404).
     * @param server The server the request arrived on.
     */
    void handleNotFound(WebServer &server);

    /**
     * @brief Write and stream the controller backup file.
     * @param server The server the request arrived on.
     * @param attach When true, send a download Content-Disposition header.
     */
    void handleBackup(WebServer &server, bool attach = false);

    /**
     * @brief Schedule a device reboot.
     * @param server The server the request arrived on.
     */
    void handleReboot(WebServer &server);
};
