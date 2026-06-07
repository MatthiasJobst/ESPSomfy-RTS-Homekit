// WebAuth.cpp — Web handler implementations for authentication and security.
//
// Transport only: parse the request, delegate token/validation to AuthService,
// serialize the response.
//   - Login / token issue (handleLogin)
//   - Identity/security context (handleLoginContext)
//   - Security settings read/write (handleGetSecurity, handleSaveSecurity)

#include "WebAuth.h"

#include <cstring>
#include <esp_log.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "AuthService.h"
#include "ConfigSettings.h"
#include "Web.h"
#include "WebJsonResponder.h"

extern ConfigSettings settings;
extern AuthService auth;

static const char *s_TAG = "WebAuth";

void WebAuth::begin()
{
    // REST API routes
    registerApiHandler("/login", [this]() { handleLogin(apiServer); });
    // Web UI routes
    registerHandler("/login", [this]() { handleLogin(server); });
    registerHandler("/loginContext", [this]() { handleLoginContext(server); });
    registerHandler("/saveSecurity", [this]() { handleSaveSecurity(server); });
    registerHandler("/getSecurity", [this]() { handleGetSecurity(server); });
}

void WebAuth::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebAuth::handleLogin(WebServer &server)
{
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    if (server.hasArg("plain")) {
        WebJsonResponder json(server);
        JsonObject body;
        if (!json.parseBody(body)) return;
        if (body.containsKey("username") && body["username"]) strlcpy(username, body["username"], sizeof(username));
        if (body.containsKey("password") && body["password"]) strlcpy(password, body["password"], sizeof(password));
        if (body.containsKey("pin") && body["pin"]) strlcpy(pin, body["pin"], sizeof(pin));
    } else {
        if (server.hasArg("username")) strlcpy(username, server.arg("username").c_str(), sizeof(username));
        if (server.hasArg("password")) strlcpy(password, server.arg("password").c_str(), sizeof(password));
        if (server.hasArg("pin")) strlcpy(pin, server.arg("pin").c_str(), sizeof(pin));
    }
    AuthService::LoginResult res = auth.login(server.client().remoteIP(), username, password, pin);
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = static_cast<uint8_t>(settings.Security.type);
    obj["success"] = res.success;
    obj["msg"] = res.msg;
    if (res.apiKey.length()) obj["apiKey"] = res.apiKey;
    String out;
    serializeJson(doc, out);
    server.send(200, ENCODING_JSON, out);
}
void WebAuth::handleSaveSecurity(WebServer &server)
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
            settings.Security.fromJSON(obj);
            settings.Security.save();
            obj["apiKey"] = auth.tokenForClient(server.client().remoteIP());
            JsonDocument sdoc;
            JsonObject sobj = sdoc.to<JsonObject>();
            settings.Security.toJSON(sobj);
            String out;
            serializeJson(sdoc, out);
            server.send(200, ENCODING_JSON, out);
        } else {
            server.send(403, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}"));
        }
    }
}
void WebAuth::handleGetSecurity(WebServer &server)
{
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    String out;
    serializeJson(doc, out);
    server.send(200, ENCODING_JSON, out);
}
void WebAuth::handleLoginContext(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    objJson.addElem("type", static_cast<uint8_t>(settings.Security.type));
    objJson.addElem("permissions", settings.Security.permissions);
    objJson.addElem("serverId", settings.serverId);
    objJson.addElem("version", settings.fwVersion.name);
    objJson.addElem("model", "ESPSomfyRTS");
    objJson.addElem("hostname", settings.hostname);
}
