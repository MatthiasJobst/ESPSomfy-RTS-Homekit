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
//   - Next available shade ID scaffold (handleGetNextShade)
//   - Persisting shade sort order (handleShadeSortOrder)
//
// Request parsing and JSON/status responses go through a per-handler
// WebJsonResponder (`WebJsonResponder json(server);`): json.parseBody() reads
// the body, and json.respondJson() exposes the writer for
// .object()/.array()/.error()/… responses. The requireShade()/sendShadeJSON()
// helpers take that responder so all output flows through one facade.

#include "WebShades.h"

#include <esp_log.h>
#include <WebServer.h>
#include "Utils.h"
#include "SomfyShadeController.h"
#include "Web.h"
#include "WebJsonResponder.h"

extern SomfyShadeController somfy;

static const char *s_TAG = "WebShades";

void WebShades::begin()
{
    // REST API routes
    registerApiHandler("/shades", [this]() { handleGetShades(apiServer); });
    registerApiHandler("/shade", HTTP_GET, [this]() { handleShade(apiServer); });
    registerApiHandler("/shadeCommand", [this]() { handleShadeCommand(apiServer); });
    registerApiHandler("/tiltCommand", [this]() { handleTiltCommand(apiServer); });
    registerApiHandler("/setPositions", [this]() { handleSetPositions(apiServer); });
    // Web UI routes
    registerHandler("/shades", [this]() { handleGetShades(server); });
    registerHandler("/shade", [this]() { handleShade(server); });
    registerHandler("/shadeCommand", [this]() { handleShadeCommand(server); });
    registerHandler("/tiltCommand", [this]() { handleTiltCommand(server); });
    registerHandler("/setPositions", [this]() { handleSetPositions(server); });
    registerHandler("/getNextShade", [this]() { handleGetNextShade(server); });
    registerHandler("/shadeSortOrder", [this]() { handleShadeSortOrder(server); });
    registerHandler("/addShade", [this]() { handleAddShade(server); });
    registerHandler("/saveShade", [this]() { handleSaveShade(server); });
    registerHandler("/deleteShade", [this]() { handleDeleteShade(server); });
    registerHandler("/setMyPosition", [this]() { handleSetMyPosition(server); });
    registerHandler("/setRollingCode", [this]() { handleSetRollingCode(server); });
    registerHandler("/setPaired", [this]() { handleSetPaired(server); });
    registerHandler("/unpairShade", [this]() { handleUnpairShade(server); });
    registerHandler("/linkRepeater", [this]() { handleLinkRepeater(server); });
    registerHandler("/unlinkRepeater", [this]() { handleUnlinkRepeater(server); });
    registerHandler("/linkRemote", [this]() { handleLinkRemote(server); });
    registerHandler("/unlinkRemote", [this]() { handleUnlinkRemote(server); });
}

void WebShades::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

// ---------------------------------------------------------------------------
// Private helpers — shared by the handlers in this module (declared in WebShades.h)
// ---------------------------------------------------------------------------

// Look up a shade by id. Sends a 500 error and returns nullptr if not found.
SomfyShade *WebShades::requireShade(WebJsonResponder &json, uint8_t shadeId)
{
    SomfyShade *shade = somfy.getShadeById(shadeId);
    if (!shade) json.respondJson().error("Shade not found.");
    return shade;
}

// Send the shade's JSON state as the HTTP response.
// Pass ref=true to use toJSONRef() (minimal reference fields) instead of toJSON().
void WebShades::sendShadeJSON(WebJsonResponder &json, SomfyShade *shade, bool ref)
{
    auto objJson = json.respondJson().object();
    if (ref)
        shade->toJSONRef(objJson);
    else
        shade->toJSON(objJson);
}

// ---------------------------------------------------------------------------

void WebShades::handleGetShades(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
        auto arrJson = json.respondJson().array();
        somfy.toJSONShades(arrJson);
    } else
        json.respondJson().notFound();
}

