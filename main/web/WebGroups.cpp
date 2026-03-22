// WebGroups.cpp — Web handler implementations for group management.
//
// Covers the full lifecycle of a Somfy shade group via the HTTP API:
//   - Querying all groups (handleGetGroups)
//   - Reading and updating a single group (handleGroup)
//   - Next available group ID scaffold (handleGetNextGroup)
//   - Persisting group sort order (handleGroupSortOrder)
//   - CRUD operations: add, save, delete (handleAddGroup, handleSaveGroup, handleDeleteGroup)
//   - Group send options (handleGroupOptions)
//   - Linking and unlinking shades from groups (handleLinkToGroup, handleUnlinkFromGroup)

#include <WebServer.h>
#include <esp_log.h>
#include "ConfigSettings.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "WebHelpers.h"

extern SomfyShadeController somfy;
extern Web webServer;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];
extern const char _response_404[];

static const char *TAG = "WebGroups";

void Web::handleGetGroups(WebServer &server) {
    HTTPMethod method = server.method();
    if (method == HTTP_POST || method == HTTP_GET) {
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginArray();
      somfy.toJSONGroups(resp);
      resp.endArray();
      resp.endResponse();
    }
    else server.send(404, _encoding_text, _response_404);
}

void Web::handleGroup(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_GET) {
    if (server.hasArg("groupId")) {
      int groupId = atoi(server.arg("groupId").c_str());
      SomfyGroup* group = somfy.getGroupById(groupId);
      if (group) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        group->toJSON(resp);
        resp.endObject();
        resp.endResponse();
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid shade id.\"}"));
    }
  }
  else if (method == HTTP_PUT || method == HTTP_POST) {
      // We are updating an existing group.
      if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Updating a group");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("groupId")) {
        SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
        if (group) {
          group->fromJSON(obj);
          group->save();
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          group->toJSON(resp);
          resp.endObject();
          resp.endResponse();
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
  }
  else
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Invalid Http method\"}"));
}

void Web::handleGetNextGroup(WebServer &server) {
  uint8_t groupId = somfy.getNextGroupId();
  JsonResponse resp;
  resp.beginResponse(&server, g_content, sizeof(g_content));
  resp.beginObject();
  resp.addElem("groupId", groupId);
  resp.addElem("remoteAddress", (uint32_t)somfy.getNextRemoteAddress(groupId));
  resp.addElem("bitLength", somfy.transceiver.config.type);
  resp.addElem("proto", static_cast<uint8_t>(somfy.transceiver.config.proto));
  resp.endObject();
  resp.endResponse();
}


void Web::handleGroupSortOrder(WebServer &server) {
  JsonDocument doc;
  ESP_LOGI(TAG, "Plain: ");
  ESP_LOGI(TAG, "Method: %d", server.method());
  ESP_LOGI(TAG, "Plain: %s", server.arg("plain").c_str());
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
        uint8_t groupId = v.as<uint8_t>();
        if (groupId != 255) {
          SomfyGroup *group = somfy.getGroupById(groupId);
          if(group) group->sortOrder = order++;
        }
      }
      server.send(200, "application/json", "{\"status\":\"OK\",\"desc\":\"Successfully set group order\"}");
    }
    else {
      server.send(201, "application/json", "{\"status\":\"ERROR\",\"desc\":\"Invalid HTTP Method: \"}");
    }
  }
}


void Web::handleAddGroup(WebServer &server) {
  HTTPMethod method = server.method();
  SomfyGroup * group = nullptr;
  if (method == HTTP_POST || method == HTTP_PUT) {
    ESP_LOGI(TAG, "Adding a group");
    JsonDocument doc; JsonObject obj;
    if (!parseBody(server, doc, obj)) return;
    ESP_LOGI(TAG, "Counting shades");
    if (somfy.groupCount() > SOMFY_MAX_GROUPS) {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Maximum number of groups exceeded.\"}"));
      return;
    }
    else {
      ESP_LOGI(TAG, "Adding group");
      group = somfy.addGroup(obj);
      if (!group) {
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error adding group.\"}"));
        return;
      }
    }
  }
  if (group) {
    JsonResponse resp;
    resp.beginResponse(&server, g_content, sizeof(g_content));
    resp.beginObject();
    group->toJSON(resp);
    resp.endObject();
    resp.endResponse();
  }
  else {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error saving Somfy Group.\"}"));
  }
}

