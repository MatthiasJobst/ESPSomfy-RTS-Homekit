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
#include "Web.h"
#include "WebJsonResponder.h"
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
    WebJsonResponder json(server);
    esp_task_wdt_delete(NULL);
    if (WiFi.getMode() & WIFI_AP) WiFi.disconnect(false);
    int n = WiFi.scanNetworks(false, true);
    esp_task_wdt_add(NULL);
    ESP_LOGI(s_TAG, "Scanned %d networks", n);
    auto objJson = json.respondJson().object();
    objJson.beginObject("connected");
    objJson.addElem("name", settings.WIFI.ssid);
    objJson.addElem("passphrase", settings.WIFI.passphrase);
    objJson.addElem("strength", (int32_t)WiFi.RSSI());
    objJson.addElem("channel", (int32_t)WiFi.channel());
    objJson.endObject();
    objJson.beginArray("accessPoints");
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue;
        objJson.beginObject();
        objJson.addElem("name", WiFi.SSID(i).c_str());
        objJson.addElem("channel", (int32_t)WiFi.channel(i));
        objJson.addElem("strength", (int32_t)WiFi.RSSI(i));
        objJson.addElem("macAddress", WiFi.BSSIDstr(i).c_str());
        objJson.endObject();
    }
    objJson.endArray();
}

void WebUtils::handleSendRemoteCommand(WebServer &server)
{
    WebJsonResponder json(server);
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
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            String scmd;
            if (obj.containsKey("address")) frame.remoteAddress = obj["address"];
            if (obj.containsKey("command")) scmd = obj["command"].as<String>();
            if (obj.containsKey("repeats")) repeats = obj["repeats"];
            if (obj.containsKey("rcode")) frame.rollingCode = obj["rcode"];
            if (obj.containsKey("encKey")) frame.encKey = obj["encKey"];
            frame.cmd = translateSomfyCommand(scmd.c_str());
        }
        if (frame.remoteAddress > 0 && frame.rollingCode > 0) {
            somfy.sendFrame(frame, repeats);
            json.respondJson().success("Command Sent");
        } else
            json.respondJson().error("No address or rolling code provided");
    }
}

void WebUtils::handleBeginFrequencyScan(WebServer &server)
{
    WebJsonResponder json(server);
    somfy.transceiver.beginFrequencyScan();
    auto objJson = json.respondJson().object();
    somfy.transceiver.toJSON(objJson);
}

void WebUtils::handleEndFrequencyScan(WebServer &server)
{
    WebJsonResponder json(server);
    somfy.transceiver.endFrequencyScan();
    auto objJson = json.respondJson().object();
    somfy.transceiver.toJSON(objJson);
}

void WebUtils::handleRecoverFilesystem(WebServer &server)
{
    WebJsonResponder json(server);
    if (git.status == GIT_UPDATING)
        json.respondJson().ok("Filesystem is updating.  Please wait!!!");
    else if (git.status != GIT_STATUS_READY)
        json.respondJson().error("Cannot recover file system at this time.");
    else {
        git.recoverFilesystem();
        json.respondJson().ok("Recovering filesystem from github please wait!!!");
    }
}