void WebShades::handleShadeCommand(WebServer &server)
{
    WebJsonResponder json(server);
    if (server.method() == HTTP_OPTIONS) {
        server.send(200, "OK");
        return;
    }
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    uint8_t stepSize = 0;
    int16_t repeat = -1;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("shadeId")) {
            shadeId = atoi(server.arg("shadeId").c_str());
            if (server.hasArg("command"))
                command = translateSomfyCommand(server.arg("command"));
            else if (server.hasArg("target"))
                target = atoi(server.arg("target").c_str());
            if (server.hasArg("repeat")) repeat = static_cast<int8_t>(atoi(server.arg("repeat").c_str()));
            if (server.hasArg("stepSize")) stepSize = atoi(server.arg("stepSize").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Sending Shade Command");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId"))
                shadeId = obj["shadeId"];
            else
                json.respondJson().error("No shade id was supplied.");
            if (obj.containsKey("command")) {
                String scmd = obj["command"];
                command = translateSomfyCommand(scmd);
            } else if (obj.containsKey("target"))
                target = obj["target"].as<uint8_t>();
            if (obj.containsKey("repeat")) repeat = obj["repeat"].as<uint8_t>();
            if (obj.containsKey("stepSize")) stepSize = obj["stepSize"].as<uint8_t>();
        } else
            json.respondJson().error("No shade object supplied.");
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            ESP_LOGI(s_TAG, "Received: %s", server.arg("plain").c_str());
            if (target <= 100)
                somfy.enqueueShadeTarget(shadeId, shade->transformPosition(target));
            else
                somfy.enqueueShadeCommand(shadeId, command, repeat > 0 ? repeat : shade->repeats, stepSize);
            sendShadeJSON(json, shade, true);
        }
    } else
        json.respondJson().invalidMethod();
}

void WebShades::handleTiltCommand(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    uint8_t target = 255;
    somfy_commands command = somfy_commands::My;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("shadeId")) {
            shadeId = atoi(server.arg("shadeId").c_str());
            if (server.hasArg("command"))
                command = translateSomfyCommand(server.arg("command"));
            else if (server.hasArg("target"))
                target = atoi(server.arg("target").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Sending Shade Tilt Command");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId"))
                shadeId = obj["shadeId"];
            else
                json.respondJson().error("No shade id was supplied.");
            if (obj.containsKey("command")) {
                String scmd = obj["command"];
                command = translateSomfyCommand(scmd);
            } else if (obj.containsKey("target"))
                target = obj["target"].as<uint8_t>();
        } else
            json.respondJson().error("No shade object supplied.");
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            ESP_LOGI(s_TAG, "Received: %s", server.arg("plain").c_str());
            if (target <= 100)
                somfy.enqueueShadeTiltTarget(shadeId, shade->transformPosition(target));
            else
                somfy.enqueueShadeTiltCommand(shadeId, command);
            sendShadeJSON(json, shade, true);
        }
    } else
        json.respondJson().invalidMethod();
}

void WebShades::handleShade(WebServer &server)
{
    WebJsonResponder json(server);
    if (server.method() == HTTP_GET) {
        if (server.hasArg("shadeId")) {
            SomfyShade *shade = requireShade(json, atoi(server.arg("shadeId").c_str()));
            if (shade) sendShadeJSON(json, shade);
        } else {
            json.respondJson().error("You must supply a valid shade id.");
        }
    } else
        json.respondJson().notFound();
}

void WebShades::handleSetPositions(WebServer &server)
{
    WebJsonResponder json(server);
    uint8_t shadeId = (server.hasArg("shadeId")) ? atoi(server.arg("shadeId").c_str()) : 255;
    int8_t pos = (server.hasArg("position")) ? static_cast<int8_t>(atoi(server.arg("position").c_str())) : int8_t{-1};
    int8_t tiltPos =
        (server.hasArg("tiltPosition")) ? static_cast<int8_t>(atoi(server.arg("tiltPosition").c_str())) : int8_t{-1};
    if (server.hasArg("plain")) {
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        if (obj.containsKey("position")) pos = obj["position"].as<int8_t>();
        if (obj.containsKey("tiltPosition")) tiltPos = obj["tiltPosition"].as<int8_t>();
    }
    if (shadeId != 255) {
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            if (pos >= 0) shade->target = shade->currentPos = pos;
            if (tiltPos >= 0 && shade->tiltType != tilt_types::none)
                shade->tiltTarget = shade->currentTiltPos = tiltPos;
            shade->emitState();
            sendShadeJSON(json, shade);
        }
    } else {
        json.respondJson().error("shadeId was not provided");
    }
}

