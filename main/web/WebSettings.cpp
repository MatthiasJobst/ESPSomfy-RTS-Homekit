// WebSettings.cpp — Web handler implementations for device configuration.
//
// Exposes the HTTP API surface for reading and writing all persistent settings:
//   - Radio / transceiver configuration (handleGetRadio, handleSaveRadio)
//   - General device settings (handleSetGeneral)
//   - Network settings and Wi-Fi connection (handleSetNetwork, handleSetIP, handleConnectWifi, handleNetworkSettings)
//   - MQTT broker configuration and connection (handleMQTTSettings, handleConnectMQTT)
//   - Module-level settings (handleModuleSettings)
//   - OTA release listing and firmware download queuing (handleGetReleases, handleCancelFirmware)
//
// Request parsing and JSON/status responses go through a per-handler
// WebJsonResponder (`WebJsonResponder json(server);`): json.parseBody() reads
// the body, and json.respondJson() exposes the writer for
// .object()/.error()/.ok()/… responses. handleSaveRadio/handleSetNetwork keep
// an inline deserializeJson because they answer a bad body with a 400 HTML page
// rather than the facade's standard JSON error.

#include "WebSettings.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "OtaService.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "Web.h"
#include "WebJsonResponder.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "ControllerNetwork.h"

extern ConfigSettings settings;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern MQTTClass mqtt;
extern ControllerNetwork net;
extern OtaService ota;

static const char *s_TAG = "WebSettings";

void WebSettings::begin()
{
    registerHandler("/saveRadio", [this]() { handleSaveRadio(server); });
    registerHandler("/getRadio", [this]() { handleGetRadio(server); });
    registerHandler("/setgeneral", [this]() { handleSetGeneral(server); });
    registerHandler("/setNetwork", [this]() { handleSetNetwork(server); });
    registerHandler("/setIP", [this]() { handleSetIP(server); });
    registerHandler("/connectwifi", [this]() { handleConnectWifi(server); });
    registerHandler("/modulesettings", [this]() { handleModuleSettings(server); });
    registerHandler("/networksettings", [this]() { handleNetworkSettings(server); });
    registerHandler("/connectmqtt", [this]() { handleConnectMQTT(server); });
    registerHandler("/mqttsettings", [this]() { handleMQTTSettings(server); });
}

void WebSettings::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

// ============================================================
// Settings handlers
// ============================================================
void WebSettings::handleSaveRadio(WebServer &server)
{
    WebJsonResponder json(server);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        ESP_LOGE(s_TAG, "Error parsing JSON %s", err.c_str());
        String msg = err.c_str();
        server.send(400, ENCODING_HTML, "Error parsing JSON body<br>" + msg);
    } else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        if (method == HTTP_POST || method == HTTP_PUT) {
            somfy.transceiver.fromJSON(obj);
            somfy.transceiver.save();
            auto objJson = json.respondJson().object();
            somfy.transceiver.toJSON(objJson);
        } else {
            json.respondJson().error("Invalid HTTP Method: ", 403);
        }
    }
}

void WebSettings::handleGetRadio(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    somfy.transceiver.toJSON(objJson);
}

void WebSettings::handleSetGeneral(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Plain: %d %s", server.method(), server.arg("plain").c_str());
    JsonObject obj;
    if (!json.parseBody(obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        if (obj.containsKey("hostname") || obj.containsKey("checkForUpdate") || obj.containsKey("forcedMoveRepeats")) {
            bool checkForUpdate = settings.checkForUpdate;
            settings.fromJSON(obj);
            settings.save();
            if (settings.checkForUpdate != checkForUpdate) ota.emitUpdateCheck();
            if (obj.containsKey("hostname")) net.updateHostname();
        }
        if (obj.containsKey("ntpServer") || obj.containsKey("ntpServer")) {
            settings.NTP.fromJSON(obj);
            settings.NTP.save();
        }
        json.respondJson().ok("Successfully set General Settings");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebSettings::handleSetNetwork(WebServer &server)
{
    WebJsonResponder json(server);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        ESP_LOGE(s_TAG, "Error parsing JSON %s", err.c_str());
        String msg = err.c_str();
        server.send(400, ENCODING_HTML, "Error parsing JSON body<br>" + msg);
    } else {
        JsonObject obj = doc.as<JsonObject>();
        HTTPMethod method = server.method();
        if (method == HTTP_POST || method == HTTP_PUT) {
            if (net.applyNetworkConfig(obj)) {
                ESP_LOGI(s_TAG, "Rebooting ESP for new Network settings...");
                rebootDelay.requestReboot(1000);
            }
            json.respondJson().ok("Successfully set Network Settings");
        } else {
            json.respondJson().error("Invalid HTTP Method: ", 403);
        }
    }
}

void WebSettings::handleSetIP(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Setting IP...");
    JsonObject obj;
    if (!json.parseBody(obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        settings.IP.fromJSON(obj);
        settings.IP.save();
        json.respondJson().ok("Successfully set Network Settings");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebSettings::handleConnectWifi(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Settings WIFI connection...");
    JsonObject obj;
    if (!json.parseBody(obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        String ssid = "";
        String passphrase = "";
        if (obj.containsKey("ssid")) ssid = obj["ssid"].as<String>();
        if (obj.containsKey("passphrase")) passphrase = obj["passphrase"].as<String>();
        bool reboot = false;
        if (net.applyWifiCredentials(ssid.c_str(), passphrase.c_str(), reboot) ==
            ControllerNetwork::WifiApply::NotFound) {
            json.respondJson().error("WiFi Network Does not exist", 400);
        } else {
            json.respondJson().ok("Successfully set server connection");
            if (reboot) {
                ESP_LOGI(s_TAG, "Rebooting ESP for new WiFi settings...");
                rebootDelay.requestReboot(1000);
            }
        }
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebSettings::handleModuleSettings(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    objJson.addElem("fwVersion", settings.fwVersion.name);
    objJson.addElem("buildVersion", getBuildVersion());
    objJson.addElem("gitRepo", GIT_REPO);
    settings.toJSON(objJson);
    settings.NTP.toJSON(objJson);
}

void WebSettings::handleNetworkSettings(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    settings.toJSON(objJson);
    objJson.addElem("fwVersion", settings.fwVersion.name);
    objJson.addElem("buildVersion", getBuildVersion());
    objJson.beginObject("ethernet");
    settings.Ethernet.toJSON(objJson);
    objJson.endObject();
    objJson.beginObject("wifi");
    settings.WIFI.toJSON(objJson);
    objJson.endObject();
    objJson.beginObject("ip");
    settings.IP.toJSON(objJson);
    objJson.endObject();
}
void WebSettings::handleConnectMQTT(WebServer &server)
{
    WebJsonResponder json(server);
    JsonObject obj;
    if (!json.parseBody(obj)) return;
    HTTPMethod method = server.method();
    ESP_LOGI(s_TAG, "Saving MQTT HTTP Method: %d", server.method());
    if (method == HTTP_POST || method == HTTP_PUT) {
        mqtt.disconnect();
        settings.MQTT.fromJSON(obj);
        settings.MQTT.save();
        auto objJson = json.respondJson().object();
        settings.MQTT.toJSON(objJson);
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebSettings::handleMQTTSettings(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    settings.MQTT.toJSON(objJson);
}
