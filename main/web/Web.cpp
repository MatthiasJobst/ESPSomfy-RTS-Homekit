#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "mbedtls/md.h"
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SSDP.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "ControllerNetwork.h"
#include "HomeKit.h"
#include "Sockets.h"

extern ConfigSettings settings;
extern SSDPClass SSDP;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
extern GitUpdater git;
extern ControllerNetwork net;
extern SocketEmitter sockEmit;

//#define WEB_MAX_RESPONSE 34768
#define WEB_MAX_RESPONSE 4096
char g_content[WEB_MAX_RESPONSE];

static const char *TAG = "Web";

// General responses
extern const char _response_404[] = "404: Service Not Found";


// Encodings
extern const char _encoding_text[] = "text/plain";
extern const char _encoding_html[] = "text/html";
extern const char _encoding_json[] = "application/json";

WebServer apiServer(APP_API_PORT);
WebServer server(APP_HTTP_PORT);
void Web::startup() {
  ESP_LOGI(TAG, "Launching web server...");
}
void Web::loop() {
  server.handleClient();
  delay(1);
  apiServer.handleClient();
  delay(1);
}
void Web::sendCacheHeaders(uint32_t seconds) {
  server.sendHeader(F("Cache-Control"), F("public, max-age=604800, immutable"));
}
void Web::end() {
  //server.end();
}
void Web::handleDeserializationError(WebServer &server, DeserializationError &err) {
    switch (err.code()) {
    case DeserializationError::InvalidInput:
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid JSON payload\"}"));
      break;
    case DeserializationError::NoMemory:
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Out of memory parsing JSON\"}"));
      break;
    default:
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"General JSON Deserialization failed\"}"));
      break;
    }
}
bool Web::isAuthenticated(WebServer &server, bool cfg) {
  ESP_LOGI(TAG, "Checking authentication");
  if(settings.Security.type == security_types::None) return true;
  else if(!cfg && (settings.Security.permissions & static_cast<uint8_t>(security_permissions::ConfigOnly)) == 0x01) return true;
  else if(server.hasHeader("apikey")) {
    // Api key was supplied.
    ESP_LOGI(TAG, "Checking API Key...");
    char token[65];
    memset(token, 0x00, sizeof(token));
    this->createAPIToken(server.client().remoteIP(), token);
    // Compare the tokens.
    if(String(token) != server.header("apikey")) return false;
    server.sendHeader("apikey", token);
  }
  else {
    // Send a 401
    ESP_LOGI(TAG, "Not authenticated...");
    server.send(401, "Unauthorized API Key");
    return false;
  }
  return true;
}
bool Web::createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token) {
  return this->createAPIToken((String(pin) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token) {
  return this->createAPIToken((String(username) + ":" + String(password) + ":" + ipAddress.toString()).c_str(), token);
}
bool Web::createAPIToken(const char *payload, char *token) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)settings.serverId, strlen(settings.serverId));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload, strlen(payload)); 
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    ESP_LOGI(TAG, "Hash: ");
    token[0] = '\0';
    for(int i = 0; i < sizeof(hmacResult); i++){
        char str[3];
        sprintf(str, "%02x", (int)hmacResult[i]);
        strcat(token, str);
    }
    ESP_LOGI(TAG, "Token: %s", token);
    return true;
}
bool Web::createAPIToken(const IPAddress ipAddress, char *token) {
    String payload;
    if(settings.Security.type == security_types::Password) createAPIPasswordToken(ipAddress, settings.Security.username, settings.Security.password, token);
    else if(settings.Security.type == security_types::PinEntry) createAPIPinToken(ipAddress, settings.Security.pin, token);
    else createAPIToken(ipAddress.toString().c_str(), token);
    return true;
}
void Web::handleLogout(WebServer &server) {
  ESP_LOGI(TAG, "Logging out of webserver");
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Set-Cookie", "ESPSOMFYID=0");
  server.send(301);
}
void Web::handleLogin(WebServer &server) {
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    char token[65];
    memset(&token, 0x00, sizeof(token));
    this->createAPIToken(server.client().remoteIP(), token);
    obj["type"] = static_cast<uint8_t>(settings.Security.type);
    if(settings.Security.type == security_types::None) {
      obj["apiKey"] = token;
      obj["msg"] = "Success";
      obj["success"] = true;
      serializeJson(doc, g_content);
      server.send(200, _encoding_json, g_content);
      return;
    }
    ESP_LOGI(TAG, "Web logging in...");
    char username[33] = "";
    char password[33] = "";
    char pin[5] = "";
    memset(username, 0x00, sizeof(username));
    memset(password, 0x00, sizeof(password));
    memset(pin, 0x00, sizeof(pin));
    if(server.hasArg("plain")) {
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if(obj.containsKey("username") && obj["username"]) strlcpy(username, obj["username"], sizeof(username));
      if(obj.containsKey("password") && obj["password"]) strlcpy(password, obj["password"], sizeof(password));
      if(obj.containsKey("pin") && obj["pin"]) strlcpy(pin, obj["pin"], sizeof(pin));
    }
    else {
      if(server.hasArg("username")) strlcpy(username, server.arg("username").c_str(), sizeof(username));
      if(server.hasArg("password")) strlcpy(password, server.arg("password").c_str(), sizeof(password));
      if(server.hasArg("pin")) strlcpy(pin, server.arg("pin").c_str(), sizeof(pin));
    }
    // At this point we should have all the data we need to login.
    if(settings.Security.type == security_types::PinEntry) {
      ESP_LOGI(TAG, "Validating pin %s", pin);
      if(strlen(pin) == 0 || strcmp(pin, settings.Security.pin) != 0) {
        obj["success"] = false;
        obj["msg"] = "Invalid Pin Entry";
      }
      else {
        obj["success"] = true;
        obj["msg"] = "Login successful";
        obj["apiKey"] = token;
      }
    }
    else if(settings.Security.type == security_types::Password) {
      ESP_LOGI(TAG, "Validating username %s and password %s", username, password);
      if(strlen(username) == 0 || strlen(password) == 0 || strcmp(username, settings.Security.username) != 0 || strcmp(password, settings.Security.password) != 0) {
        obj["success"] = false;
        obj["msg"] = "Invalid username or password";
      }
      else {
        obj["success"] = true;
        obj["msg"] = "Login successful";
        obj["apiKey"] = token;
      }
    }
    serializeJson(doc, g_content);
    server.send(200, _encoding_json, g_content);
    return;
}
void Web::handleStreamFile(WebServer &server, const char *filename, const char *encoding) {
  if(git.lockFS) {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
    return;
  }
  esp_task_wdt_reset();
  // Load the index html page from the data directory.
  ESP_LOGI(TAG, "Loading file %s", filename);
  File file = LittleFS.open(filename, "r");
  if (!file) {
    ESP_LOGE(TAG, "Error opening %s", filename);
    server.send(500, _encoding_text, "Error opening file");
    return;
  }
  // Stream the file in 1 KB chunks. Poll the WebSocket server every 4 chunks
  // (~4 KB) so a new WS connection upgrade request is never starved while a
  // large file (e.g. somfy.js ~128 KB) is being transferred.
  server.setContentLength(file.size());
  server.send(200, encoding, "");      // Send status + headers; body follows via sendContent
  static uint8_t streamBuf[1024];
  uint8_t chunkCount = 0;
  while (file.available() && server.client().connected()) {
    int n = file.read(streamBuf, sizeof(streamBuf));
    if (n > 0) server.sendContent((const char *)streamBuf, n);
    esp_task_wdt_reset();
    if (++chunkCount == 4) {
      chunkCount = 0;
      sockEmit.loop();
    }
  }
  file.close();
}
void Web::handleController(WebServer &server) {
  HTTPMethod method = server.method();
  settings.printAvailHeap();
  if (method == HTTP_POST || method == HTTP_GET) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
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
  }
  else server.send(404, _encoding_text, _response_404);
}
void Web::handleHomeKit(WebServer &server) {
  if(server.method() == HTTP_GET) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    homekit.toJSON(resp);
    resp.endObject();
    resp.endResponse();
  }
  else server.send(404, _encoding_text, _response_404);
}

