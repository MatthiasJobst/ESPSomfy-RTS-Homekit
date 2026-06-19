// WebHomeKit.cpp — Web handler implementations for HomeKit.
//
// Exposes the HomeKit accessory state and pairing-reset endpoints:
//   - Reading the current accessory/bridge state (handleHomeKit)
//   - Clearing all HomeKit pairings (handleHomeKitResetPairings)

#include "WebHomeKit.h"

#include <WebServer.h>
#include "Web.h"
#include "WebJsonResponder.h"
#include "HomeKit.h"
#include "ConfigSettings.h"

extern HomeKitClass homekit;
extern ConfigSettings settings;

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
    WebJsonResponder json(server);
    if (server.method() == HTTP_GET) {
        auto objJson = json.respondJson().object();
        homekit.toJSON(objJson);
        objJson.addElem("forcedMoveRepeats", settings.forcedMoveRepeats);
    } else
        json.respondJson().notFound();
}

void WebHomeKit::handleHomeKitResetPairings(WebServer &server)
{
    WebJsonResponder json(server);
    if (server.method() == HTTP_POST) {
        homekit.resetPairings();
        // Bare {"status":"OK"} (no desc) — not the facade's status shape.
        server.send(200, ENCODING_JSON, F("{\"status\":\"OK\"}"));
    } else
        json.respondJson().notFound();
}