void WebShades::handleGetNextShade(WebServer &server)
{
    WebJsonResponder json(server);
    uint8_t shadeId = somfy.getNextShadeId();
    auto objJson = json.respondJson().object();
    objJson.addElem("shadeId", shadeId);
    objJson.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(shadeId));
    objJson.addElem("bitLength", somfy.transceiver.config.type);
    objJson.addElem("stepSize", (uint8_t)100);
    objJson.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
}

void WebShades::handleShadeSortOrder(WebServer &server)
{
    WebJsonResponder json(server);
    ESP_LOGI(s_TAG, "Plain: %s", server.arg("plain").c_str());
    JsonArray arr;
    if (!json.parseBody(arr)) return;
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_PUT) {
        uint8_t order = 0;
        for (JsonVariant v : arr) {
            uint8_t shadeId = v.as<uint8_t>();
            if (shadeId != 255) {
                SomfyShade *shade = somfy.getShadeById(shadeId);
                if (shade) shade->sortOrder = order++;
            }
        }
        json.respondJson().ok("Successfully set shade order");
    } else {
        json.respondJson().error("Invalid HTTP Method: ", 403);
    }
}

void WebShades::handleAddShade(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    SomfyShade *shade = nullptr;
    if (method == HTTP_POST || method == HTTP_PUT) {
        ESP_LOGI(s_TAG, "Adding a shade");
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        ESP_LOGI(s_TAG, "Counting shades");
        if (somfy.shadeCount() > SOMFY_MAX_SHADES) {
            json.respondJson().error("Maximum number of shades exceeded.");
            return;
        }
        ESP_LOGI(s_TAG, "Adding shade");
        shade = somfy.addShade(obj);
        if (!shade) {
            json.respondJson().error("Error adding shade.");
            return;
        }
    }
    if (shade)
        sendShadeJSON(json, shade);
    else
        json.respondJson().error("Error saving Somfy Shade.");
}

void WebShades::handleSaveShade(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Updating a shade");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId")) {
                SomfyShade *shade = requireShade(json, obj["shadeId"].as<uint8_t>());
                if (shade) {
                    int8_t err = shade->fromJSON(obj);
                    if (err == 0) {
                        shade->save();
                        sendShadeJSON(json, shade);
                    } else {
                        char buf[96];
                        snprintf(buf, sizeof(buf),
                                 "{\"status\":\"DATA\",\"desc\":\"Data Error.\", \"code\":%d}", err);
                        server.send(500, ENCODING_JSON, buf);
                    }
                }
            } else
                json.respondJson().error("No shade id was supplied.");
        } else
            json.respondJson().error("No shade object supplied.");
    }
}

void WebShades::handleDeleteShade(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("shadeId")) {
            shadeId = atoi(server.arg("shadeId").c_str());
        } else if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "Deleting a shade");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId"))
                shadeId = obj["shadeId"];
            else
                json.respondJson().error("No shade id was supplied.");
        } else
            json.respondJson().error("No shade object supplied.");
    }
    SomfyShade *shade = requireShade(json, shadeId);
    if (!shade) return;
    if (shade->isInGroup())
        json.respondJson().error("This shade is a member of a group and cannot be deleted.", 400);
    else {
        somfy.deleteShade(shadeId);
        json.respondJson().success("Shade deleted.");
    }
}

void WebShades::handleSetMyPosition(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    uint8_t shadeId = 255;
    int8_t pos = -1;
    int8_t tilt = -1;
    if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("shadeId")) {
            shadeId = atoi(server.arg("shadeId").c_str());
            if (server.hasArg("pos")) pos = static_cast<int8_t>(atoi(server.arg("pos").c_str()));
            if (server.hasArg("tilt")) tilt = static_cast<int8_t>(atoi(server.arg("tilt").c_str()));
        } else if (server.hasArg("plain")) {
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId"))
                shadeId = obj["shadeId"];
            else
                json.respondJson().error("No shade id was supplied.");
            if (obj.containsKey("pos")) pos = obj["pos"].as<int8_t>();
            if (obj.containsKey("tilt")) tilt = obj["tilt"].as<int8_t>();
        } else
            json.respondJson().error("No shade object supplied.");
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            if (tilt < 0) tilt = static_cast<int8_t>(shade->getMyPos());
            if (shade->tiltType == tilt_types::none) tilt = -1;
            if (pos >= 0 && pos <= 100)
                shade->setMyPosition(shade->transformPosition(pos), shade->transformPosition(tilt));
            sendShadeJSON(json, shade, true);
        }
    } else
        json.respondJson().invalidMethod();
}