void Web::handleHomeKitResetPairings(WebServer &server) {
  if(server.method() == HTTP_POST) {
    homekit.resetPairings();
    server.send(200, _encoding_json, F("{\"status\":\"OK\"}"));
  }
  else server.send(404, _encoding_text, _response_404);
}

void Web::handleLoginContext(WebServer &server) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
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
void Web::handleRepeatCommand(WebServer& server) {
  HTTPMethod method = server.method();
  if (method == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = 255;
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if(server.hasArg("shadeId")) shadeId = atoi(server.arg("shadeId").c_str());
    else if(server.hasArg("groupId")) groupId = atoi(server.arg("groupId").c_str());
    if(server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
    if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
    if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    if(shadeId == 255 && groupId == 255 && server.hasArg("plain")) {
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      if(obj.containsKey("groupId")) groupId = obj["groupId"];
      if(obj.containsKey("stepSize")) stepSize = obj["stepSize"];
      if (obj.containsKey("command")) {
          String scmd = obj["command"];
          command = translateSomfyCommand(scmd);
      }
      if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
    }
    //JsonDocument sdoc;
    //JsonObject sobj = sdoc.to<JsonObject>();
    if(shadeId != 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(!shade) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade reference could not be found.\"}"));
        return;        
      }
      if(shade->shadeType == shade_types::garage1 && command == somfy_commands::Prog) command = somfy_commands::Toggle;
      if(!shade->isLastCommand(command)) {
        // We are going to send this as a new command.
        shade->sendCommand(command, repeat >= 0 ? repeat : shade->repeats, stepSize);
      }
      else {
        shade->repeatFrame(repeat >= 0 ? repeat : shade->repeats);
      }
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      shade->toJSONRef(resp);
      resp.endArray();
      resp.endResponse();
    }
    else if(groupId != 255) {
      SomfyGroup * group = somfy.getGroupById(groupId);
      if(!group) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group reference could not be found.\"}"));
        return;        
      }
      if(!group->isLastCommand(command)) {
        // We are going to send this as a new command.
        group->sendCommand(command, repeat >= 0 ? repeat : group->repeats, stepSize);
      }
      else
        group->repeatFrame(repeat >= 0 ? repeat : group->repeats);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
        
      //group->toJSON(sobj);
      //serializeJson(sdoc, g_content);
      //server.send(200, _encoding_json, g_content);
    }
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
  }
}
void Web::handleGroupCommand(WebServer &server) {
  HTTPMethod method = server.method();
  uint8_t groupId = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("groupId")) {
      groupId = atoi(server.arg("groupId").c_str());
      if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
      if(server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    }
    else if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Sending Group Command");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("groupId")) groupId = obj["groupId"];
      else {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        return;
      }
      if (obj.containsKey("command")) {
        String scmd = obj["command"];
        command = translateSomfyCommand(scmd);
      }
      if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
      if(obj.containsKey("stepSize")) stepSize = obj["stepSize"].as<uint8_t>();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    SomfyGroup * group = somfy.getGroupById(groupId);
    if (group) {
      ESP_LOGI(TAG, "Received: %s", server.arg("plain").c_str());
      // Send the command to the group.
      group->sendCommand(command, repeat >= 0 ? repeat : group->repeats, stepSize);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSONRef(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}
void Web::handleDiscovery(WebServer &server) {
  HTTPMethod method = apiServer.method();
  if (method == HTTP_POST || method == HTTP_GET) {
    ESP_LOGI(TAG, "Discovery Requested");
    char connType[10] = "Unknown";
    if(net.connType == conn_types_t::ethernet) strcpy(connType, "Ethernet");
    else if(net.connType == conn_types_t::wifi) strcpy(connType, "Wifi");

    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
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
    net.needsBroadcast = true;
  }
  else
    server.send(500, _encoding_text, "Invalid http method");
}
void Web::handleBackup(WebServer &server, bool attach) {
  if(server.hasArg("attach")) attach = toBoolean(server.arg("attach").c_str(), attach);
  if(attach) {
    char filename[120];
    Timestamp ts;
    char * iso = ts.getISOTime();
    // Replace the invalid characters as quickly as we can.
    for(uint8_t i = 0; i < strlen(iso); i++) {
      switch(iso[i]) {
        case '.':
          // Just trim off the ms.
          iso[i] = '\0';
          break;
        case ':':
          iso[i] = '_';
          break;
      }
    }
    snprintf(filename, sizeof(filename), "attachment; filename=\"ESPSomfyRTS %s.backup\"", iso);
    ESP_LOGI(TAG, "%s", filename);
    server.sendHeader(F("Content-Disposition"), filename);
    server.sendHeader(F("Access-Control-Expose-Headers"), F("Content-Disposition"));
  }
  ESP_LOGI(TAG, "Saving current shade information");
  somfy.writeBackup();
  File file = LittleFS.open("/controller.backup", "r");
  if (!file) {
    ESP_LOGE(TAG, "Error opening shades.cfg");
    server.send(500, _encoding_text, "shades.cfg");
    return;
  }
  server.streamFile(file, _encoding_text);
  file.close();
}
void Web::handleSetSensor(WebServer &server) {
  uint8_t shadeId = (server.hasArg("shadeId")) ? atoi(server.arg("shadeId").c_str()) : 255;
  uint8_t groupId = (server.hasArg("groupId")) ? atoi(server.arg("groupId").c_str()) : 255;
  int8_t sunny = (server.hasArg("sunny")) ? toBoolean(server.arg("sunny").c_str(), false) ? 1 : 0 : -1;
  int8_t windy = (server.hasArg("windy")) ? atoi(server.arg("windy").c_str()) : -1;
  int8_t repeat = (server.hasArg("repeat")) ? atoi(server.arg("repeat").c_str()) : -1;
  if(server.hasArg("plain")) {
    JsonDocument doc; JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    if(obj.containsKey("shadeId")) shadeId = obj["shadeId"].as<uint8_t>();
    if(obj.containsKey("groupId")) groupId = obj["groupId"].as<uint8_t>();
    if(obj.containsKey("sunny")) {
      if(obj["sunny"].is<bool>())
        sunny = obj["sunny"].as<bool>() ? 1 : 0;
      else
        sunny = obj["sunny"].as<int8_t>();
    }
    if(obj.containsKey("windy")) {
      if(obj["windy"].is<bool>())
        windy = obj["windy"].as<bool>() ? 1 : 0;
      else
        windy = obj["windy"].as<int8_t>();
    }
    if(obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
  }
  if(shadeId != 255) {
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if(shade) {
      shade->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : shade->repeats);
      shade->emitState();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      shade->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));
      
  }
  else if(groupId != 255) {
    SomfyGroup *group = somfy.getGroupById(groupId);
    if(group) {
      group->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : group->repeats);
      group->emitState();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"An invalid groupId was provided\"}"));
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
  }
}
void Web::handleNotFound(WebServer &server) {
    HTTPMethod method = server.method();
    ESP_LOGI(TAG, "Request %s 404-%d ", server.uri().c_str(), method);
    switch (method) {
    case HTTP_POST:
      ESP_LOGI(TAG, "POST ");
      break;
    case HTTP_GET:
      ESP_LOGI(TAG, "GET ");
      break;
    case HTTP_PUT:
      ESP_LOGI(TAG, "PUT ");
      break;
    case HTTP_OPTIONS:
      ESP_LOGI(TAG, "OPTIONS ");
      server.send(200, "OK");
      return;
    default:
      ESP_LOGI(TAG, "[%d]", method);
      break;

    }
    snprintf(g_content, sizeof(g_content), "404 Service Not Found: %s", server.uri().c_str());
    server.send(404, _encoding_text, g_content);
}
void Web::handleReboot(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_POST || method == HTTP_PUT) {
    ESP_LOGI(TAG, "Rebooting ESP...");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 500;
    server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully started reboot\"}");
  }
  else {
    server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
  }
}
void Web::begin() {
  ESP_LOGI(TAG, "Creating Web MicroServices...");
  const char *keys[1] = {"apikey"};
  server.collectHeaders(keys, 1);
  // API Server Handlers
  apiServer.collectHeaders(keys, 1);
  apiServer.on("/discovery",            []() { webServer.handleDiscovery(apiServer); });
  apiServer.on("/rooms",                []() { webServer.handleGetRooms(apiServer); });
  apiServer.on("/shades",               []() { webServer.handleGetShades(apiServer); });
  apiServer.on("/groups",               []() { webServer.handleGetGroups(apiServer); });
  apiServer.on("/login",                []() { webServer.handleLogin(apiServer); });
  apiServer.onNotFound(                 []() { webServer.handleNotFound(apiServer); });
  apiServer.on("/controller",           []() { webServer.handleController(apiServer); });
  apiServer.on("/shadeCommand",         []() { webServer.handleShadeCommand(apiServer); });
  apiServer.on("/groupCommand",         []() { webServer.handleGroupCommand(apiServer); });
  apiServer.on("/tiltCommand",          []() { webServer.handleTiltCommand(apiServer); });
  apiServer.on("/repeatCommand",        []() { webServer.handleRepeatCommand(apiServer); });
  apiServer.on("/room",      HTTP_GET,  []() { webServer.handleRoom(apiServer); });
  apiServer.on("/shade",     HTTP_GET,  []() { webServer.handleShade(apiServer); });
  apiServer.on("/group",     HTTP_GET,  []() { webServer.handleGroup(apiServer); });
  apiServer.on("/setPositions",         []() { webServer.handleSetPositions(apiServer); });
  apiServer.on("/setSensor",            []() { webServer.handleSetSensor(apiServer); });
  apiServer.on("/downloadFirmware",     []() { webServer.handleDownloadFirmware(apiServer); });
  apiServer.on("/backup",               []() { webServer.handleBackup(apiServer); });
  apiServer.on("/reboot",               []() { webServer.handleReboot(apiServer); });
  apiServer.on("/homekit",              []() { webServer.handleHomeKit(apiServer); });
  apiServer.on("/homekit/resetPairings",[]() { webServer.handleHomeKitResetPairings(apiServer); });
  
  // Web Interface
  server.on("/tiltCommand",        []() { webServer.handleTiltCommand(server); });
  server.on("/repeatCommand",      []() { webServer.handleRepeatCommand(server); });
  server.on("/shadeCommand",       []() { webServer.handleShadeCommand(server); });
  server.on("/groupCommand",       []() { webServer.handleGroupCommand(server); });
  server.on("/setPositions",       []() { webServer.handleSetPositions(server); });
  server.on("/setSensor",          []() { webServer.handleSetSensor(server); });
  server.on("/upnp.xml",           []() { SSDP.schema(server.client()); });
  server.on("/",                   []() { webServer.handleStreamFile(server, "/index.html", _encoding_html); });
  server.on("/login",              []() { webServer.handleLogin(server); });
  server.on("/loginContext",       []() { webServer.handleLoginContext(server); });
  server.on("/shades.cfg",         []() { webServer.handleStreamFile(server, "/shades.cfg", _encoding_text); });
  server.on("/shades.tmp",         []() { webServer.handleStreamFile(server, "/shades.tmp", _encoding_text); });
  server.on("/getReleases",        []() { webServer.handleGetReleases(server); });
  server.on("/downloadFirmware",   []() { webServer.handleDownloadFirmware(server); });
  server.on("/cancelFirmware",     []() { webServer.handleCancelFirmware(server); });
  server.on("/backup",             []() { webServer.handleBackup(server, true); });
  server.on("/restore", HTTP_POST, []() { webServer.handleRestore(server); },
                                   []() { webServer.handleRestoreUpload(server); });
  server.on("/index.js",           []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/index.js",    "text/javascript"); });
  server.on("/ui.js",              []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/ui.js",       "text/javascript"); });
  server.on("/settings.js",        []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/settings.js", "text/javascript"); });
  server.on("/somfy.js",           []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/somfy.js",    "text/javascript"); });
  server.on("/extras.js",          []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/extras.js",   "text/javascript"); });
  server.on("/qrcode.min.js",      []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/qrcode.min.js", "text/javascript"); });
  server.on("/main.css",           []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/main.css", "text/css"); });
  server.on("/widgets.css",        []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/widgets.css", "text/css"); });
  server.on("/icons.css",          []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icons.css", "text/css"); });
  server.on("/favicon.png",        []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/favicon.png", "image/png"); });
  server.on("/icon.png",           []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icon.png", "image/png"); });
  server.on("/icon.svg",           []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/icon.svg", "image/svg+xml"); });
  server.on("/apple-icon.png",     []() { webServer.sendCacheHeaders(604800); webServer.handleStreamFile(server, "/apple-icon.png", "image/png"); });
  server.onNotFound(               []() { webServer.handleNotFound(server); });
  server.on("/controller",         []() { webServer.handleController(server); });
  server.on("/homekit",            []() { webServer.handleHomeKit(server); });
  server.on("/homekit/resetPairings", []() { webServer.handleHomeKitResetPairings(server); });
  server.on("/rooms",              []() { webServer.handleGetRooms(server); });
  server.on("/shades",             []() { webServer.handleGetShades(server); });
  server.on("/groups",             []() { webServer.handleGetGroups(server); });
  server.on("/room",               []() { webServer.handleRoom(server); });
  server.on("/shade",              []() { webServer.handleShade(server); });
  server.on("/group",              []() { webServer.handleGroup(server); });
  server.on("/getNextRoom",        []() { webServer.handleGetNextRoom(server); });
  server.on("/getNextShade",       []() { webServer.handleGetNextShade(server); });
  server.on("/getNextGroup",       []() { webServer.handleGetNextGroup(server); });
  server.on("/addRoom",            []() { webServer.handleAddRoom(server); });
  server.on("/addShade",           []() { webServer.handleAddShade(server); });
  server.on("/addGroup",           []() { webServer.handleAddGroup(server); });
  server.on("/groupOptions",       []() { webServer.handleGroupOptions(server); });
  server.on("/saveRoom",           []() { webServer.handleSaveRoom(server); });
  server.on("/saveShade",          []() { webServer.handleSaveShade(server); });
  server.on("/saveGroup",          []() { webServer.handleSaveGroup(server); });
  server.on("/setMyPosition",      []() { webServer.handleSetMyPosition(server); });
  server.on("/setRollingCode",     []() { webServer.handleSetRollingCode(server); });
  server.on("/setPaired",          []() { webServer.handleSetPaired(server); });
  server.on("/unpairShade",        []() { webServer.handleUnpairShade(server); });
  server.on("/linkRepeater",       []() { webServer.handleLinkRepeater(server); });
  server.on("/unlinkRepeater",     []() { webServer.handleUnlinkRepeater(server); });
  server.on("/unlinkRemote",       []() { webServer.handleUnlinkRemote(server); });
  server.on("/linkRemote",         []() { webServer.handleLinkRemote(server); });
  server.on("/linkToGroup",        []() { webServer.handleLinkToGroup(server); });
  server.on("/unlinkFromGroup",    []() { webServer.handleUnlinkFromGroup(server); });
  server.on("/deleteRoom",         []() { webServer.handleDeleteRoom(server); });
  server.on("/deleteShade",        []() { webServer.handleDeleteShade(server); });
  server.on("/deleteGroup",        []() { webServer.handleDeleteGroup(server); });
  server.on("/updateFirmware",  HTTP_POST, []() { webServer.handleUpdateFirmware(server); },
                                           []() { webServer.handleUpdateFirmwareUpload(server); });
  server.on("/updateShadeConfig", HTTP_POST, []() { webServer.handleUpdateShadeConfig(server); },
                                             []() { webServer.handleUpdateShadeConfigUpload(server); });
  server.on("/updateApplication", HTTP_POST, []() { webServer.handleUpdateApplication(server); },
                                             []() { webServer.handleUpdateApplicationUpload(server); });
  server.on("/scanaps",            []() { webServer.handleScanAPs(server); });
  server.on("/reboot",             []() { webServer.handleReboot(server); });
  server.on("/saveSecurity",       []() { webServer.handleSaveSecurity(server); });
  server.on("/getSecurity",        []() { webServer.handleGetSecurity(server); });
  server.on("/saveRadio",          []() { webServer.handleSaveRadio(server); });
  server.on("/getRadio",           []() { webServer.handleGetRadio(server); });
  server.on("/sendRemoteCommand",  []() { webServer.handleSendRemoteCommand(server); });
  server.on("/setgeneral",         []() { webServer.handleSetGeneral(server); });
  server.on("/setNetwork",         []() { webServer.handleSetNetwork(server); });
  server.on("/setIP",              []() { webServer.handleSetIP(server); });
  server.on("/connectwifi",        []() { webServer.handleConnectWifi(server); });
  server.on("/modulesettings",     []() { webServer.handleModuleSettings(server); });
  server.on("/networksettings",    []() { webServer.handleNetworkSettings(server); });
  server.on("/connectmqtt",        []() { webServer.handleConnectMQTT(server); });
  server.on("/mqttsettings",       []() { webServer.handleMQTTSettings(server); });
  server.on("/roomSortOrder",      []() { webServer.handleRoomSortOrder(server); });
  server.on("/shadeSortOrder",     []() { webServer.handleShadeSortOrder(server); });
  server.on("/groupSortOrder",     []() { webServer.handleGroupSortOrder(server); });
  server.on("/beginFrequencyScan", []() { webServer.handleBeginFrequencyScan(server); });
  server.on("/endFrequencyScan",   []() { webServer.handleEndFrequencyScan(server); });
  server.on("/recoverFilesystem",  []() { webServer.handleRecoverFilesystem(server); });
  server.begin();
  apiServer.begin();
}

