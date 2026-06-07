// WebOTA.cpp — Web handler implementations for OTA firmware and backup/restore.
//
// Covers all over-the-air update and data migration endpoints:
//   - Queuing a firmware download from GitHub (handleDownloadFirmware)
//   - Backup restore from uploaded file (handleRestore, handleRestoreUpload)
//   - Firmware binary update (handleUpdateFirmware, handleUpdateFirmwareUpload)
//   - Shade config migration (handleUpdateShadeConfig, handleUpdateShadeConfigUpload)
//   - Application / filesystem update (handleUpdateApplication, handleUpdateApplicationUpload)

#include "WebOTA.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "GitOTA.h"
#include "OtaService.h"
#include "Web.h"
#include "WebJsonResponder.h"

extern GitUpdater git;
extern OtaService ota;

static const char *s_TAG = "WebOTA";

void WebOTA::begin()
{
    // REST API routes
    registerApiHandler("/downloadFirmware", [this]() { handleDownloadFirmware(apiServer); });
    // Web UI routes
    registerHandler("/getReleases", [this]() { handleGetReleases(server); });
    registerHandler("/cancelFirmware", [this]() { handleCancelFirmware(server); });
    registerHandler("/downloadFirmware", [this]() { handleDownloadFirmware(server); });
    registerHandler(
        "/restore", HTTP_POST, [this]() { handleRestore(server); }, [this]() { handleRestoreUpload(server); });
    registerHandler(
        "/updateFirmware", HTTP_POST, [this]() { handleUpdateFirmware(server); },
        [this]() { handleUpdateFirmwareUpload(server); });
    registerHandler(
        "/updateShadeConfig", HTTP_POST, [this]() { handleUpdateShadeConfig(server); },
        [this]() { handleUpdateShadeConfigUpload(server); });
    registerHandler(
        "/updateApplication", HTTP_POST, [this]() { handleUpdateApplication(server); },
        [this]() { handleUpdateApplicationUpload(server); });
}

void WebOTA::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebOTA::handleGetReleases(WebServer &server)
{
    WebJsonResponder json(server);
    GitRepo repo;
    repo.getReleases();
    git.setCurrentRelease(repo);
    auto objJson = json.respondJson().object();
    repo.toJSON(objJson);
}
void WebOTA::handleCancelFirmware(WebServer &server)
{
    WebJsonResponder json(server);
    // If we are currently downloading the filesystem we cannot cancel.
    if (!ota.filesystemLocked()) {
        ota.cancelFirmwareUpdate();
        auto objJson = json.respondJson().object();
        git.toJSON(objJson);
    } else {
        json.respondJson().error("Cannot cancel during filesystem update.");
    }
}
void WebOTA::handleDownloadFirmware(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "downloadFirmware called...");
    if (!server.hasArg("ver")) {
        json.respondJson().error("Release version not supplied.", 400);
        return;
    }
    String ver = server.arg("ver");
    // The ver value comes from the dropdown which was populated by getReleases(),
    // so it is already a valid tag name (e.g. "v0.4.2") or "main".
    {
        auto objJson = json.respondJson().object();
        objJson.addElem("name", ver.c_str());
    }
    ota.queueFirmwareDownload(ver.c_str());
}

void WebOTA::handleRestore(WebServer &server)
{
    WebJsonResponder json(server);
    server.sendHeader("Connection", "close");
    if (ota.backupReceived()) {
        ESP_LOGI(s_TAG, "Restoring Shade settings");
        json.respondJson().success("Restoring Shade settings");
        restore_options_t opts;
        if (server.hasArg("data")) {
            ESP_LOGI(s_TAG, "Restore data: %s", server.arg("data").c_str());
            JsonObject obj;
            if (!json.parseArg("data", obj)) return;
            opts.fromJSON(obj);
        } else {
            ESP_LOGI(s_TAG, "No restore options sent.  Using defaults...");
            opts.shades = true;
        }
        ota.applyBackup(opts);
    }
}
void WebOTA::handleRestoreUpload(WebServer &server)
{
    ota.backupUploadChunk(server.upload());
}
void WebOTA::handleUpdateFirmware(WebServer &server)
{
    WebJsonResponder json(server);
    if (!ota.lastImageUploadOk())
        json.respondJson().error("Error updating firmware: ");
    else
        json.respondJson().success("Successfully updated firmware");
    ota.requestReboot(500);
}
void WebOTA::handleUpdateFirmwareUpload(WebServer &server)
{
    ota.firmwareUploadChunk(server.upload());
}
void WebOTA::handleUpdateShadeConfig(WebServer &server)
{
    WebJsonResponder json(server);
    if (ota.filesystemLocked()) {
        json.respondJson().error("Filesystem update in progress");
        return;
    }
    server.sendHeader("Connection", "close");
    json.respondJson().ok("Updating Shade Config: ");
}
void WebOTA::handleUpdateShadeConfigUpload(WebServer &server)
{
    ota.shadeConfigUploadChunk(server.upload());
}
void WebOTA::handleUpdateApplication(WebServer &server)
{
    WebJsonResponder json(server);
    server.sendHeader("Connection", "close");
    if (!ota.lastImageUploadOk()) {
        json.respondJson().error(ota.lastImageError());
    } else {
        json.respondJson().success("Successfully updated application");
        ota.requestReboot(500);
    }
}
void WebOTA::handleUpdateApplicationUpload(WebServer &server)
{
    ota.applicationUploadChunk(server.upload());
}
