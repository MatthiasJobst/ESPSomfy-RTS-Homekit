#pragma once
// WebHelpers.h — Inline helpers shared by all Web translation units.
//
// parseBody: parses the request body (server.arg("plain")) into a JsonObject.
//   Returns false and sends a 500 error response if deserialization fails.

#include <WebServer.h>
#include <ArduinoJson.h>
#include "Web.h"

extern Web webServer;

inline bool parseBody(WebServer &server, JsonDocument &doc, JsonObject &obj) {
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) { webServer.handleDeserializationError(server, err); return false; }
    obj = doc.as<JsonObject>();
    return true;
}
