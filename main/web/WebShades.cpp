// WebShades.cpp — Web handler implementations for shade management.
//
// Covers the full lifecycle of a Somfy shade via the HTTP API:
//   - Querying shades (handleGetShades)
//   - Sending movement and tilt commands (handleShadeCommand, handleTiltCommand)
//   - Reading and updating a single shade (handleShade, handleSetPositions)
//   - CRUD operations: add, save, delete (handleAddShade, handleSaveShade, handleDeleteShade)
//   - Position calibration: my-position and rolling code (handleSetMyPosition, handleSetRollingCode)
//   - Pairing/unpairing with the motor (handleSetPaired, handleUnpairShade)
//   - Linking and unlinking hardware repeaters (handleLinkRepeater, handleUnlinkRepeater)
//   - Linking and unlinking physical remotes (handleLinkRemote, handleUnlinkRemote)

#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Web webServer;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];
extern const char _response_404[];

// ---------------------------------------------------------------------------
// File-local helpers — shared by all handlers in this translation unit
// ---------------------------------------------------------------------------

// Look up a shade by id. Sends a 500 error and returns nullptr if not found.
static SomfyShade* requireShade(WebServer &server, uint8_t shadeId) {
    SomfyShade* shade = somfy.getShadeById(shadeId);
    if (!shade) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade not found.\"}"));
    return shade;
}

// Send the shade's JSON state as the HTTP response.
// Pass ref=true to use toJSONRef() (minimal reference fields) instead of toJSON().
static void sendShadeJSON(WebServer &server, SomfyShade *shade, bool ref = false) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    if (ref) shade->toJSONRef(resp); else shade->toJSON(resp);
    resp.endObject();
    resp.endResponse();
}

// ---------------------------------------------------------------------------

