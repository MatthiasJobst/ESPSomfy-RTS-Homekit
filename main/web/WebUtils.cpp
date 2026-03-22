// WebUtils.cpp — Web handler implementations for utility and scan operations.
//
// Miscellaneous endpoints that don't belong to a single domain:
//   - Next available shade ID scaffold (handleGetNextShade)
//   - Shade sort order persistence (handleShadeSortOrder)
//   - Wi-Fi AP scanning (handleScanAPs)
//   - Sending raw remote commands (handleSendRemoteCommand)
//   - Frequency scan lifecycle (handleBeginFrequencyScan, handleEndFrequencyScan)
//   - LittleFS filesystem recovery from GitHub (handleRecoverFilesystem)

#include <WiFi.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <esp_log.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "GitOTA.h"
#include "SomfyNetwork.h"

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Web webServer;
extern GitUpdater git;
extern SomfyNetwork net;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_json[];

static const char* TAG = "WebUtils";

void Web::handleGetNextShade(WebServer &server) {
  uint8_t shadeId = somfy.getNextShadeId();
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  resp.addElem("shadeId", shadeId);
  resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(shadeId));
  resp.addElem("bitLength", somfy.transceiver.config.type);
  resp.addElem("stepSize", (uint8_t)100);
  resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
  resp.endObject();
  resp.endResponse();
}

void Web::handleShadeSortOrder(WebServer &server) {
  JsonDocument doc;
  ESP_LOGI(TAG, "Plain: %s", server.arg("plain").c_str());
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    webServer.handleDeserializationError(server, err);
    return;
  }
  else {
    JsonArray arr = doc.as<JsonArray>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      uint8_t order = 0;
      for(JsonVariant v : arr) {
        uint8_t shadeId = v.as<uint8_t>();
        if (shadeId != 255) {
          SomfyShade *shade = somfy.getShadeById(shadeId);
          if(shade) shade->sortOrder = order++;
        }
      }
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set shade order\"}");
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}

void Web::handleScanAPs(WebServer &server) {
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  if(net.softAPOpened) WiFi.disconnect(false);
  int n = WiFi.scanNetworks(false, true);
  esp_task_wdt_add(NULL);
  ESP_LOGI(TAG, "Scanned %d networks", n);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  resp.beginObject("connected");
  resp.addElem("name", settings.WIFI.ssid);
  resp.addElem("passphrase", settings.WIFI.passphrase);
  resp.addElem("strength", (int32_t)WiFi.RSSI());
  resp.addElem("channel", (int32_t)WiFi.channel());
  resp.endObject();
  resp.beginArray("accessPoints");
  for(int i = 0; i < n; ++i) {
    if(WiFi.SSID(i).length() == 0 || WiFi.RSSI(i) < -95) continue;
    resp.beginObject();
    resp.addElem("name", WiFi.SSID(i).c_str());
    resp.addElem("channel", (int32_t)WiFi.channel(i));
    resp.addElem("strength", (int32_t)WiFi.RSSI(i));
    resp.addElem("macAddress", WiFi.BSSIDstr(i).c_str());
    resp.endObject();
  }
  resp.endArray();
  resp.endObject();
  resp.endResponse();
}

void Web::handleSendRemoteCommand(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    somfy_frame_t frame;
    uint8_t repeats = 0;
    if (server.hasArg("address")) {
      frame.remoteAddress = atoi(server.arg("address").c_str());
      if (server.hasArg("encKey")) frame.encKey = atoi(server.arg("encKey").c_str());
      if (server.hasArg("command")) frame.cmd = translateSomfyCommand(server.arg("command"));
      if (server.hasArg("rcode")) frame.rollingCode = atoi(server.arg("rcode").c_str());
      if (server.hasArg("repeats")) repeats = atoi(server.arg("repeats").c_str());
    }
    else if (server.hasArg("plain")) {
      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        String scmd;
        if (obj.containsKey("address")) frame.remoteAddress = obj["address"];
        if (obj.containsKey("command")) scmd = obj["command"].as<String>();
        if (obj.containsKey("repeats")) repeats = obj["repeats"];
        if (obj.containsKey("rcode")) frame.rollingCode = obj["rcode"];
        if (obj.containsKey("encKey")) frame.encKey = obj["encKey"];
        frame.cmd = translateSomfyCommand(scmd.c_str());
      }
    }
    if (frame.remoteAddress > 0 && frame.rollingCode > 0) {
      somfy.sendFrame(frame, repeats);
      server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Command Sent\"}"));
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No address or rolling code provided\"}"));
  }
}

void Web::handleBeginFrequencyScan(WebServer &server) {
  somfy.transceiver.beginFrequencyScan();
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  somfy.transceiver.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}

void Web::handleEndFrequencyScan(WebServer &server) {
  somfy.transceiver.endFrequencyScan();
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  somfy.transceiver.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}

void Web::handleRecoverFilesystem(WebServer &server) {
  if(git.status == GIT_UPDATING)
    server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Filesystem is updating.  Please wait!!!\"}");
  else if(git.status != GIT_STATUS_READY)
    server.send(200, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Cannot recover file system at this time.\"}");
  else {
    git.recoverFilesystem();
    server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Recovering filesystem from github please wait!!!\"}");
  }
}


