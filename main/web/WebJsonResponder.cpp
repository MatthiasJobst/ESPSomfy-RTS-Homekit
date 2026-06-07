// WebJsonResponder.cpp — Inbound parsing + deserialization error handling.
//
// The outbound side is delegated inline to WebResponder (see the header); this
// translation unit implements the request-body parsing and the shared
// deserialization-error response.

#include "WebJsonResponder.h"

bool WebJsonResponder::parseBody(JsonObject &obj)
{
    return parseArg("plain", obj);
}

bool WebJsonResponder::parseArg(const char *argName, JsonObject &obj)
{
    DeserializationError err = deserializeJson(_doc, _server.arg(argName));
    if (err) {
        sendDeserializationError(err);
        return false;
    }
    obj = _doc.as<JsonObject>();
    return true;
}

bool WebJsonResponder::parseBody(JsonArray &arr)
{
    DeserializationError err = deserializeJson(_doc, _server.arg("plain"));
    if (err) {
        sendDeserializationError(err);
        return false;
    }
    arr = _doc.as<JsonArray>();
    return true;
}

void WebJsonResponder::sendDeserializationError(DeserializationError &err)
{
    switch (err.code()) {
    case DeserializationError::InvalidInput:
        _server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
        break;
    case DeserializationError::NoMemory:
        _server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
        break;
    default:
        _server.send(500, "application/json",
                     F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
        break;
    }
}
