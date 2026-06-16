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
#include "Web.h"
#include "WebJsonResponder.h"

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
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    settings.printAvailHeap();
    if (method == HTTP_POST || method == HTTP_GET) {
        auto objJson = json.respondJson().object();
        objJson.addElem("maxRooms", (uint8_t)SOMFY_MAX_ROOMS);
        objJson.addElem("maxShades", (uint8_t)SOMFY_MAX_SHADES);
        objJson.addElem("maxGroups", (uint8_t)SOMFY_MAX_GROUPS);
        objJson.addElem("maxGroupedShades", (uint8_t)SOMFY_MAX_GROUPED_SHADES);
        objJson.addElem("maxLinkedRemotes", (uint8_t)SOMFY_MAX_LINKED_REMOTES);
        objJson.addElem("startingAddress", (uint32_t)somfy.startingAddress);
        objJson.beginObject("transceiver");
        somfy.transceiver.toJSON(objJson);
        objJson.endObject();
        objJson.beginObject("version");
        git.toJSON(objJson);
        objJson.endObject();
        objJson.beginArray("rooms");
        somfy.roomController.toJSONRooms(objJson);
        objJson.endArray();
        objJson.beginArray("shades");
        somfy.toJSONShades(objJson);
        objJson.endArray();
        objJson.beginArray("groups");
        somfy.groupController.toJSONGroups(objJson);
        objJson.endArray();
        objJson.beginArray("repeaters");
        somfy.repeaterController.toJSONRepeaters(objJson);
        objJson.endArray();
    } else
        json.respondJson().notFound();
}

void WebSystem::handleDiscovery(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = apiServer.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        ESP_LOGI(s_TAG, "Discovery Requested");
        char connType[10] = "Unknown";
        if (net.connType == conn_types_t::ethernet)
            strcpy(connType, "Ethernet");
        else if (net.connType == conn_types_t::wifi)
            strcpy(connType, "Wifi");

        {
            auto objJson = json.respondJson().object();
            objJson.addElem("serverId", settings.serverId);
            objJson.addElem("version", settings.fwVersion.name);
            objJson.addElem("latest", git.latest.name);
            objJson.addElem("model", "ESPSomfyRTS");
            objJson.addElem("hostname", settings.hostname);
            objJson.addElem("authType", static_cast<uint8_t>(settings.Security.type));
            objJson.addElem("permissions", settings.Security.permissions);
            objJson.addElem("chipModel", settings.chipModel);
            objJson.addElem("connType", connType);
            objJson.addElem("checkForUpdate", settings.checkForUpdate);
            objJson.beginObject("memory");
            objJson.addElem("max", ESP.getMaxAllocHeap());
            objJson.addElem("free", ESP.getFreeHeap());
            objJson.addElem("min", ESP.getMinFreeHeap());
            objJson.addElem("total", ESP.getHeapSize());
            objJson.endObject();
            objJson.beginArray("rooms");
            somfy.roomController.toJSONRooms(objJson);
            objJson.endArray();
            objJson.beginArray("shades");
            somfy.toJSONShades(objJson);
            objJson.endArray();
            objJson.beginArray("groups");
            somfy.groupController.toJSONGroups(objJson);
            objJson.endArray();
        }
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
    somfy.store.writeBackup();
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
    server.send(404, ENCODING_TEXT, "404 Service Not Found: " + server.uri());
}

void WebSystem::handleReboot(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Rebooting ESP...");
        rebootDelay.requestReboot(500);
        json.respondJson().ok("Successfully started reboot");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}
