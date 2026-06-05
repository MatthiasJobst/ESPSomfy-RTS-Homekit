// WebRooms.cpp — Web handler implementations for room management.
//
// Covers the full lifecycle of a Somfy room via the HTTP API:
//   - Querying all rooms (handleGetRooms)
//   - Reading and updating a single room (handleRoom)
//   - Next available room ID scaffold (handleGetNextRoom)
//   - Persisting room sort order (handleRoomSortOrder)
//   - CRUD operations: add, save, delete (handleAddRoom, handleSaveRoom, handleDeleteRoom)

#include "WebRooms.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

extern SomfyShadeController somfy;

static const char *s_TAG = "WebRooms";

void WebRooms::begin()
{
    // REST API routes
    registerApiHandler("/rooms", [this]() { handleGetRooms(apiServer); });
    registerApiHandler("/room", HTTP_GET, [this]() { handleRoom(apiServer); });
    // Web UI routes
    registerHandler("/rooms", [this]() { handleGetRooms(server); });
    registerHandler("/room", [this]() { handleRoom(server); });
    registerHandler("/getNextRoom", [this]() { handleGetNextRoom(server); });
    registerHandler("/addRoom", [this]() { handleAddRoom(server); });
    registerHandler("/saveRoom", [this]() { handleSaveRoom(server); });
    registerHandler("/deleteRoom", [this]() { handleDeleteRoom(server); });
    registerHandler("/roomSortOrder", [this]() { handleRoomSortOrder(server); });
}

void WebRooms::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebRooms::handleGetRooms(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginArray();
        somfy.toJSONRooms(resp);
        resp.endArray();
        resp.endResponse();
    } else
        server.send(404, ENCODING_TEXT, RESPONSE_404);
}

void WebRooms::handleRoom(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
        if (server.hasArg("roomId")) {
            int roomId = atoi(server.arg("roomId").c_str());
            SomfyRoom *room = somfy.getRoomById(roomId);
            if (room) {
                JsonResponse resp;
                resp.beginResponse(&server, content, sizeof(content));
                resp.beginObject();
                room->toJSON(resp);
                resp.endObject();
                resp.endResponse();
            } else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
        } else {
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid room id.\"}"));
        }
    } else if (method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing room.
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a room");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (err) {
                sendDeserializationError(server, err);
                return;
            } else {
                JsonObject obj = doc.as<JsonObject>();
                if (obj.containsKey("roomId")) {
                    SomfyRoom *room = somfy.getRoomById(obj["roomId"]);
                    if (room) {
                        uint8_t err = room->fromJSON(obj);
                        if (err == 0) {
                            room->save();
                            JsonResponse resp;
                            resp.beginResponse(&server, content, sizeof(content));
                            resp.beginObject();
                            room->toJSON(resp);
                            resp.endObject();
                            resp.endResponse();
                        } else {
                            snprintf(content, sizeof(content),
                                     "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
                            server.send(500, ENCODING_JSON, content);
                        }
                    } else
                        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
                } else
                    server.send(500, ENCODING_JSON,
                                F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
            }
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
    } else
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void WebRooms::handleGetNextRoom(WebServer &server)
{
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    resp.addElem("roomId", somfy.getNextRoomId());
    resp.endObject();
    resp.endResponse();
}

void WebRooms::handleRoomSortOrder(WebServer &server)
{
    JsonDocument doc;
    ESP_LOGI(s_TAG, "Plain: %s", server.arg("plain").c_str());
    ESP_LOGI(s_TAG, "Method: %d", server.method());
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        sendDeserializationError(server, err);
        return;
    } else {
        JsonArray arr = doc.as<JsonArray>();
        HTTPMethod method = server.method();
        if (method == HTTP_POST || method == HTTP_PUT) {
            uint8_t order = 0;
            for (JsonVariant v : arr) {
                uint8_t roomId = v.as<uint8_t>();
                if (roomId != 0) {
                    SomfyRoom *room = somfy.getRoomById(roomId);
                    if (room) room->sortOrder = order++;
                }
            }
            server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set room order\"}");
        } else {
            server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    }
}

void WebRooms::handleAddRoom(WebServer &server)
{
    HTTPMethod method = server.method();
    SomfyRoom *room = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Adding a room");
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            sendDeserializationError(server, err);
            return;
        } else {
            JsonObject obj = doc.as<JsonObject>();
            ESP_LOGI(s_TAG, "Counting rooms");
            if (somfy.roomCount() > SOMFY_MAX_ROOMS) {
                server.send(500, ENCODING_JSON,
                            F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of rooms exceeded.\"}"));
                return;
            } else {
                ESP_LOGI(s_TAG, "Adding room");
                room = somfy.addRoom(obj);
                if (!room) {
                    server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Error adding room.\"}"));
                    return;
                }
            }
        }
    }
    if (room) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        room->toJSON(resp);
        resp.endObject();
        resp.endResponse();
    } else {
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Room.\"}"));
    }
}

void WebRooms::handleSaveRoom(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a room");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (err) {
                sendDeserializationError(server, err);
                return;
            } else {
                JsonObject obj = doc.as<JsonObject>();
                if (obj.containsKey("roomId")) {
                    SomfyRoom *room = somfy.getRoomById(obj["roomId"]);
                    if (room) {
                        room->fromJSON(obj);
                        room->save();
                        JsonResponse resp;
                        resp.beginResponse(&server, content, sizeof(content));
                        resp.beginObject();
                        room->toJSON(resp);
                        resp.endObject();
                        resp.endResponse();
                    } else
                        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Room Id not found.\"}"));
                } else
                    server.send(500, ENCODING_JSON,
                                F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
            }
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
    }
}

void WebRooms::handleDeleteRoom(WebServer &server)
{
    HTTPMethod method = server.method();
    uint8_t roomId = 0;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("roomId")) {
            roomId = atoi(server.arg("roomId").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Deleting a Room");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (err) {
                sendDeserializationError(server, err);
                return;
            } else {
                JsonObject obj = doc.as<JsonObject>();
                if (obj.containsKey("roomId"))
                    roomId = obj["roomId"];
                else
                    server.send(500, ENCODING_JSON,
                                F("{\"status\":\"ERROR\",\"desc\":\"No room id was supplied.\"}"));
            }
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No room object supplied.\"}"));
    }
    SomfyRoom *room = somfy.getRoomById(roomId);
    if (!room)
        server.send(500, ENCODING_JSON,
                    F("{\"status\":\"ERROR\",\"desc\":\"Room with the specified id not found.\"}"));
    else {
        somfy.deleteRoom(roomId);
        server.send(200, ENCODING_JSON, F("{\"status\":\"SUCCESS\",\"desc\":\"Room deleted.\"}"));
    }
}
