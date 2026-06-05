#pragma once
// WebFiles.h — Serves the static front-end assets from LittleFS over HTTP.
//
// Owns every route that streams a file (index.html, the JS/CSS bundles, icons,
// shades.cfg/.tmp). Concrete WebHandler: begin() registers the routes, end() is
// a no-op because WebServer has no per-route removal.

#include <WebServer.h>
#include "WebHandler.h"

class WebFiles : public WebHandler {
  public:
    using WebHandler::WebHandler;

    void begin() override;
    void end() override;

  private:
    /** @brief One static-asset route: which file to stream and how. */
    struct StaticFile {
        const char *route;    /**< Request path (e.g. "/somfy.js"). */
        const char *file;     /**< LittleFS path streamed in response. */
        const char *encoding; /**< Content-Type sent to the client. */
        bool cache;           /**< Send long-lived cache headers when true. */
    };

    /**
     * @brief Send immutable long-lived Cache-Control headers for static assets.
     *
     * @param seconds Cache lifetime hint (currently fixed at one week).
     */
    void sendCacheHeaders(uint32_t seconds = 604800);

    /**
     * @brief Stream a LittleFS file to the client in 1 KB chunks.
     *
     * Polls the WebSocket server every ~4 KB so a concurrent WS upgrade is not
     * starved, and resets the task watchdog while large files transfer.
     *
     * @param filename The LittleFS path to open and stream.
     * @param encoding The Content-Type to report.
     */
    void handleStreamFile(const char *filename, const char *encoding);

    static const StaticFile s_files[]; /**< Route table, registered in begin(). */
    static const size_t s_fileCount;   /**< Number of entries in s_files. */
};
