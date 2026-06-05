#pragma once
// WebHelpers.h — Inline helpers shared by all Web translation units.

#include <WebServer.h>
#include <ArduinoJson.h>
#include "Web.h"

/**
 * @brief Send a 500 JSON error response describing a failed deserialization.
 *
 * Stateless — operates only on the request server.
 *
 * @param server The request server to send the response on.
 * @param err The ArduinoJson deserialization error to describe.
 */
inline void sendDeserializationError(WebServer &server, DeserializationError &err)
{
    switch (err.code()) {
    case DeserializationError::InvalidInput:
        server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
        break;
    case DeserializationError::NoMemory:
        server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
        break;
    default:
        server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
        break;
    }
}

/**
 * @brief Parse the request body (server.arg("plain")) into a JsonObject.
 *
 * Sends a 500 error response via sendDeserializationError() on failure.
 *
 * @param server The request server providing the body and error response.
 * @param doc The JsonDocument to deserialize into.
 * @param obj Set to the parsed JsonObject on success.
 * @return true on success, false if deserialization fails.
 */
inline bool parseBody(WebServer &server, JsonDocument &doc, JsonObject &obj)
{
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        sendDeserializationError(server, err);
        return false;
    }
    obj = doc.as<JsonObject>();
    return true;
}
