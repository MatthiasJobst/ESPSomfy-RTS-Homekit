// WebRooms.cpp — Web handler implementations for room management.
//
// Covers the full lifecycle of a Somfy room via the HTTP API:
//   - Querying all rooms (handleGetRooms)
//   - Reading and updating a single room (handleRoom)
//   - Next available room ID scaffold (handleGetNextRoom)
//   - Persisting room sort order (handleRoomSortOrder)
//   - CRUD operations: add, save, delete (handleAddRoom, handleSaveRoom, handleDeleteRoom)

#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"

extern SomfyShadeController somfy;
extern Web webServer;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];
extern const char _response_404[];

void Web::handleGetRooms(WebServer &server) {
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONRooms(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}

void Web::handleRoom(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_GET) {
    if (server.hasArg("roomId")) {
      int roomId = atoi(server.arg("roomId").c_str());
      SomfyRoom* room = somfy.getRoomById(roomId);
      if (room) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        room->toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid room id.\"}"));
    }
  }
  else if (method == HTTP_PUT || method == HTTP_POST) {
    // We are updating an existing room.
    if (server.hasArg("plain")) {
      Serial.println("Updating a room");
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        this->handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("roomId")) {
          SomfyRoom* room = somfy.getRoomById(obj["roomId"]);
          if (room) {
            uint8_t err = room->fromJSON(obj);
            if(err == 0) {
              room->save();
              JsonResponse resp;
              resp.beginResponse(&server, g_content, sizeof(g_content));
              resp.beginObject();
              room->toJSON(resp);
              resp.endObject();
              resp.endResponse();
            }
            else {
              snprintf(g_content, sizeof(g_content), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
              server.send(500, _encoding_json, g_content);
            }
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void Web::handleGetNextRoom(WebServer &server) {
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  resp.addElem("roomId", somfy.getNextRoomId());
  resp.endObject();
  resp.endResponse();
}

void Web::handleRoomSortOrder(WebServer &server) {
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
    JsonArray arr = doc.as<JsonArray>();
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
      uint8_t order = 0;
      for(JsonVariant v : arr) {
        uint8_t roomId = v.as<uint8_t>();
        if (roomId != 0) {
          SomfyRoom *room = somfy.getRoomById(roomId);
          if(room) room->sortOrder = order++;
        }
      }
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set room order\"}");
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}

void Web::handleAddRoom(WebServer &server) {
  HTTPMethod method = server.method();
  SomfyRoom * room = nullptr;
  if (method == HTTP_POST || method == HTTP_PUT) {
    Serial.println("Adding a room");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      webServer.handleDeserializationError(server, err);
      return;
    }
    else {
      JsonObject obj = doc.as<JsonObject>();
      Serial.println("Counting rooms");
      if (somfy.roomCount() > SOMFY_MAX_ROOMS) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of rooms exceeded.\"}"));
        return;
      }
      else {
        Serial.println("Adding room");
        room = somfy.addRoom(obj);
        if (!room) {
          server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding room.\"}"));
          return;
        }
      }
    }
  }
  if (room) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    room->toJSON(resp);
    resp.endObject();
    resp.endResponse();
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Room.\"}"));
  }
}

void Web::handleSaveRoom(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      Serial.println("Updating a room");
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("roomId")) {
          SomfyRoom* room = somfy.getRoomById(obj["roomId"]);
          if (room) {
            room->fromJSON(obj);
            room->save();
            JsonResponse resp;
            resp.beginResponse(&server, g_content, sizeof(g_content));
            resp.beginObject();
            room->toJSON(resp);
            resp.endObject();
            resp.endResponse();
          }
          else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
  }
}

void Web::handleDeleteRoom(WebServer &server) {
  HTTPMethod method = server.method();
  uint8_t roomId = 0;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("roomId")) {
      roomId = atoi(server.arg("roomId").c_str());
    }
    else if (server.hasArg("plain")) {
      Serial.println("Deleting a Room");
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        if (obj.containsKey("roomId")) roomId = obj["roomId"];
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
      }
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
  }
  SomfyRoom* room = somfy.getRoomById(roomId);
  if (!room) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Room with the specified id not found.\"}"));
  else {
    somfy.deleteRoom(roomId);
    server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Room deleted.\"}"));
  }
}

