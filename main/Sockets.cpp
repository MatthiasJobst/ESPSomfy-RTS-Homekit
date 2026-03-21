#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "Sockets.h"
#include "ConfigSettings.h"
#include "SomfyController.h"
#include "SomfyNetwork.h"
#include "GitOTA.h"

extern ConfigSettings settings;
extern SomfyNetwork net;
extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern GitUpdater git;

static const char *TAG = "Sockets";
uint16_t socketsPort = 8080;

WebSocketsServer sockServer = WebSocketsServer(socketsPort);

#define MAX_SOCK_RESPONSE 2048
static char g_response[MAX_SOCK_RESPONSE];

bool room_t::isJoined(uint8_t num) {
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == num) return true; 
  } 
  return false; 
}
bool room_t::join(uint8_t num) {
  if(this->isJoined(num)) return true; 
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == 255) { 
      this->clients[i] = num; 
      return true; 
    } 
  }
  return false;  
}
bool room_t::leave(uint8_t num) { 
  if(!this->isJoined(num)) return false; 
  for(uint8_t i = 0; i < sizeof(this->clients); i++) { 
    if(this->clients[i] == num) this->clients[i] = 255; 
  } 
  return true;
}
void room_t::clear() {
  memset(this->clients, 255, sizeof(this->clients));
}
uint8_t room_t::activeClients() {
  uint8_t n = 0;
  for(uint8_t i = 0; i < sizeof(this->clients); i++) {
    if(this->clients[i] != 255) n++;
  }
  return n;
}

/*********************************************************************
 * SocketEmitter class members
 ********************************************************************/
void SocketEmitter::startup() {
  
}
void SocketEmitter::begin() {
  sockServer.begin();
  sockServer.enableHeartbeat(20000, 10000, 3);
  sockServer.onEvent(this->wsEvent);
  ESP_LOGI(TAG, "Socket Server Started on port %d", socketsPort);
}
void SocketEmitter::loop() {
  this->initClients();
  sockServer.loop();
}
JsonSockEvent *SocketEmitter::beginEmit(const char *evt) {
  this->json.beginEvent(&sockServer, evt, g_response, sizeof(g_response));
  return &this->json;
}
void SocketEmitter::endEmit(uint8_t num) { this->json.endEvent(num); sockServer.loop(); }
void SocketEmitter::endEmitRoom(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) {
    room_t *r = &this->rooms[room];
    for(uint8_t i = 0; i < sizeof(r->clients); i++) {
      if(r->clients[i] != 255) this->json.endEvent(r->clients[i]);
    }
  }
}
uint8_t SocketEmitter::activeClients(uint8_t room) {
  if(room < SOCK_MAX_ROOMS) return this->rooms[room].activeClients();
  return 0;
}
void SocketEmitter::initClients() {
  for(uint8_t i = 0; i < sizeof(this->newClients); i++) {
    uint8_t num = this->newClients[i];
    if(num != 255) {
      if(sockServer.clientIsConnected(num)) {
        ESP_LOGI(TAG, "Initializing Socket Client %u", num);
        esp_task_wdt_reset();
        settings.emitSockets(num);
        somfy.emitState(num);
        git.emitUpdateCheck(num);
        net.emitSockets(num);
        esp_task_wdt_reset();
      }
      this->newClients[i] = 255;
    }
  }
}
void SocketEmitter::delayInit(uint8_t num) {
  for(uint8_t i=0; i < sizeof(this->newClients); i++) {
    if(this->newClients[i] == num) break;
    else if(this->newClients[i] == 255) {
      this->newClients[i] = num;
      break;
    }
  }
}
void SocketEmitter::end() { 
  sockServer.close(); 
  for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++)
    this->rooms[i].clear();
}
void SocketEmitter::disconnect() { sockServer.disconnect(); }
void SocketEmitter::wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch(type) {
        case WStype_ERROR:
            if(length > 0)
              ESP_LOGE(TAG, "Socket Error: %s", payload);
            else
              ESP_LOGE(TAG, "Socket Error: \n");
            break;
        case WStype_DISCONNECTED:
            if(length > 0)
              ESP_LOGI(TAG, "WS [%u] Disconnected! [%s]", num, payload);
            else
              ESP_LOGI(TAG, "WS [%u] Disconnected!", num);
            for(uint8_t i = 0; i < SOCK_MAX_ROOMS; i++) {
              sockEmit.rooms[i].leave(num);
            }
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = sockServer.remoteIP(num);
                ESP_LOGI(TAG, "WS [%u] Connected from %d.%d.%d.%d url: %s", num, ip[0], ip[1], ip[2], ip[3], payload);
                // Send all the current shade settings to the client.
                sockServer.sendTXT(num, "Connected");
                sockEmit.delayInit(num);
            }
            break;
        case WStype_TEXT:
            if(strncmp((char *)payload, "join:", 5) == 0) {
              // In this instance the client wants to join a room.  Let's do some
              // work to get the ordinal of the room that the client wants to join.
              uint8_t roomNum = atoi((char *)&payload[5]);
              ESP_LOGI(TAG, "Client %u joining room %u", num, roomNum);
              if(roomNum < SOCK_MAX_ROOMS) sockEmit.rooms[roomNum].join(num);
            }
            else if(strncmp((char *)payload, "leave:", 6) == 0) {
              uint8_t roomNum = atoi((char *)&payload[6]);
              ESP_LOGI(TAG, "Client %u leaving room %u", num, roomNum);
              if(roomNum < SOCK_MAX_ROOMS) sockEmit.rooms[roomNum].leave(num);
            }
            else {
              ESP_LOGI(TAG, "Socket [%u] text: %s", num, payload);
            }
            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // sockServer.broadcastTXT("message here");
            break;
        case WStype_BIN:
            ESP_LOGI(TAG, "[%u] get binary length: %u", num, length);
            //hexdump(payload, length);

            // send message to client
            // sockServer.sendBIN(num, payload, length);
            break;
          case WStype_PING:
              ESP_LOGI(TAG, "WS [%u] Ping received", num);
              break;
          case WStype_PONG:
              ESP_LOGI(TAG, "WS [%u] Pong received", num);
              break;
        default:
            ESP_LOGI(TAG, "WS [%u] Unhandled Event: %d", num, type);
            break;
    }  
}
