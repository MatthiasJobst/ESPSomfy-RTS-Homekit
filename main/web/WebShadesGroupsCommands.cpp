// WebShadesGroupsCommands.cpp — Web handler implementations for cross-domain commands.
//
// Endpoints that target either a shade or a group, selected by shadeId/groupId:
//   - Send/repeat a movement command (handleRepeatCommand)
//   - Send a sun/wind sensor command (handleSetSensor)
//
// Request parsing and JSON/status responses go through a per-handler
// WebJsonResponder (`WebJsonResponder json(server);`): json.parseBody() reads
// the body, and json.respondJson() exposes the writer for
// .object()/.array()/.error()/… responses.

#include "WebShadesGroupsCommands.h"

#include <WebServer.h>
#include "Utils.h"
#include "SomfyShadeController.h"
#include "Web.h"
#include "WebJsonResponder.h"

extern SomfyShadeController somfy;

void WebShadesGroupsCommands::begin()
{
    // REST API routes
    registerApiHandler("/repeatCommand", [this]() { handleRepeatCommand(apiServer); });
    registerApiHandler("/setSensor", [this]() { handleSetSensor(apiServer); });
    // Web UI routes
    registerHandler("/repeatCommand", [this]() { handleRepeatCommand(server); });
    registerHandler("/setSensor", [this]() { handleSetSensor(server); });
}

void WebShadesGroupsCommands::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebShadesGroupsCommands::handleRepeatCommand(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_OPTIONS) {
        server.send(200, "OK");
        return;
    }
    uint8_t shadeId = 255;
    uint8_t groupId = 255;
    uint8_t stepSize = 0;
    int16_t repeat = -1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("shadeId"))
            shadeId = atoi(server.arg("shadeId").c_str());
        else if (server.hasArg("groupId"))
            groupId = atoi(server.arg("groupId").c_str());
        if (server.hasArg("command")) command = translateSomfyCommand(server.arg("command"));
        if (server.hasArg("repeat")) repeat = static_cast<uint8_t>(atoi(server.arg("repeat").c_str()));
        if (server.hasArg("stepSize")) stepSize = static_cast<uint8_t>(atoi(server.arg("stepSize").c_str()));
        if (shadeId == 255 && groupId == 255 && server.hasArg("plain")) {
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
            if (obj.containsKey("groupId")) groupId = obj["groupId"];
            if (obj.containsKey("stepSize")) stepSize = obj["stepSize"];
            if (obj.containsKey("command")) {
                String scmd = obj["command"];
                command = translateSomfyCommand(scmd);
            }
            if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
        }
        if (shadeId != 255) {
            SomfyShade *shade = somfy.getShadeById(shadeId);
            if (!shade) {
                json.respondJson().error("Shade reference could not be found.");
                return;
            }
            shade->sendOrRepeat(command, repeat, stepSize);
            auto arrJson = json.respondJson().array();
            shade->toJSONRef(arrJson);
        } else if (groupId != 255) {
            SomfyGroup *group = somfy.groupController.getGroupById(groupId);
            if (!group) {
                json.respondJson().error("Group reference could not be found.");
                return;
            }
            group->sendOrRepeat(command, repeat, stepSize);
            auto objJson = json.respondJson().object();
            group->toJSONRef(objJson);
        }
    } else {
        json.respondJson().invalidMethod();
    }
}

void WebShadesGroupsCommands::handleSetSensor(WebServer &server)
{
    WebJsonResponder json(server);
    uint8_t shadeId = (server.hasArg("shadeId")) ? static_cast<uint8_t>(atoi(server.arg("shadeId").c_str())) : 255;
    uint8_t groupId = (server.hasArg("groupId")) ? static_cast<uint8_t>(atoi(server.arg("groupId").c_str())) : 255;
    int8_t sunny =
        static_cast<int8_t>((server.hasArg("sunny")) ? toBoolean(server.arg("sunny").c_str(), false) ? 1 : 0 : -1);
    int8_t windy = (server.hasArg("windy")) ? static_cast<int8_t>(atoi(server.arg("windy").c_str())) : int8_t{-1};
    int16_t repeat = (server.hasArg("repeat")) ? static_cast<int16_t>(atoi(server.arg("repeat").c_str())) : int16_t{-1};
    if (server.hasArg("plain")) {
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        if (obj.containsKey("shadeId")) shadeId = obj["shadeId"].as<uint8_t>();
        if (obj.containsKey("groupId")) groupId = obj["groupId"].as<uint8_t>();
        if (obj.containsKey("sunny")) {
            if (obj["sunny"].is<bool>())
                sunny = obj["sunny"].as<bool>() ? 1 : 0;
            else
                sunny = obj["sunny"].as<int8_t>();
        }
        if (obj.containsKey("windy")) {
            if (obj["windy"].is<bool>())
                windy = obj["windy"].as<bool>() ? 1 : 0;
            else
                windy = obj["windy"].as<int8_t>();
        }
        if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
    }
    if (shadeId != 255) {
        SomfyShade *shade = somfy.getShadeById(shadeId);
        if (shade) {
            shade->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : shade->repeats);
            shade->emitState();
            auto objJson = json.respondJson().object();
            shade->toJSON(objJson);
        } else
            json.respondJson().error("An invalid shadeId was provided");

    } else if (groupId != 255) {
        SomfyGroup *group = somfy.groupController.getGroupById(groupId);
        if (group) {
            group->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : group->repeats);
            group->emitState();
            auto objJson = json.respondJson().object();
            group->toJSON(objJson);
        } else
            json.respondJson().error("An invalid groupId was provided");
    } else {
        json.respondJson().error("shadeId was not provided");
    }
}