void Web::handleSaveGroup(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Updating a group");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("groupId")) {
        SomfyGroup* group = somfy.getGroupById(obj["groupId"]);
        if (group) {
          group->fromJSON(obj);
          group->save();
          JsonResponse resp;
          resp.beginResponse(&server, g_content, sizeof(g_content));
          resp.beginObject();
          group->toJSON(resp);
          resp.endObject();
          resp.endResponse();
        }
        else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
  }
}

void Web::handleGroupOptions(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_GET || method == HTTP_POST) {
    if (server.hasArg("groupId")) {
      int groupId = atoi(server.arg("groupId").c_str());
      SomfyGroup* group = somfy.getGroupById(groupId);
      if (group) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        group->toJSON(resp);
        resp.beginArray("availShades");
        for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
          SomfyShade *shade = &somfy.shades[i];
          if(shade->getShadeId() != 255) {
            bool isLinked = false;
            for(uint8_t j = 0; j < SOMFY_MAX_GROUPED_SHADES; j++) {
              if(group->linkedShades[j] == shade->getShadeId()) {
                isLinked = true;
                break;
              }
            }
            if(!isLinked) {
              resp.beginObject();
              shade->toJSONRef(resp);
              resp.endObject();
            }
          }
        }
        resp.endArray();
        resp.endObject();
        resp.endResponse();
      }
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group Id not found.\"}"));
    }
    else {
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"You must supply a valid group id.\"}"));
    }
  }
}

void Web::handleDeleteGroup(WebServer &server) {
  HTTPMethod method = server.method();
  uint8_t groupId = 255;
  if (method == HTTP_GET || method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("groupId")) {
      groupId = atoi(server.arg("groupId").c_str());
    }
    else if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Deleting a group");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      if (obj.containsKey("groupId")) groupId = obj["groupId"];
      else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group id was supplied.\"}"));
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No group object supplied.\"}"));
  }
  SomfyGroup * group = somfy.getGroupById(groupId);
  if (!group) server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group with the specified id not found.\"}"));
  else {
    somfy.deleteGroup(groupId);
    server.send(200, _encoding_json, F("{\"status\":\"SUCCESS\",\"desc\":\"Group deleted.\"}"));
  }
}

void Web::handleLinkToGroup(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Linking a shade to a group");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
      uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
      if(groupId == 0) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}")); return; }
      if(shadeId == 0) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}")); return; }
      SomfyGroup * group = somfy.getGroupById(groupId);
      if(!group) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}")); return; }
      SomfyShade * shade = somfy.getShadeById(shadeId);
      if(!shade) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}")); return; }
      group->linkShade(shadeId);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No linking object supplied.\"}"));
  }
}

void Web::handleUnlinkFromGroup(WebServer &server) {
  HTTPMethod method = server.method();
  if (method == HTTP_PUT || method == HTTP_POST) {
    if (server.hasArg("plain")) {
      ESP_LOGI(TAG, "Unlinking a shade from a group");
      JsonDocument doc; JsonObject obj;
      if (!parseBody(server, doc, obj)) return;
      uint8_t shadeId = obj.containsKey("shadeId") ? obj["shadeId"] : 0;
      uint8_t groupId = obj.containsKey("groupId") ? obj["groupId"] : 0;
      if(groupId == 0) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not provided.\"}")); return; }
      if(shadeId == 0) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not provided.\"}")); return; }
      SomfyGroup * group = somfy.getGroupById(groupId);
      if(!group) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Group id not found.\"}")); return; }
      SomfyShade * shade = somfy.getShadeById(shadeId);
      if(!shade) { server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Shade id not found.\"}")); return; }
      group->unlinkShade(shadeId);
      JsonResponse resp;
      resp.beginResponse(&server, g_content, sizeof(g_content));
      resp.beginObject();
      group->toJSON(resp);
      resp.endObject();
      resp.endResponse();
    }
    else server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"No unlinking object supplied.\"}"));
  }
}


