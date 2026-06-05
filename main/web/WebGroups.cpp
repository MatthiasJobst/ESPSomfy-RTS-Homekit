// WebGroups.cpp — Web handler implementations for group management.
//
// Covers the full lifecycle of a Somfy shade group via the HTTP API:
//   - Querying all groups (handleGetGroups)
//   - Reading and updating a single group (handleGroup)
//   - Command dispatch to a group (handleGroupCommand)
//   - Next available group ID scaffold (handleGetNextGroup)
//   - Persisting group sort order (handleGroupSortOrder)
//   - CRUD operations: add, save, delete (handleAddGroup, handleSaveGroup, handleDeleteGroup)
//   - Group send options (handleGroupOptions)
//   - Linking and unlinking shades from groups (handleLinkToGroup, handleUnlinkFromGroup)

#include "WebGroups.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

extern SomfyShadeController somfy;

static const char *s_TAG = "WebGroups";

void WebGroups::begin()
{
    // REST API routes
    registerApiHandler("/groups", [this]() { handleGetGroups(apiServer); });
    registerApiHandler("/group", HTTP_GET, [this]() { handleGroup(apiServer); });
    registerApiHandler("/groupCommand", [this]() { handleGroupCommand(apiServer); });
    // Web UI routes
    registerHandler("/groups", [this]() { handleGetGroups(server); });
    registerHandler("/group", [this]() { handleGroup(server); });
    registerHandler("/groupCommand", [this]() { handleGroupCommand(server); });
    registerHandler("/getNextGroup", [this]() { handleGetNextGroup(server); });
    registerHandler("/addGroup", [this]() { handleAddGroup(server); });
    registerHandler("/groupOptions", [this]() { handleGroupOptions(server); });
    registerHandler("/saveGroup", [this]() { handleSaveGroup(server); });
    registerHandler("/linkToGroup", [this]() { handleLinkToGroup(server); });
    registerHandler("/unlinkFromGroup", [this]() { handleUnlinkFromGroup(server); });
    registerHandler("/deleteGroup", [this]() { handleDeleteGroup(server); });
    registerHandler("/groupSortOrder", [this]() { handleGroupSortOrder(server); });
}

void WebGroups::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebGroups::handleGetGroups(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginArray();
        somfy.toJSONGroups(resp);
        resp.endArray();
        resp.endResponse();
    } else
        server.send(404, ENCODING_TEXT, RESPONSE_404);
}

void WebGroups::handleGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
        if (server.hasArg("groupId")) {
            int groupId = atoi(server.arg("groupId").c_str());
            SomfyGroup *group = somfy.getGroupById(groupId);
            if (group) {
                JsonResponse resp;
                resp.beginResponse(&server, content, sizeof(content));
                resp.beginObject();
                group->toJSON(resp);
                resp.endObject();
                resp.endResponse();
            } else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
        } else {
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
        }
    } else if (method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing group.
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a group");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            if (obj.containsKey("groupId")) {
                SomfyGroup *group = somfy.getGroupById(obj["groupId"]);
                if (group) {
                    group->fromJSON(obj);
                    group->save();
                    JsonResponse resp;
                    resp.beginResponse(&server, content, sizeof(content));
                    resp.beginObject();
                    group->toJSON(resp);
                    resp.endObject();
                    resp.endResponse();
                } else
                    server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
            } else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    } else
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void WebGroups::handleGroupCommand(WebServer &server)
{
    HTTPMethod method = server.method();
    uint8_t groupId = 255;
    int16_t repeat = -1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("groupId")) {
            groupId = atoi(server.arg("groupId").c_str());
            if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
            if (server.hasArg("repeat")) repeat = static_cast<int8_t>(atoi(server.arg("repeat").c_str()));
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Sending Group Command");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            if (obj.containsKey("groupId"))
                groupId = obj["groupId"];
            else {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
                return;
            }
            if (obj.containsKey("command")) {
                String scmd = obj["command"];
                command = translateSomfyCommand(scmd);
            }
            if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
        SomfyGroup *group = somfy.getGroupById(groupId);
        if (group) {
            ESP_LOGI(s_TAG, "Received: %s", server.arg("plain").c_str());
            somfy.enqueueGroupCommand(groupId, command, repeat >= 0 ? (uint8_t)repeat : group->repeats);
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            group->toJSONRef(resp);
            resp.endObject();
            resp.endResponse();
        } else {
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
        }
    } else
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void WebGroups::handleGetNextGroup(WebServer &server)
{
    uint8_t groupId = somfy.getNextGroupId();
    JsonResponse resp;
    resp.beginResponse(&server, content, sizeof(content));
    resp.beginObject();
    resp.addElem("groupId", groupId);
    resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(groupId));
    resp.addElem("bitLength", somfy.transceiver.config.type);
    resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
    resp.endObject();
    resp.endResponse();
}

