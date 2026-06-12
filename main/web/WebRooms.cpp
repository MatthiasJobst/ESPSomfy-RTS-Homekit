// WebRooms.cpp — Web handler implementations for room management.
//
// Covers the full lifecycle of a Somfy room via the HTTP API:
//   - Querying all rooms (handleGetRooms)
//   - Reading and updating a single room (handleRoom)
//   - Next available room ID scaffold (handleGetNextRoom)
//   - Persisting room sort order (handleRoomSortOrder)
//   - CRUD operations: add, save, delete (handleAddRoom, handleSaveRoom, handleDeleteRoom)
//
// Request parsing and JSON/status responses go through a per-handler
// WebJsonResponder (`WebJsonResponder json(server);`): json.parseBody() reads
// the body, and json.respondJson() exposes the writer for
// .object()/.array()/.error()/.success()/… responses.

#include "WebRooms.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "Web.h"
#include "WebJsonResponder.h"

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
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        auto arrJson = json.respondJson().array();
        somfy.roomController.toJSONRooms(arrJson);
    } else
        json.respondJson().notFound();
}

void WebRooms::handleRoom(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
        if (server.hasArg("roomId")) {
            int roomId = atoi(server.arg("roomId").c_str());
            SomfyRoom *room = somfy.roomController.getRoomById(roomId);
            if (room) {
                auto objJson = json.respondJson().object();
                room->toJSON(objJson);
            } else
                json.respondJson().error("Room Id not found.");
        } else {
            json.respondJson().error("You must supply a valid room id.");
        }
    } else if (method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing room.
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a room");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("roomId")) {
                SomfyRoom *room = somfy.roomController.getRoomById(obj["roomId"]);
                if (room) {
                    uint8_t err = room->fromJSON(obj);
                    if (err == 0) {
                        room->save();
                        auto objJson = json.respondJson().object();
                        room->toJSON(objJson);
                    } else {
                        char buf[96];
                        snprintf(buf, sizeof(buf), "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
                        server.send(500, ENCODING_JSON, buf);
                    }
                } else
                    json.respondJson().error("Room Id not found.");
            } else
                json.respondJson().error("No room id was supplied.");
        } else
            json.respondJson().error("No room object supplied.");
    } else
        json.respondJson().invalidMethod();
}

void WebRooms::handleGetNextRoom(WebServer &server)
{
    WebJsonResponder json(server);
    auto objJson = json.respondJson().object();
    objJson.addElem("roomId", somfy.roomController.getNextRoomId());
}

void WebRooms::handleRoomSortOrder(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Plain: %s", server.arg("plain").c_str());
    ESP_LOGI(s_TAG, "Method: %d", server.method());
    JsonArray arr;
    if (!json.parseBody(arr)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        uint8_t order = 0;
        for (JsonVariant v : arr) {
            uint8_t roomId = v.as<uint8_t>();
            if (roomId != 0) {
                SomfyRoom *room = somfy.roomController.getRoomById(roomId);
                if (room) room->sortOrder = order++;
            }
        }
        json.respondJson().ok("Successfully set room order");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebRooms::handleAddRoom(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    SomfyRoom *room = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Adding a room");
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        ESP_LOGI(s_TAG, "Counting rooms");
        if (somfy.roomController.roomCount() > SOMFY_MAX_ROOMS) {
            json.respondJson().error("Maximum number of rooms exceeded.");
            return;
        } else {
            ESP_LOGI(s_TAG, "Adding room");
            room = somfy.roomController.addRoom(obj);
            if (!room) {
                json.respondJson().error("Error adding room.");
                return;
            }
        }
    }
    if (room) {
        auto objJson = json.respondJson().object();
        room->toJSON(objJson);
    } else {
        json.respondJson().error("Error saving Somfy Room.");
    }
}

void WebRooms::handleSaveRoom(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a room");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("roomId")) {
                SomfyRoom *room = somfy.roomController.getRoomById(obj["roomId"]);
                if (room) {
                    room->fromJSON(obj);
                    room->save();
                    auto objJson = json.respondJson().object();
                    room->toJSON(objJson);
                } else
                    json.respondJson().error("Room Id not found.");
            } else
                json.respondJson().error("No room id was supplied.");
        } else
            json.respondJson().error("No room object supplied.");
    }
}

void WebRooms::handleDeleteRoom(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    uint8_t roomId = 0;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("roomId")) {
            roomId = atoi(server.arg("roomId").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Deleting a Room");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("roomId"))
                roomId = obj["roomId"];
            else
                json.respondJson().error("No room id was supplied.");
        } else
            json.respondJson().error("No room object supplied.");
    }
    SomfyRoom *room = somfy.roomController.getRoomById(roomId);
    if (!room)
        json.respondJson().error("Room with the specified id not found.");
    else {
        somfy.roomController.deleteRoom(roomId);
        json.respondJson().success("Room deleted.");
    }
}
