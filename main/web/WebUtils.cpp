// WebUtils.cpp — Web handler implementations for utility and scan operations.
//
// Miscellaneous endpoints that don't belong to a single domain:
//   - Wi-Fi AP scanning (handleScanAPs)
//   - Sending raw remote commands (handleSendRemoteCommand)
//   - Frequency scan lifecycle (handleBeginFrequencyScan, handleEndFrequencyScan)
//   - LittleFS filesystem recovery from GitHub (handleRecoverFilesystem)

#include "WebUtils.h"

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"
#include "GitOTA.h"

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern GitUpdater git;

static const char *s_TAG = "WebUtils";

void WebUtils::begin()
{
    registerHandler("/scanaps", [this]() { handleScanAPs(server); });
    registerHandler("/sendRemoteCommand", [this]() { handleSendRemoteCommand(server); });
    registerHandler("/beginFrequencyScan", [this]() { handleBeginFrequencyScan(server); });
    registerHandler("/endFrequencyScan", [this]() { handleEndFrequencyScan(server); });
    registerHandler("/recoverFilesystem", [this]() { handleRecoverFilesystem(server); });
}

void WebUtils::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebUtils::handleScanAPs(WebServer &server)
{
    esp_task_wdt_delete(NULL);
    if (WiFi.getMode() & WIFI_AP) WiFi.disconnect(false);
    int n = WiFi.scanNetworks(false, true);
    esp_task_wdt_add(NULL);
    ESP_LOGI(s_TAG, "Scanned %d networks", n);
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    resp.beginObject("connected");
    resp.addElem("name", settings.WIFI.ssid);
    resp.addElem("passphrase", settings.WIFI.passphrase);
    resp.addElem("strength", (int32_t)WiFi.RSSI());
    resp.addElem("channel", (int32_t)WiFi.channel());
    resp.endObject();
    resp.beginArray("accessPoints");
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue;
        resp.beginObject();
        resp.addElem("name", WiFi.SSID(i).c_str());
        resp.addElem("channel", (int32_t)WiFi.channel(i));
        resp.addElem("strength", (int32_t)WiFi.RSSI(i));
        resp.addElem("macAddress", WiFi.BSSIDstr(i).c_str());
        resp.endObject();
    }
    resp.endArray();
    resp.endObject();
    resp.endResponse();
}

void WebUtils::handleSendRemoteCommand(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        somfy_frame_t frame;
        uint8_t repeats = 0;
        if (server.hasArg("address")) {
            frame.remoteAddress = atoi(server.arg("address").c_str());
            if (server.hasArg("encKey")) frame.encKey = atoi(server.arg("encKey").c_str());
            if (server.hasArg("command")) frame.cmd = translateSomfyCommand(server.arg("command"));
            if (server.hasArg("rcode")) frame.rollingCode = atoi(server.arg("rcode").c_str());
            if (server.hasArg("repeats")) repeats = atoi(server.arg("repeats").c_str());
        } else if (server.hasArg("plain")) {
            StaticJsonDocument<128> doc;
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (err) {
                sendDeserializationError(server, err);
                return;
            } else {
                JsonObject obj = doc.as<JsonObject>();
                String scmd;
                if (obj.containsKey("address")) frame.remoteAddress = obj["address"];
                if (obj.containsKey("command")) scmd = obj["command"].as<String>();
                if (obj.containsKey("repeats")) repeats = obj["repeats"];
                if (obj.containsKey("rcode")) frame.rollingCode = obj["rcode"];
                if (obj.containsKey("encKey")) frame.encKey = obj["encKey"];
                frame.cmd = translateSomfyCommand(scmd.c_str());
            }
        }
        if (frame.remoteAddress > 0 && frame.rollingCode > 0) {
            somfy.sendFrame(frame, repeats);
            server.send(200, ENCODING_JSON, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
        } else
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
    }
}

void WebUtils::handleBeginFrequencyScan(WebServer &server)
{
    somfy.transceiver.beginFrequencyScan();
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}

void WebUtils::handleEndFrequencyScan(WebServer &server)
{
    somfy.transceiver.endFrequencyScan();
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}

void WebUtils::handleRecoverFilesystem(WebServer &server)
{
    if (git.status == GIT_UPDATING)
        server.send(200, "application/json",
                    "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
    else if (git.status != GIT_STATUS_READY)
        server.send(200, "application/json",
                    "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
    else {
        git.recoverFilesystem();
        server.send(200, "application/json",
                    "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
    }
}
