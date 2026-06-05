// WebSystem.cpp — Web handler implementations for system-level endpoints.
//
// Cross-cutting endpoints not owned by a single device domain:
//   - Aggregate controller snapshot (handleController)
//   - Discovery payload (handleDiscovery)
//   - Not-found fallback (handleNotFound)
//   - Backup download (handleBackup)
//   - Reboot (handleReboot)

#include "WebSystem.h"

#include <esp_log.h>
#include <LittleFS.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "GitOTA.h"
#include "ControllerNetwork.h"
#include "WResp.h"
#include "Web.h"

extern ConfigSettings settings;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern GitUpdater git;
extern ControllerNetwork net;

static const char *s_TAG = "WebSystem";

void WebSystem::begin()
{
    // REST API routes
    registerApiHandler("/discovery", [this]() { handleDiscovery(apiServer); });
    registerApiHandler("/controller", [this]() { handleController(apiServer); });
    registerApiHandler("/backup", [this]() { handleBackup(apiServer); });
    registerApiHandler("/reboot", [this]() { handleReboot(apiServer); });
    // Web UI routes
    registerHandler("/controller", [this]() { handleController(server); });
    registerHandler("/backup", [this]() { handleBackup(server, true); });
    registerHandler("/reboot", [this]() { handleReboot(server); });
    // Not-found fallback is a one-time server setting, not a route.
    apiServer.onNotFound([this]() { handleNotFound(apiServer); });
    server.onNotFound([this]() { handleNotFound(server); });
}

void WebSystem::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebSystem::handleController(WebServer &server)
{
    HTTPMethod method = server.method();
    settings.printAvailHeap();
    if (method == HTTP_POST || method == HTTP_GET) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        resp.addElem("maxRooms", (uint8_t)SOMFY_MAX_ROOMS);
        resp.addElem("maxShades", (uint8_t)SOMFY_MAX_SHADES);
        resp.addElem("maxGroups", (uint8_t)SOMFY_MAX_GROUPS);
        resp.addElem("maxGroupedShades", (uint8_t)SOMFY_MAX_GROUPED_SHADES);
        resp.addElem("maxLinkedRemotes", (uint8_t)SOMFY_MAX_LINKED_REMOTES);
        resp.addElem("startingAddress", (uint32_t)somfy.startingAddress);
        resp.beginObject("transceiver");
        somfy.transceiver.toJSON(resp);
        resp.endObject();
        resp.beginObject("version");
        git.toJSON(resp);
        resp.endObject();
        resp.beginArray("rooms");
        somfy.toJSONRooms(resp);
        resp.endArray();
        resp.beginArray("shades");
        somfy.toJSONShades(resp);
        resp.endArray();
        resp.beginArray("groups");
        somfy.toJSONGroups(resp);
        resp.endArray();
        resp.beginArray("repeaters");
        somfy.toJSONRepeaters(resp);
        resp.endArray();
        resp.endObject();
        resp.endResponse();
    } else
        server.send(404, ENCODING_TEXT, RESPONSE_404);
}

void WebSystem::handleDiscovery(WebServer &server)
{
    HTTPMethod method = apiServer.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        ESP_LOGI(s_TAG, "Discovery Requested");
        char connType[10] = "Unknown";
        if (net.connType == conn_types_t::ethernet)
            strcpy(connType, "Ethernet");
        else if (net.connType == conn_types_t::wifi)
            strcpy(connType, "Wifi");

        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        resp.addElem("serverId", settings.serverId);
        resp.addElem("version", settings.fwVersion.name);
        resp.addElem("latest", git.latest.name);
        resp.addElem("model", "ESPSomfyRTS");
        resp.addElem("hostname", settings.hostname);
        resp.addElem("authType", static_cast<uint8_t>(settings.Security.type));
        resp.addElem("permissions", settings.Security.permissions);
        resp.addElem("chipModel", settings.chipModel);
        resp.addElem("connType", connType);
        resp.addElem("checkForUpdate", settings.checkForUpdate);
        resp.beginObject("memory");
        resp.addElem("max", ESP.getMaxAllocHeap());
        resp.addElem("free", ESP.getFreeHeap());
        resp.addElem("min", ESP.getMinFreeHeap());
        resp.addElem("total", ESP.getHeapSize());
        resp.endObject();
        resp.beginArray("rooms");
        somfy.toJSONRooms(resp);
        resp.endArray();
        resp.beginArray("shades");
        somfy.toJSONShades(resp);
        resp.endArray();
        resp.beginArray("groups");
        somfy.toJSONGroups(resp);
        resp.endArray();
        resp.endObject();
        resp.endResponse();
        net.emitSockets();
    } else
        server.send(500, ENCODING_TEXT, "Invalid http method");
}

void WebSystem::handleBackup(WebServer &server, bool attach)
{
    if (server.hasArg("attach")) attach = toBoolean(server.arg("attach").c_str(), attach);
    if (attach) {
        char filename[120];
        Timestamp ts;
        char *iso = ts.getISOTime();
        // Replace the invalid characters as quickly as we can.
        for (size_t i = 0; i < strlen(iso); i++) {
            switch (iso[i]) {
            case '.':
                // Just trim off the ms.
                iso[i] = '\0';
                break;
            case ':':
                iso[i] = '_';
                break;
            default:
                break;
            }
        }
        snprintf(filename, sizeof(filename), "attachment; filename=\"ESPSomfyRTS %s.backup\"", iso);
        ESP_LOGI(s_TAG, "%s", filename);
        server.sendHeader(F("Content-Disposition"), filename);
        server.sendHeader(F("Access-Control-Expose-Headers"), F("Content-Disposition"));
    }
    ESP_LOGI(s_TAG, "Saving current shade information");
    somfy.writeBackup();
    File file = LittleFS.open("/controller.backup", "r");
    if (!file) {
        ESP_LOGE(s_TAG, "Error opening shades.cfg");
        server.send(500, ENCODING_TEXT, "shades.cfg");
        return;
    }
    server.streamFile(file, ENCODING_TEXT);
    file.close();
}

void WebSystem::handleNotFound(WebServer &server)
{
    HTTPMethod method = server.method();
    ESP_LOGI(s_TAG, "Request %s 404-%d ", server.uri().c_str(), method);
    switch (method) {
    case HTTP_POST:
        ESP_LOGI(s_TAG, "POST ");
        break;
    case HTTP_GET:
        ESP_LOGI(s_TAG, "GET ");
        break;
    case HTTP_PUT:
        ESP_LOGI(s_TAG, "PUT ");
        break;
    case HTTP_OPTIONS:
        ESP_LOGI(s_TAG, "OPTIONS ");
        server.send(200, "OK");
        return;
    default:
        ESP_LOGI(s_TAG, "[%d]", method);
        break;
    }
    snprintf(content, sizeof(content), "404 Service Not Found: %s", server.uri().c_str());
    server.send(404, ENCODING_TEXT, content);
}

void WebSystem::handleReboot(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Rebooting ESP...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 500;
        server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
    } else {
        server.send(201, ENCODING_JSON, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
}