void Web::handleGetShades(WebServer &server) {
    webServer.sendCORSHeaders(server);
    if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONShades(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}

void Web::handleShadeCommand(WebServer& server) {
  webServer.sendCORSHeaders(server);
  if (server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t shadeId = 255;
  uint8_t target = 255;
  uint8_t stepSize = 0;
  int8_t repeat = -1;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
      if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
      else if (server.hasArg("target")) target = atoi(server.arg("target").c_str());
      if (server.hasArg("repeat")) repeat = atoi(server.arg("repeat").c_str());
      if(server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Sending Shade Command");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
      if (obj.containsKey("command")) {
          String scmd = obj["command"];
          command = translateSomfyCommand(scmd);
      }
      else if (obj.containsKey("target")) target = obj["target"].as<uint8_t>();
      if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
      if(obj.containsKey("stepSize")) stepSize = obj["stepSize"].as<uint8_t>();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    SomfyShade* shade = requireShade(server, shadeId);
    if (shade) {
      Serial.print("Received:"); Serial.println(server.arg("plain"));
      if (target <= 100)
          shade->moveToTarget(shade->transformPosition(target));
      else
          shade->sendCommand(command, repeat > 0 ? repeat : shade->repeats, stepSize);
      sendShadeJSON(server, shade, true);
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void Web::handleTiltCommand(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t shadeId = 255;
  uint8_t target = 255;
  somfy_commands command = somfy_commands::My;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
      if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
      else if(server.hasArg("target")) target = atoi(server.arg("target").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Sending Shade Tilt Command");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
      if (obj.containsKey("command")) {
        String scmd = obj["command"];
        command = translateSomfyCommand(scmd);
      }
      else if(obj.containsKey("target")) target = obj["target"].as<uint8_t>();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    SomfyShade* shade = requireShade(server, shadeId);
    if (shade) {
      Serial.print("Received:"); Serial.println(server.arg("plain"));
      if(target <= 100)
        shade->moveToTiltTarget(shade->transformPosition(target));
      else
        shade->sendTiltCommand(command);
      sendShadeJSON(server, shade, true);
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void Web::handleShade(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  if (server.method() == HTTP_GET) {
    if (server.hasArg("shadeId")) {
      SomfyShade* shade = requireShade(server, atoi(server.arg("shadeId").c_str()));
      if (shade) sendShadeJSON(server, shade);
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
    }
  }
  else server.send(404, _encoding_text, _response_404);
}

void Web::handleSetPositions(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = (server.hasArg("shadeId")) ? atoi(server.arg("shadeId").c_str()) : 255;
  int8_t pos = (server.hasArg("position")) ? atoi(server.arg("position").c_str()) : -1;
  int8_t tiltPos = (server.hasArg("tiltPosition")) ? atoi(server.arg("tiltPosition").c_str()) : -1;
  if(server.hasArg("plain")) {
    JsonDocument doc; JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    if(obj.containsKey("shadeId")) shadeId = obj["shadeId"];
    if(obj.containsKey("position")) pos = obj["position"];
    if(obj.containsKey("tiltPosition")) tiltPos = obj["tiltPosition"];
  }
  if(shadeId != 255) {
    SomfyShade *shade = requireShade(server, shadeId);
    if(shade) {
      if(pos >= 0) shade->target = shade->currentPos = pos;
      if(tiltPos >= 0 && shade->tiltType != tilt_types::none) shade->tiltTarget = shade->currentTiltPos = tiltPos;
      shade->emitState();
      sendShadeJSON(server, shade);
    }
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
  }
}

void Web::handleAddShade(WebServer &server) {
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  SomfyShade* shade = nullptr;
  if (method == HTTP_POST || method == HTTP_PUT) {
    Serial.println("Adding a shade");
    JsonDocument doc; JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    Serial.println("Counting shades");
    if (somfy.shadeCount() > SOMFY_MAX_SHADES) {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of shades exceeded.\"}"));
      return;
    }
    Serial.println("Adding shade");
    shade = somfy.addShade(obj);
    if (!shade) {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding shade.\"}"));
      return;
    }
  }
  if (shade) sendShadeJSON(server, shade);
  else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Shade.\"}"));
}

void Web::handleSaveShade(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      Serial.println("Updating a shade");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) {
        SomfyShade* shade = requireShade(server, obj["shadeId"].as<uint8_t>());
        if (shade) {
          int8_t err = shade->fromJSON(obj);
          if(err == 0) { shade->save(); sendShadeJSON(server, shade); }
          else {
            snprintf(g_content, sizeof(g_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
            server.send(500, _encoding_json, g_content);
          }
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
  }
}

void Web::handleDeleteShade(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t shadeId = 255;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Deleting a shade");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
  }
  SomfyShade* shade = requireShade(server, shadeId);
  if (!shade) return;
  if(shade->isInGroup())
    server.send(400, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"This shade is a member of a group and cannot be deleted.\"}"));
  else {
    somfy.deleteShade(shadeId);
    server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Shade deleted.\"}"));
  }
}

void Web::handleSetMyPosition(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  uint8_t shadeId = 255;
  int8_t pos = -1;
  int8_t tilt = -1;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
      if(server.hasArg("pos")) pos = atoi(server.arg("pos").c_str());
      if(server.hasArg("tilt")) tilt = atoi(server.arg("tilt").c_str());
    }
    else if (server.hasArg("plain")) {
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
      if(obj.containsKey("pos")) pos = obj["pos"].as<int8_t>();
      if(obj.containsKey("tilt")) tilt = obj["tilt"].as<int8_t>();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade object supplied.\"}"));
    SomfyShade* shade = requireShade(server, shadeId);
    if (shade) {
      if(tilt < 0) tilt = shade->myPos;
      if(shade->tiltType == tilt_types::none) tilt = -1;
      if(pos >= 0 && pos <= 100)
        shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
      sendShadeJSON(server, shade, true);
    }
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void Web::handleSetRollingCode(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    uint8_t shadeId = 255;
    uint16_t rollingCode = 0;
    if (server.hasArg("plain")) {
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
      if(obj.containsKey("rollingCode")) rollingCode = obj["rollingCode"];
    }
    else if (server.hasArg("shadeId")) {
      shadeId = atoi(server.arg("shadeId").c_str());
      rollingCode = atoi(server.arg("rollingCode").c_str());
    }
    SomfyShade* shade = requireShade(server, shadeId);
    if (shade) {
      shade->setRollingCode(rollingCode);
      sendShadeJSON(server, shade);
    }
  }
}

