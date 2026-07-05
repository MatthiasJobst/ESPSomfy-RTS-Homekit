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
//
// Request parsing and JSON/status responses go through a per-handler
// WebJsonResponder (`WebJsonResponder json(server);`): json.parseBody() reads
// the body, and json.respondJson() exposes the writer for
// .object()/.array()/.error()/.success()/… responses.

#include "WebGroups.h"

#include <esp_log.h>
#include <WebServer.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "Web.h"
#include "WebJsonResponder.h"

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
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        auto arrJson = json.respondJson().array();
        somfy.groupController.toJSONGroups(arrJson);
    } else
        json.respondJson().notFound();
}

void WebGroups::handleGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_GET) {
        if (server.hasArg("groupId")) {
            int groupId = atoi(server.arg("groupId").c_str());
            SomfyGroup *group = somfy.groupController.getGroupById(groupId);
            if (group) {
                auto objJson = json.respondJson().object();
                group->toJSON(objJson);
            } else
                json.respondJson().error("Group Id not found.");
        } else {
            json.respondJson().error("You must supply a valid shade id.");
        }
    } else if (method == HTTP_PUT || method == HTTP_POST) {
        // We are updating an existing group.
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a group");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("groupId")) {
                SomfyGroup *group = somfy.groupController.getGroupById(obj["groupId"]);
                if (group) {
                    group->fromJSON(obj);
                    group->save();
                    auto objJson = json.respondJson().object();
                    group->toJSON(objJson);
                } else
                    json.respondJson().error("Group Id not found.");
            } else
                json.respondJson().error("No group id was supplied.");
        } else
            json.respondJson().error("No group object supplied.");
    } else
        json.respondJson().invalidMethod();
}

void WebGroups::handleGroupCommand(WebServer &server)
{
    WebJsonResponder json(server);
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
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("groupId"))
                groupId = obj["groupId"];
            else {
                json.respondJson().error("No group id was supplied.");
                return;
            }
            if (obj.containsKey("command")) {
                String scmd = obj["command"];
                command = translateSomfyCommand(scmd);
            }
            if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        } else
            json.respondJson().error("No group object supplied.");
        SomfyGroup *group = somfy.groupController.getGroupById(groupId);
        if (group) {
            ESP_LOGI(s_TAG, "Received: %s", server.arg("plain").c_str());
            somfy.commandDispatcher.enqueueGroupCommand(group, command, repeat >= 0 ? (uint8_t)repeat : group->repeats);
            auto objJson = json.respondJson().object();
            group->toJSONRef(objJson);
        } else {
            json.respondJson().error("Group with the specified id not found.");
        }
    } else
        json.respondJson().invalidMethod();
}

void WebGroups::handleGetNextGroup(WebServer &server)
{
    WebJsonResponder json(server);
    uint8_t groupId = somfy.groupController.getNextGroupId();
    auto objJson = json.respondJson().object();
    objJson.addElem("groupId", groupId);
    objJson.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(groupId));
    objJson.addElem("bitLength", somfy.transceiver.config.type);
    objJson.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
}

void WebGroups::handleGroupSortOrder(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Plain: ");
    ESP_LOGI(s_TAG, "Method: %d", server.method());
    ESP_LOGI(s_TAG, "Plain: %s", server.arg("plain").c_str());
    JsonArray arr;
    if (!json.parseBody(arr)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        uint8_t order = 0;
        for (JsonVariant v : arr) {
            uint8_t groupId = v.as<uint8_t>();
            if (groupId != 255) {
                SomfyGroup *group = somfy.groupController.getGroupById(groupId);
                if (group) group->sortOrder = order++;
            }
        }
        json.respondJson().ok("Successfully set group order");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebGroups::handleAddGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    SomfyGroup *group = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Adding a group");
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        ESP_LOGI(s_TAG, "Counting shades");
        if (somfy.groupController.groupCount() >= SOMFY_MAX_GROUPS) {
            json.respondJson().error("Maximum number of groups exceeded.");
            return;
        } else {
            ESP_LOGI(s_TAG, "Adding group");
            group = somfy.groupController.addGroup(obj);
            if (!group) {
                json.respondJson().error("Error adding group.");
                return;
            }
        }
    }
    if (group) {
        auto objJson = json.respondJson().object();
        group->toJSON(objJson);
    } else {
        json.respondJson().error("Error saving Somfy Group.");
    }
}

void WebGroups::handleSaveGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a group");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("groupId")) {
                SomfyGroup *group = somfy.groupController.getGroupById(obj["groupId"]);
                if (group) {
                    group->fromJSON(obj);
                    group->save();
                    auto objJson = json.respondJson().object();
                    group->toJSON(objJson);
                } else
                    json.respondJson().error("Group Id not found.");
            } else
                json.respondJson().error("No group id was supplied.");
        } else
            json.respondJson().error("No group object supplied.");
    }
}

void WebGroups::handleGroupOptions(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_GET || method == HTTP_POST) {
        if (server.hasArg("groupId")) {
            int groupId = atoi(server.arg("groupId").c_str());
            SomfyGroup *group = somfy.groupController.getGroupById(groupId);
            if (group) {
                auto objJson = json.respondJson().object();
                group->toJSON(objJson);
                objJson.beginArray("availShades");
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
                            objJson.beginObject();
                            shade->toJSONRef(objJson);
                            objJson.endObject();
                        }
                    }
                }
                objJson.endArray();
            } else
                json.respondJson().error("Group Id not found.");
        } else {
            json.respondJson().error("You must supply a valid group id.");
        }
    }
}

void WebGroups::handleDeleteGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    uint8_t groupId = 255;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("groupId")) {
            groupId = atoi(server.arg("groupId").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Deleting a group");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("groupId"))
                groupId = obj["groupId"];
            else
                json.respondJson().error("No group id was supplied.");
        } else
            json.respondJson().error("No group object supplied.");
    }
    SomfyGroup *group = somfy.groupController.getGroupById(groupId);
    if (!group)
        json.respondJson().error("Group with the specified id not found.");
    else {
        somfy.groupController.deleteGroup(groupId);
        json.respondJson().success("Group deleted.");
    }
}

void WebGroups::handleLinkToGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Linking a shade to a group");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
            uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
            if (groupId == 0) {
                json.respondJson().error("Group id not provided.");
                return;
            }
            if (shadeId == 0) {
                json.respondJson().error("Shade id not provided.");
                return;
            }
            SomfyGroup *group = somfy.groupController.getGroupById(groupId);
            if (!group) {
                json.respondJson().error("Group id not found.");
                return;
            }
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if (!shade) {
                json.respondJson().error("Shade id not found.");
                return;
            }
            group->linkShade(shadeId);
            auto objJson = json.respondJson().object();
            group->toJSON(objJson);
        } else
            json.respondJson().error("No linking object supplied.");
    }
}

void WebGroups::handleUnlinkFromGroup(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Unlinking a shade from a group");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
            uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
            if (groupId == 0) {
                json.respondJson().error("Group id not provided.");
                return;
            }
            if (shadeId == 0) {
                json.respondJson().error("Shade id not provided.");
                return;
            }
            SomfyGroup *group = somfy.groupController.getGroupById(groupId);
            if (!group) {
                json.respondJson().error("Group id not found.");
                return;
            }
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if (!shade) {
                json.respondJson().error("Shade id not found.");
                return;
            }
            group->unlinkShade(shadeId);
            auto objJson = json.respondJson().object();
            group->toJSON(objJson);
        } else
            json.respondJson().error("No unlinking object supplied.");
    }
}
