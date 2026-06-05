// WebHomeKit.cpp — Web handler implementations for HomeKit.
//
// Exposes the HomeKit accessory state and pairing-reset endpoints:
//   - Reading the current accessory/bridge state (handleHomeKit)
//   - Clearing all HomeKit pairings (handleHomeKitResetPairings)

#include "WebHomeKit.h"

#include <WebServer.h>
#include "WResp.h"
#include "Web.h"
#include "HomeKit.h"

extern HomeKitClass homekit;

void WebHomeKit::begin()
{
    // REST API routes
    registerApiHandler("/homekit", [this]() { handleHomeKit(apiServer); });
    registerApiHandler("/homekit/resetPairings", [this]() { handleHomeKitResetPairings(apiServer); });
    // Web UI routes
    registerHandler("/homekit", [this]() { handleHomeKit(server); });
    registerHandler("/homekit/resetPairings", [this]() { handleHomeKitResetPairings(server); });
}

void WebHomeKit::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebHomeKit::handleHomeKit(WebServer &server)
{
    if (server.method() == HTTP_GET) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        homekit.toJSON(resp);
        resp.endObject();
        resp.endResponse();
    } else
        server.send(404, ENCODING_TEXT, RESPONSE_404);
}

void WebHomeKit::handleHomeKitResetPairings(WebServer &server)
{
    if (server.method() == HTTP_POST) {
        homekit.resetPairings();
        server.send(200, ENCODING_JSON, F("{\"status\":\"OK\"}"));
    } else
        server.send(404, ENCODING_TEXT, RESPONSE_404);
}
