// WebShadesGroupsCommands.cpp — Web handler implementations for cross-domain commands.
//
// Endpoints that target either a shade or a group, selected by shadeId/groupId:
//   - Send/repeat a movement command (handleRepeatCommand)
//   - Send a sun/wind sensor command (handleSetSensor)

#include "WebShadesGroupsCommands.h"

#include <WebServer.h>
#include "Utils.h"
#include "SomfyShadeController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

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
            JsonDocument doc;
            JsonObject obj;
            if (!parseBody(server, doc, obj)) return;
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
                server.send(500, ENCODING_JSON,
                            F("{\"status\":\"ERROR\",\"desc\":\"Shade reference could not be found.\"}"));
                return;
            }
            if (shade->shadeType == shade_types::garage1 && command == somfy_commands::Prog)
                command = somfy_commands::Toggle;
            if (!shade->isLastCommand(command)) {
                // We are going to send this as a new command.
                shade->sendCommand(command, repeat >= 0 ? repeat : shade->repeats, stepSize);
            } else {
                shade->repeatFrame(repeat >= 0 ? repeat : shade->repeats);
            }
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginArray();
            shade->toJSONRef(resp);
            resp.endArray();
            resp.endResponse();
        } else if (groupId != 255) {
            SomfyGroup *group = somfy.getGroupById(groupId);
            if (!group) {
                server.send(500, ENCODING_JSON,
                            F("{\"status\":\"ERROR\",\"desc\":\"Group reference could not be found.\"}"));
                return;
            }
            if (!group->isLastCommand(command)) {
                // We are going to send this as a new command.
                group->sendCommand(command, repeat >= 0 ? repeat : group->repeats, stepSize);
            } else
                group->repeatFrame(repeat >= 0 ? repeat : group->repeats);
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            group->toJSONRef(resp);
            resp.endObject();
            resp.endResponse();
        }
    } else {
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
    }
}

void WebShadesGroupsCommands::handleSetSensor(WebServer &server)
{
    uint8_t shadeId = (server.hasArg("shadeId")) ? static_cast<uint8_t>(atoi(server.arg("shadeId").c_str())) : 255;
    uint8_t groupId = (server.hasArg("groupId")) ? static_cast<uint8_t>(atoi(server.arg("groupId").c_str())) : 255;
    int8_t sunny =
        static_cast<int8_t>((server.hasArg("sunny")) ? toBoolean(server.arg("sunny").c_str(), false) ? 1 : 0 : -1);
    int8_t windy = (server.hasArg("windy")) ? static_cast<int8_t>(atoi(server.arg("windy").c_str())) : int8_t{-1};
    int16_t repeat = (server.hasArg("repeat")) ? static_cast<int16_t>(atoi(server.arg("repeat").c_str())) : int16_t{-1};
    if (server.hasArg("plain")) {
        JsonDocument doc;
        JsonObject obj;
        if (!parseBody(server, doc, obj)) return;
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
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            shade->toJSON(resp);
            resp.endObject();
            resp.endResponse();
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"An invalid shadeId was provided\"}"));

    } else if (groupId != 255) {
        SomfyGroup *group = somfy.getGroupById(groupId);
        if (group) {
            group->sendSensorCommand(windy, sunny, repeat >= 0 ? (uint8_t)repeat : group->repeats);
            group->emitState();
            JsonResponse resp;
            resp.beginResponse(&server, content, sizeof(content));
            resp.beginObject();
            group->toJSON(resp);
            resp.endObject();
            resp.endResponse();
        } else
            server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"An invalid groupId was provided\"}"));
    } else {
        server.send(500, ENCODING_JSON, F("{\"status\":\"ERROR\",\"desc\":\"shadeId was not provided\"}"));
    }
}
