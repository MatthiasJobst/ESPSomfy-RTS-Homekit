// WebAuth.cpp — Web handler implementations for authentication and security.
//
// Login, login-context and security settings, plus the API-token (HMAC-SHA256
// keyed by serverId) helpers shared by login and save-security:
//   - Login / token issue (handleLogin)
//   - Identity/security context (handleLoginContext)
//   - Security settings read/write (handleGetSecurity, handleSaveSecurity)

#include "WebAuth.h"

#include <cstring>
#include <esp_log.h>
#include <mbedtls/md.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "ConfigSettings.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

extern ConfigSettings settings;

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

bool WebAuth::createAPIPinToken(const IPAddress &ipAddress, const char *pin, char *token)
{
    return this->createAPIToken((String(pin) + ":" + ipAddress.toString()).c_str(), token);
}
bool WebAuth::createAPIPasswordToken(const IPAddress &ipAddress, const char *username, const char *password, char *token)
{
    return this->createAPIToken((String(username) + ":" + String(password) + ":" + ipAddress.toString()).c_str(),
                                token);
}
bool WebAuth::createAPIToken(const char *payload, char *token)
{
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)settings.serverId, strlen(settings.serverId));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, strlen(payload));
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    ESP_LOGI(s_TAG, "Hash: ");
    token[0] = '\0';
    for (int i = 0; i < sizeof(hmacResult); i++) {
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strlcat(token, str, sizeof(token));
    }
    ESP_LOGI(s_TAG, "Token: %s", token);
    return true;
}
bool WebAuth::createAPIToken(const IPAddress &ipAddress, char *token)
{
    String payload;
    if (settings.Security.type == security_types::Password)
        createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    else if (settings.Security.type == security_types::PinEntry)
        createAPIPinToken(ipAddress, settings.Security.pin, token);
    else
        createAPIToken(ipAddress.toString().c_str(), token);
    return true;
}
void WebAuth::handleLogin(WebServer &server)
{
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    char token[65];
    memset(&token, 0x00, sizeof(token));
    this->createAPIToken(server.client().remoteIP(), token);
    obj["type"] = static_cast<uint8_t>(settings.Security.type);
    if (settings.Security.type == security_types::None) {
        obj["apiKey"] = token;
        obj["msg"] = "Success";
        obj["success"] = true;
        serializeJson(doc, content);
        server.send(200, ENCODING_JSON, content);
        return;
    }
    ESP_LOGI(s_TAG, "Web logging in...");
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    memset(username, 0x00, sizeof(username));
    memset(password, 0x00, sizeof(password));
    memset(pin, 0x00, sizeof(pin));
    if (server.hasArg("plain")) {
        JsonDocument doc;
        JsonObject obj;
        if (!parseBody(server, doc, obj)) return;
        if (obj.containsKey("username") && obj["username"]) strlcpy(username, obj["username"], sizeof(username));
        if (obj.containsKey("password") && obj["password"]) strlcpy(password, obj["password"], sizeof(password));
        if (obj.containsKey("pin") && obj["pin"]) strlcpy(pin, obj["pin"], sizeof(pin));
    } else {
        if (server.hasArg("username")) strlcpy(username, server.arg("username").c_str(), sizeof(username));
        if (server.hasArg("password")) strlcpy(password, server.arg("password").c_str(), sizeof(password));
        if (server.hasArg("pin")) strlcpy(pin, server.arg("pin").c_str(), sizeof(pin));
    }
    // At this point we should have all the data we need to login.
    if (settings.Security.type == security_types::PinEntry) {
        ESP_LOGI(s_TAG, "Validating pin %s", pin);
        if (strlen(pin) == 0 || strcmp(pin, settings.Security.pin) != 0) {
            obj["success"] = false;
            obj["msg"] = "Invalid Pin Entry";
        } else {
            obj["success"] = true;
            obj["msg"] = "Login successful";
            obj["apiKey"] = token;
        }
    } else if (settings.Security.type == security_types::Password) {
        ESP_LOGI(s_TAG, "Validating username %s and password %s", username, password);
        if (strlen(username) == 0 || strlen(password) == 0 || strcmp(username, settings.Security.username) != 0 ||
            strcmp(password, settings.Security.password) != 0) {
            obj["success"] = false;
            obj["msg"] = "Invalid username or password";
        } else {
            obj["success"] = true;
            obj["msg"] = "Login successful";
            obj["apiKey"] = token;
        }
    }
    serializeJson(doc, content);
    server.send(200, ENCODING_JSON, content);
    return;
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
            char token[65];
            createAPIToken(server.client().remoteIP(), token);
            obj["apiKey"] = token;
            JsonDocument sdoc;
            JsonObject sobj = sdoc.to<JsonObject>();
            settings.Security.toJSON(sobj);
            serializeJson(sdoc, content);
            server.send(200, ENCODING_JSON, content);
        } else {
            server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    }
}
void WebAuth::handleGetSecurity(WebServer &server)
{
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    settings.Security.toJSON(obj);
    serializeJson(doc, content);
    server.send(200, ENCODING_JSON, content);
}
void WebAuth::handleLoginContext(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    resp.addElem("type", static_cast<uint8_t>(settings.Security.type));
    resp.addElem("permissions", settings.Security.permissions);
    resp.addElem("serverId", settings.serverId);
    resp.addElem("version", settings.fwVersion.name);
    resp.addElem("model", "ESPSomfyRTS");
    resp.addElem("hostname", settings.hostname);
    resp.endObject();
    resp.endResponse();
}
