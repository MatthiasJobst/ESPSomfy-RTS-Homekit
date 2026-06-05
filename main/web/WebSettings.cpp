// WebSettings.cpp — Web handler implementations for device configuration.
//
// Exposes the HTTP API surface for reading and writing all persistent settings:
//   - Radio / transceiver configuration (handleGetRadio, handleSaveRadio)
//   - General device settings (handleSetGeneral)
//   - Network settings and Wi-Fi connection (handleSetNetwork, handleSetIP, handleConnectWifi, handleNetworkSettings)
//   - MQTT broker configuration and connection (handleMQTTSettings, handleConnectMQTT)
//   - Module-level settings (handleModuleSettings)
//   - OTA release listing and firmware download queuing (handleGetReleases, handleCancelFirmware)

#include "WebSettings.h"

#include <esp_log.h>
#include <WiFi.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "ControllerNetwork.h"

extern ConfigSettings settings;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern MQTTClass mqtt;
extern GitUpdater git;
extern ControllerNetwork net;

static const char *s_TAG = "WebSettings";

void WebSettings::begin()
{
    registerHandler("/getReleases", [this]() { handleGetReleases(server); });
    registerHandler("/cancelFirmware", [this]() { handleCancelFirmware(server); });
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
void WebSettings::handleGetReleases(WebServer &server)
{
    GitRepo repo;
    repo.getReleases();
    git.setCurrentRelease(repo);
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    repo.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}
void WebSettings::handleCancelFirmware(WebServer &server)
{
    // If we are currently downloading the filesystem we cannot cancel.
    if (!git.lockFS) {
        git.status = GIT_UPDATE_CANCELLING;
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        git.toJSON(resp);
        resp.endObject();
        resp.endResponse();
        git.cancelled = true;
    } else {
        server.send(500, ENCODING_JSON,
                    F("{\"status\":\"ERROR\",\"desc\":\"Cannot cancel during filesystem update.\"}"));
    }
}
void WebSettings::handleSaveRadio(WebServer &server)
{
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
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            somfy.transceiver.toJSON(resp);
            resp.endObject();
            resp.endResponse();
        } else {
            server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    }
}
void WebSettings::handleGetRadio(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    somfy.transceiver.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}
void WebSettings::handleSetGeneral(WebServer &server)
{
    ESP_LOGI(s_TAG, "Plain: %d %s", server.method(), server.arg("plain").c_str());
    JsonDocument doc;
    JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        if (obj.containsKey("hostname") || obj.containsKey("checkForUpdate")) {
            bool checkForUpdate = settings.checkForUpdate;
            settings.fromJSON(obj);
            settings.save();
            if (settings.checkForUpdate != checkForUpdate) git.emitUpdateCheck();
            if (obj.containsKey("hostname")) net.updateHostname();
        }
        if (obj.containsKey("ntpServer") || obj.containsKey("ntpServer")) {
            settings.NTP.fromJSON(obj);
            settings.NTP.save();
        }
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
    } else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
}
void WebSettings::handleSetNetwork(WebServer &server)
{
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
            bool reboot = false;
            if (obj.containsKey("connType") &&
                obj["connType"].as<uint8_t>() != static_cast<uint8_t>(settings.connType)) {
                settings.connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
                settings.save();
                reboot = true;
            }
            if (obj.containsKey("wifi")) {
                JsonObject objWifi = obj["wifi"];
                if (settings.connType == conn_types_t::wifi) {
                    if (objWifi.containsKey("ssid") &&
                        objWifi["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) {
                        if (WiFi.softAPgetStationNum() == 0) reboot = true;
                    }
                    if (objWifi.containsKey("passphrase") &&
                        objWifi["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) {
                        if (WiFi.softAPgetStationNum() == 0) reboot = true;
                    }
                }
                settings.WIFI.fromJSON(objWifi);
                settings.WIFI.save();
            }
            if (obj.containsKey("ethernet")) {
                JsonObject objEth = obj["ethernet"];
                if (settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
                    reboot = true;
                settings.Ethernet.fromJSON(objEth);
                settings.Ethernet.save();
            }
            if (reboot) {
                ESP_LOGI(s_TAG, "Rebooting ESP for new Network settings...");
                rebootDelay.reboot = true;
                rebootDelay.rebootTime = millis() + 1000;
            }
            server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
        } else {
            server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    }
}
void WebSettings::handleSetIP(WebServer &server)
{
    ESP_LOGI(s_TAG, "Setting IP...");
    JsonDocument doc;
    JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        settings.IP.fromJSON(obj);
        settings.IP.save();
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    } else {
        server.send(201, ENCODING_JSON, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
}
void WebSettings::handleConnectWifi(WebServer &server)
{
    ESP_LOGI(s_TAG, "Settings WIFI connection...");
    JsonDocument doc;
    JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        String ssid = "";
        String passphrase = "";
        if (obj.containsKey("ssid")) ssid = obj["ssid"].as<String>();
        if (obj.containsKey("passphrase")) passphrase = obj["passphrase"].as<String>();
        bool reboot;
        if (ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
        if (passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
        if (!settings.WIFI.ssidExists(ssid.c_str()) && ssid.length() > 0) {
            server.send(400, ENCODING_JSON, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
        } else {
            SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
            SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
            settings.WIFI.save();
            settings.WIFI.print();
            server.send(201, ENCODING_JSON, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
            if (reboot) {
                ESP_LOGI(s_TAG, "Rebooting ESP for new WiFi settings...");
                rebootDelay.reboot = true;
                rebootDelay.rebootTime = millis() + 1000;
            }
        }
    } else {
        server.send(201, ENCODING_JSON, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
}
void WebSettings::handleModuleSettings(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    resp.addElem("fwVersion", settings.fwVersion.name);
    resp.addElem("buildVersion", getBuildVersion());
    resp.addElem("gitRepo", GIT_REPO);
    settings.toJSON(resp);
    settings.NTP.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}
void WebSettings::handleNetworkSettings(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    settings.toJSON(resp);
    resp.addElem("fwVersion", settings.fwVersion.name);
    resp.addElem("buildVersion", getBuildVersion());
    resp.beginObject("ethernet");
    settings.Ethernet.toJSON(resp);
    resp.endObject();
    resp.beginObject("wifi");
    settings.WIFI.toJSON(resp);
    resp.endObject();
    resp.beginObject("ip");
    settings.IP.toJSON(resp);
    resp.endObject();
    resp.endObject();
    resp.endResponse();
}
void WebSettings::handleConnectMQTT(WebServer &server)
{
    JsonDocument doc;
    JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    HTTPMethod method = server.method();
    ESP_LOGI(s_TAG, "Saving MQTT HTTP Method: %d", server.method());
    if (method == HTTP_POST || method == HTTP_PUT) {
        mqtt.disconnect();
        settings.MQTT.fromJSON(obj);
        settings.MQTT.save();
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        settings.MQTT.toJSON(resp);
        resp.endObject();
        resp.endResponse();
    } else {
        server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
}
void WebSettings::handleMQTTSettings(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    settings.MQTT.toJSON(resp);
    resp.endObject();
    resp.endResponse();
}
