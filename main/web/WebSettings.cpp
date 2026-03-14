#include <WiFi.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "SomfyNetwork.h"

extern ConfigSettings settings;
extern rebootDelay_t rebootDelay;
extern SomfyShadeController somfy;
extern Web webServer;
extern MQTTClass mqtt;
extern GitUpdater git;
extern SomfyNetwork net;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_html[];
extern const char _encoding_json[];

// ============================================================
// Settings handlers
// ============================================================
void Web::handleGetReleases(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  GitRepo repo;
  repo.getReleases();
  git.setCurrentRelease(repo);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  repo.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}
void Web::handleCancelFirmware(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  // If we are currently downloading the filesystem we cannot cancel.
  if(!git.lockFS) {
    git.status = GIT_UPDATE_CANCELLING;
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    git.toJSON(resp);
    resp.endObject();
    resp.endResponse();
    git.cancelled = true;
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Cannot cancel during filesystem update.\"}"));
  }
}
void Web::handleSaveSecurity(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    Serial.print("Error parsing JSON ");
    Serial.println(err.c_str());
    String msg = err.c_str();
    server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      settings.Security.fromJSON(obj);
      settings.Security.save();
      char token[65];
      webServer.createAPIToken(server.client().remoteIP(), token);
      obj["apiKey"] = token;
      JsonDocument sdoc;
      JsonObject sobj = sdoc.to<JsonObject>();
      settings.Security.toJSON(sobj);
      serializeJson(sdoc, g_content);
      server.send(200, _encoding_json, g_content);
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleGetSecurity(WebServer &server) {
  webServer.sendCORSHeaders(server);
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  settings.Security.toJSON(obj);
  serializeJson(doc, g_content);
  server.send(200, _encoding_json, g_content);
}
void Web::handleSaveRadio(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    Serial.print("Error parsing JSON ");
    Serial.println(err.c_str());
    String msg = err.c_str();
    server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      somfy.transceiver.fromJSON(obj);
      somfy.transceiver.save();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      somfy.transceiver.toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleGetRadio(WebServer &server) {
  webServer.sendCORSHeaders(server);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  somfy.transceiver.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}
void Web::handleSetGeneral(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  JsonDocument doc;
  Serial.print("Plain: ");
  Serial.print(server.method());
  Serial.println(server.arg("plain"));
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    webServer.handleDeserializationError(server, err);
    return;
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      if (obj.containsKey("hostname") || obj.containsKey("ssdpBroadcast") || obj.containsKey("checkForUpdate")) {
        bool checkForUpdate = settings.checkForUpdate;
        settings.fromJSON(obj);
        settings.save();
        if(settings.checkForUpdate != checkForUpdate) git.emitUpdateCheck();
        if(obj.containsKey("hostname")) net.updateHostname();
      }
      if (obj.containsKey("ntpServer") || obj.containsKey("ntpServer")) {
        settings.NTP.fromJSON(obj);
        settings.NTP.save();
      }
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set General Settings\"}");
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleSetNetwork(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    Serial.print("Error parsing JSON ");
    Serial.println(err.c_str());
    String msg = err.c_str();
    server.send(400, _encoding_html, "Error parsing JSON body<br>" + msg);
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      bool reboot = false;
      if(obj.containsKey("connType") && obj["connType"].as<uint8_t>() != static_cast<uint8_t>(settings.connType)) {
        settings.connType = static_cast<conn_types_t>(obj["connType"].as<uint8_t>());
        settings.save();
        reboot = true;
      }
      if(obj.containsKey("wifi")) {
        JsonObject objWifi = obj["wifi"];
        if(settings.connType == conn_types_t::wifi) {
          if(objWifi.containsKey("ssid") && objWifi["ssid"].as<String>().compareTo(settings.WIFI.ssid) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
          if(objWifi.containsKey("passphrase") && objWifi["passphrase"].as<String>().compareTo(settings.WIFI.passphrase) != 0) {
            if(WiFi.softAPgetStationNum() == 0) reboot = true;
          }
        }
        settings.WIFI.fromJSON(objWifi);
        settings.WIFI.save();
      }
      if(obj.containsKey("ethernet")) {
        JsonObject objEth = obj["ethernet"];
        if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref)
          reboot = true;
        settings.Ethernet.fromJSON(objEth);
        settings.Ethernet.save();
      }
      if (reboot) {
        Serial.println("Rebooting ESP for new Network settings...");
        rebootDelay.reboot = true;
        rebootDelay.rebootTime = millis() + 1000;
      }
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleSetIP(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  Serial.println("Setting IP...");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    webServer.handleDeserializationError(server, err);
    return;
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      settings.IP.fromJSON(obj);
      settings.IP.save();
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set Network Settings\"}");
    }
    else {
      server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleConnectWifi(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  Serial.println("Settings WIFI connection...");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    webServer.handleDeserializationError(server, err);
    return;
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      String ssid = "";
      String passphrase = "";
      if (obj.containsKey("ssid")) ssid = obj["ssid"].as<String>();
      if (obj.containsKey("passphrase")) passphrase = obj["passphrase"].as<String>();
      bool reboot;
      if (ssid.compareTo(settings.WIFI.ssid) != 0) reboot = true;
      if (passphrase.compareTo(settings.WIFI.passphrase) != 0) reboot = true;
      if (!settings.WIFI.ssidExists(ssid.c_str()) && ssid.length() > 0) {
        server.send(400, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"WiFi Network Does not exist\"}");
      }
      else {
        SETCHARPROP(settings.WIFI.ssid, ssid.c_str(), sizeof(settings.WIFI.ssid));
        SETCHARPROP(settings.WIFI.passphrase, passphrase.c_str(), sizeof(settings.WIFI.passphrase));
        settings.WIFI.save();
        settings.WIFI.print();
        server.send(201, _encoding_json, "{\"status\":\"OK\",\"desc\":\"Successfully set server connection\"}");
        if (reboot) {
          Serial.println("Rebooting ESP for new WiFi settings...");
          rebootDelay.reboot = true;
          rebootDelay.rebootTime = millis() + 1000;
        }
      }
    }
    else {
      server.send(201, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleModuleSettings(WebServer &server) {
  webServer.sendCORSHeaders(server);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  resp.addElem("fwVersion", settings.fwVersion.name);
  settings.toJSON(resp);
  settings.NTP.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}
void Web::handleNetworkSettings(WebServer &server) {
  webServer.sendCORSHeaders(server);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  settings.toJSON(resp);
  resp.addElem("fwVersion", settings.fwVersion.name);
  resp.beginObject("ethernet");
  settings.Ethernet.toJSON(resp);
  resp.endObject();
  resp.beginObject("wifi");
  settings.WIFI.toJSON(resp);
  resp.endObject();
  resp.beginObject("ip");
  settings.IP.toJSON(resp);
  resp.endObject();
  resp.endObject();
  resp.endResponse();
}
void Web::handleConnectMQTT(WebServer &server) {
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    webServer.handleDeserializationError(server, err);
    return;
  }
  else {
    JsonObject obj = doc.as<JsonObject>();
    HTTPMethod method = server.method();
    Serial.print("Saving MQTT ");
    Serial.print(F("HTTP Method: "));
    Serial.println(server.method());
    if (method == HTTP_POST || method == HTTP_PUT) {
      mqtt.disconnect();
      settings.MQTT.fromJSON(obj);
      settings.MQTT.save();
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      settings.MQTT.toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}
void Web::handleMQTTSettings(WebServer &server) {
  webServer.sendCORSHeaders(server);
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  settings.MQTT.toJSON(resp);
  resp.endObject();
  resp.endResponse();
}
