// WebOTA.cpp — Web handler implementations for OTA firmware and backup/restore.
//
// Covers all over-the-air update and data migration endpoints:
//   - Queuing a firmware download from GitHub (handleDownloadFirmware)
//   - Backup restore from uploaded file (handleRestore, handleRestoreUpload)
//   - Firmware binary update (handleUpdateFirmware, handleUpdateFirmwareUpload)
//   - Shade config migration (handleUpdateShadeConfig, handleUpdateShadeConfigUpload)
//   - Application / filesystem update (handleUpdateApplication, handleUpdateApplicationUpload)

#include "Web.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "ConfigSettings.h"
#include "GitOTA.h"
#include "MQTT.h"
#include "ShadeConfigFile.h"
#include "SomfyShadeController.h"
#include "Utils.h"
#include "WResp.h"

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Web webServer;
extern GitUpdater git;
extern rebootDelay_t rebootDelay;
extern MQTTClass mqtt;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char g_encoding_text[];
extern const char g_encoding_json[];

static const char *s_TAG = "WebOTA";

void Web::handleDownloadFirmware(WebServer &server)
{
    ESP_LOGI(s_TAG, "downloadFirmware called...");
    if (!server.hasArg("ver")) {
        server.send(400, g_encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release version not supplied.\"}"));
        return;
    }
    String ver = server.arg("ver");
    // The ver value comes from the dropdown which was populated by getReleases(),
    // so it is already a valid tag name (e.g. "v0.4.2") or "main".
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    resp.addElem("name", ver.c_str());
    resp.endObject();
    resp.endResponse();
    strlcpy(git.targetRelease, ver.c_str(), sizeof(git.targetRelease));
    git.status = GIT_AWAITING_UPDATE;
}

void Web::handleRestore(WebServer &server)
{
    server.sendHeader("Connection", "close");
    if (webServer.uploadSuccess) {
        ESP_LOGI(s_TAG, "Restoring Shade settings");
        server.send(200, g_encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
        restore_options_t opts;
        if (server.hasArg("data")) {
            ESP_LOGI(s_TAG, "Restore data: %s", server.arg("data").c_str());
            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, server.arg("data"));
            if (err) {
                webServer.handleDeserializationError(server, err);
                return;
            } else {
                JsonObject obj = doc.as<JsonObject>();
                opts.fromJSON(obj);
            }
        } else {
            ESP_LOGI(s_TAG, "No restore options sent.  Using defaults...");
            opts.shades = true;
        }
        ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
        ESP_LOGI(s_TAG, "Rebooting ESP for restored settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
    }
}
void Web::handleRestoreUpload(WebServer &server)
{
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        ESP_LOGI(s_TAG, "Restore: %s", upload.filename.c_str());
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(upload.buf, upload.currentSize);
        fup.close();
    } else if (upload.status == UPLOAD_FILE_END) {
        webServer.uploadSuccess = true;
    }
}
void Web::handleUpdateFirmware(WebServer &server)
{
    if (Update.hasError())
        server.send(500, g_encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
    else
        server.send(200, g_encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated firmware\"}");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
}
void Web::handleUpdateFirmwareUpload(WebServer &server)
{
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        ESP_LOGI(s_TAG, "Update: %s - %d", upload.filename.c_str(), upload.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        } else {
            somfy.transceiver.end();
            mqtt.end();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        ESP_LOGE(s_TAG, "Upload of %s aborted", upload.filename.c_str());
        Update.abort();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
            ESP_LOGE(s_TAG, "Upload of %s aborted invalid size %d", upload.filename.c_str(), upload.currentSize);
            Update.abort();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            ESP_LOGI(s_TAG, "Update Success: %u\nRebooting...\n", upload.totalSize);
            webServer.uploadSuccess = true;
        } else {
            Update.printError(Serial);
            ESP_LOGE(s_TAG, "Update failed");
        }
    }
}
void Web::handleUpdateShadeConfig(WebServer &server)
{
    if (git.lockFS) {
        server.send(500, g_encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
        return;
    }
    server.sendHeader("Connection", "close");
    server.send(200, g_encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Shade Config: \"}");
}
void Web::handleUpdateShadeConfigUpload(WebServer &server)
{
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        ESP_LOGI(s_TAG, "Update: shades.cfg");
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            File fup = LittleFS.open("/shades.tmp", "a");
            fup.write(upload.buf, upload.currentSize);
            fup.close();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        somfy.loadShadesFile("/shades.tmp");
    }
}
void Web::handleUpdateApplication(WebServer &server)
{
    server.sendHeader("Connection", "close");
    if (Update.hasError()) {
        snprintf(g_content, sizeof(g_content), "{\"status\":\"ERROR\",\"desc\":\"%s\"}", Update.errorString());
        server.send(500, g_encoding_json, g_content);
    } else {
        server.send(200, g_encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated application\"}");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 500;
    }
}
void Web::handleUpdateApplicationUpload(WebServer &server)
{
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        webServer.uploadSuccess = false;
        ESP_LOGI(s_TAG, "Update: %s %d", upload.filename.c_str(), upload.totalSize);
        somfy.commit();
        LittleFS.end();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASHFS)) {
            Update.printError(Serial);
        } else {
            somfy.transceiver.end();
            mqtt.end();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        ESP_LOGE(s_TAG, "Upload of %s aborted", upload.filename.c_str());
        Update.abort();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
            ESP_LOGE(s_TAG, "Upload of %s aborted invalid size %d", upload.filename.c_str(), upload.currentSize);
            Update.abort();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            webServer.uploadSuccess = true;
            ESP_LOGI(s_TAG, "Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}