void WebGroups::handleGroupSortOrder(WebServer &server)
{
    JsonDocument doc;
    ESP_LOGI(s_TAG, "Plain: ");
    ESP_LOGI(s_TAG, "Method: %d", server.method());
    ESP_LOGI(s_TAG, "Plain: %s", server.arg("plain").c_str());
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
                uint8_t groupId = v.as<uint8_t>();
                if (groupId != 255) {
                    SomfyGroup *group = somfy.getGroupById(groupId);
                    if (group) group->sortOrder = order++;
                }
            }
            server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set group order\"}");
        } else {
            server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
        }
    }
}

void WebGroups::handleAddGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    SomfyGroup *group = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Adding a group");
        JsonDocument doc;
        JsonObject obj;
        if (!parseBody(server, doc, obj)) return;
        ESP_LOGI(s_TAG, "Counting shades");
        if (somfy.groupCount() > SOMFY_MAX_GROUPS) {
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of groups exceeded.\"}"));
            return;
        } else {
            ESP_LOGI(s_TAG, "Adding group");
            group = somfy.addGroup(obj);
            if (!group) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Error adding group.\"}"));
                return;
            }
        }
    }
    if (group) {
        JsonResponse resp;
        resp.beginResponse(&server, content, sizeof(content));
        resp.beginObject();
        group->toJSON(resp);
        resp.endObject();
        resp.endResponse();
    } else {
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Group.\"}"));
    }
}

void WebGroups::handleSaveGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a group");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            if (obj.containsKey("groupId")) {
                SomfyGroup *group = somfy.getGroupById(obj["groupId"]);
                if (group) {
                    group->fromJSON(obj);
                    group->save();
                    JsonResponse resp;
                    resp.beginResponse(&server, content, sizeof(content));
                    resp.beginObject();
                    group->toJSON(resp);
                    resp.endObject();
                    resp.endResponse();
                } else
                    server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
            } else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
}

void WebGroups::handleGroupOptions(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_POST) {
        if (server.hasArg("groupId")) {
            int groupId = atoi(server.arg("groupId").c_str());
            SomfyGroup *group = somfy.getGroupById(groupId);
            if (group) {
                JsonResponse resp;
                resp.beginResponse(&server, content, sizeof(content));
                resp.beginObject();
                group->toJSON(resp);
                resp.beginArray("availShades");
                for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
                    SomfyShade *shade = &somfy.shades[i];
                    if (shade->getShadeId() != 255) {
                        bool isLinked = false;
                        for (uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
                            if (group->linkedShades[j] == shade->getShadeId()) {
                                isLinked = true;
                                break;
                            }
                        }
                        if (!isLinked) {
                            resp.beginObject();
                            shade->toJSONRef(resp);
                            resp.endObject();
                        }
                    }
                }
                resp.endArray();
                resp.endObject();
                resp.endResponse();
            } else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
        } else {
            server.send(500, ENCODING_JSON,
                        F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
        }
    }
}

void WebGroups::handleDeleteGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    uint8_t groupId = 255;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("groupId")) {
            groupId = atoi(server.arg("groupId").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Deleting a group");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            if (obj.containsKey("groupId"))
                groupId = obj["groupId"];
            else
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
    }
    SomfyGroup *group = somfy.getGroupById(groupId);
    if (!group)
        server.send(500, ENCODING_JSON,
                    F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
    else {
        somfy.deleteGroup(groupId);
        server.send(200, ENCODING_JSON, F("{\"status\":\"SUCCESS\",\"desc\":\"Group deleted.\"}"));
    }
}

void WebGroups::handleLinkToGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Linking a shade to a group");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
            uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
            if (groupId == 0) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
                return;
            }
            if (shadeId == 0) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
                return;
            }
            SomfyGroup *group = somfy.getGroupById(groupId);
            if (!group) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
                return;
            }
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if (!shade) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
                return;
            }
            group->linkShade(shadeId);
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            group->toJSON(resp);
            resp.endObject();
            resp.endResponse();
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No linking object supplied.\"}"));
    }
}

void WebGroups::handleUnlinkFromGroup(WebServer &server)
{
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Unlinking a shade from a group");
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
            uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
            uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
            if (groupId == 0) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}"));
                return;
            }
            if (shadeId == 0) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}"));
                return;
            }
            SomfyGroup *group = somfy.getGroupById(groupId);
            if (!group) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}"));
                return;
            }
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if (!shade) {
                server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}"));
                return;
            }
            group->unlinkShade(shadeId);
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            group->toJSON(resp);
            resp.endObject();
            resp.endResponse();
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"No unlinking object supplied.\"}"));
    }
}