void WebShades::handleSetRollingCode(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        uint8_t shadeId = 255;
        uint16_t rollingCode = 0;
        if (server.hasArg("plain")) {
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
            if (obj.containsKey("rollingCode")) rollingCode = obj["rollingCode"];
        } else if (server.hasArg("shadeId")) {
            shadeId = atoi(server.arg("shadeId").c_str());
            rollingCode = atoi(server.arg("rollingCode").c_str());
        }
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            shade->setRollingCode(rollingCode);
            sendShadeJSON(json, shade);
        }
    }
}

void WebShades::handleSetPaired(WebServer &server)
{
    WebJsonResponder json(server);
    uint8_t shadeId = 255;
    bool paired = false;
    if (server.hasArg("plain")) {
        JsonObject obj;
        if (!json.parseBody(obj)) return;
        if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        if (obj.containsKey("paired")) paired = obj["paired"];
    } else if (server.hasArg("shadeId"))
        shadeId = atoi(server.arg("shadeId").c_str());
    if (server.hasArg("paired")) paired = toBoolean(server.arg("paired").c_str(), false);
    SomfyShade *shade = requireShade(json, shadeId);
    if (shade) {
        shade->paired = paired;
        shade->save();
        sendShadeJSON(json, shade);
    }
}

void WebShades::handleUnpairShade(WebServer &server)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        uint8_t shadeId = 255;
        if (server.hasArg("plain")) {
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId")) shadeId = obj["shadeId"];
        } else if (server.hasArg("shadeId"))
            shadeId = atoi(server.arg("shadeId").c_str());
        SomfyShade *shade = requireShade(json, shadeId);
        if (shade) {
            shade->sendCommand(somfy_commands::Prog, shade->bitLength == 56 ? 7 : 1);
            shade->paired = false;
            shade->save();
            sendShadeJSON(json, shade);
        }
    }
}

void WebShades::repeaterLinkOp(WebServer &server, bool link)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        uint32_t address = 0;
        if (server.hasArg("plain")) {
            ESP_LOGI(s_TAG, "%s a repeater", link ? "Linking" : "Unlinking");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("address"))
                address = obj["address"];
            else if (obj.containsKey("remoteAddress"))
                address = obj["remoteAddress"];
        } else if (server.hasArg("address"))
            address = atoi(server.arg("address").c_str());
        if (address == 0)
            json.respondJson().error("No repeater address was supplied.");
        else {
            if (link)
                somfy.linkRepeater(address);
            else
                somfy.unlinkRepeater(address);
            auto arrJson = json.respondJson().array();
            somfy.toJSONRepeaters(arrJson);
        }
    }
}

void WebShades::handleLinkRepeater(WebServer &server)
{
    repeaterLinkOp(server, true);
}

void WebShades::handleUnlinkRepeater(WebServer &server)
{
    repeaterLinkOp(server, false);
}

void WebShades::remoteLinkOp(WebServer &server, bool link)
{
    WebJsonResponder json(server);
    HTTPMethod method = server.method();
    if (method == HTTP_PUT || method == HTTP_POST) {
        if (server.hasArg("plain")) {
            if (link) ESP_LOGI(s_TAG, "Linking a remote");
            JsonObject obj;
            if (!json.parseBody(obj)) return;
            if (obj.containsKey("shadeId")) {
                SomfyShade *shade = requireShade(json, obj["shadeId"].as<uint8_t>());
                if (shade) {
                    if (obj.containsKey("remoteAddress")) {
                        if (link) {
                            if (obj.containsKey("rollingCode"))
                                shade->linkRemote(obj["remoteAddress"], obj["rollingCode"]);
                            else
                                shade->linkRemote(obj["remoteAddress"]);
                        } else {
                            shade->unlinkRemote(obj["remoteAddress"]);
                        }
                    } else {
                        json.respondJson().error("Remote address not provided.");
                    }
                    sendShadeJSON(json, shade);
                }
            } else
                json.respondJson().error("No shade id was supplied.");
        } else
            json.respondJson().error("No remote object supplied.");
    }
}

void WebShades::handleLinkRemote(WebServer &server)
{
    remoteLinkOp(server, true);
}

void WebShades::handleUnlinkRemote(WebServer &server)
{
    remoteLinkOp(server, false);
}
