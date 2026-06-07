#pragma once
// WebOTA.h — HTTP handler module for OTA firmware, backup/restore and migration.
//
// Concrete WebHandler owning the over-the-air update endpoints. The /restore and
// /update* routes are POST with a file-upload callback (the 4-arg registerHandler
// overload). begin() registers the routes; end() is a no-op (WebServer has no
// per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebOTA : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every OTA route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief List the available GitHub releases and current version.
     * @param server The server the request arrived on.
     */
    void handleGetReleases(WebServer &server);

    /**
     * @brief Cancel an in-progress firmware download (not during FS update).
     * @param server The server the request arrived on.
     */
    void handleCancelFirmware(WebServer &server);

    /**
     * @brief Queue a firmware download from GitHub for the supplied version.
     * @param server The server the request arrived on.
     */
    void handleDownloadFirmware(WebServer &server);

    /**
     * @brief Apply a previously uploaded backup, then reboot.
     * @param server The server the request arrived on.
     */
    void handleRestore(WebServer &server);

    /**
     * @brief Upload callback: stream the backup file to LittleFS.
     * @param server The server the request arrived on.
     */
    void handleRestoreUpload(WebServer &server);

    /**
     * @brief Finish a firmware binary update and reboot.
     * @param server The server the request arrived on.
     */
    void handleUpdateFirmware(WebServer &server);

    /**
     * @brief Upload callback: stream the firmware image into the OTA partition.
     * @param server The server the request arrived on.
     */
    void handleUpdateFirmwareUpload(WebServer &server);

    /**
     * @brief Finish a shade config migration upload.
     * @param server The server the request arrived on.
     */
    void handleUpdateShadeConfig(WebServer &server);

    /**
     * @brief Upload callback: stream the shade config file and reload it.
     * @param server The server the request arrived on.
     */
    void handleUpdateShadeConfigUpload(WebServer &server);

    /**
     * @brief Finish an application/filesystem update and reboot.
     * @param server The server the request arrived on.
     */
    void handleUpdateApplication(WebServer &server);

    /**
     * @brief Upload callback: stream the filesystem image into the FS partition.
     * @param server The server the request arrived on.
     */
    void handleUpdateApplicationUpload(WebServer &server);
};