void Web::handleSetPaired(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  uint8_t shadeId = 255;
  bool paired = false;
  if(server.hasArg("plain")) {
    JsonDocument doc; JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
    if(obj.containsKey("paired")) paired = obj["paired"];
  }
  else if (server.hasArg("shadeId"))
    shadeId = atoi(server.arg("shadeId").c_str());
  if(server.hasArg("paired"))
    paired = toBoolean(server.arg("paired").c_str(), false);
  SomfyShade* shade = requireShade(server, shadeId);
  if (shade) {
    shade->paired = paired;
    shade->save();
    sendShadeJSON(server, shade);
  }
}

void Web::handleUnpairShade(WebServer &server) {
  webServer.sendCORSHeaders(server);
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    uint8_t shadeId = 255;
    if (server.hasArg("plain")) {
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
    }
    else if (server.hasArg("shadeId"))
      shadeId = atoi(server.arg("shadeId").c_str());
    SomfyShade* shade = requireShade(server, shadeId);
    if (shade) {
      shade->sendCommand(somfy_commands::Prog, shade->bitLength == 56 ? 7 : 1);
      shade->paired = false;
      shade->save();
      sendShadeJSON(server, shade);
    }
  }
}

static void repeaterLinkOp(WebServer &server, bool link) {
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    uint32_t address = 0;
    if (server.hasArg("plain")) {
      Serial.println(link ? "Linking a repeater" : "Unlinking a repeater");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("address")) address = obj["address"];
      else if(obj.containsKey("remoteAddress")) address = obj["remoteAddress"];
    }
    else if(server.hasArg("address"))
      address = atoi(server.arg("address").c_str());
    if(address == 0)
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No repeater address was supplied.\"}"));
    else {
      if (link) somfy.linkRepeater(address); else somfy.unlinkRepeater(address);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONRepeaters(resp);
      resp.endArray();
      resp.endResponse();
    }
  }
}

void Web::handleLinkRepeater(WebServer &server) {
  webServer.sendCORSHeaders(server);
  repeaterLinkOp(server, true);
}

void Web::handleUnlinkRepeater(WebServer &server) {
  webServer.sendCORSHeaders(server);
  repeaterLinkOp(server, false);
}

static void remoteLinkOp(WebServer &server, bool link) {
  if(server.method() == HTTP_OPTIONS) { server.send(200, "OK"); return; }
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      if (link) Serial.println("Linking a remote");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("shadeId")) {
        SomfyShade* shade = requireShade(server, obj["shadeId"].as<uint8_t>());
        if (shade) {
          if (obj.containsKey("remoteAddress")) {
            if (link) {
              if (obj.containsKey("rollingCode")) shade->linkRemote(obj["remoteAddress"], obj["rollingCode"]);
              else shade->linkRemote(obj["remoteAddress"]);
            } else {
              shade->unlinkRemote(obj["remoteAddress"]);
            }
          }
          else {
            server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Remote address not provided.\"}"));
          }
          sendShadeJSON(server, shade);
        }
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No shade id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No remote object supplied.\"}"));
  }
}

void Web::handleLinkRemote(WebServer &server) {
  webServer.sendCORSHeaders(server);
  remoteLinkOp(server, true);
}

void Web::handleUnlinkRemote(WebServer &server) {
  webServer.sendCORSHeaders(server);
  remoteLinkOp(server, false);
}
