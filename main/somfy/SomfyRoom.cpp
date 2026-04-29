// SomfyRoom.cpp — SomfyRoom: NVS persistence, JSON serialisation, and socket-emit.
#include "SomfyRoom.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "esp_log.h"

static const char *TAG = "SomfyRoom";

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;

void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}

bool SomfyRoom::save() { somfy.commit(); return true; }

bool SomfyRoom::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("sortOrder")) this->sortOrder = obj["sortOrder"].as<int8_t>();
  return true;
}

void SomfyRoom::toJSON(JsonResponse &json) {
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("sortOrder", this->sortOrder);
}

void SomfyRoom::emitState(const char *evt) { this->emitState(255, evt); }

void SomfyRoom::emitState(uint8_t num, const char *evt) {
  ESP_LOGD(TAG, "Emiting state %u", num);
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("roomId", this->roomId);
  json->addElem("name", this->name);
  json->addElem("sortOrder", this->sortOrder);
  json->endObject();
  sockEmit.endEmit(num);
}

void SomfyRoom::publish() {}
void SomfyRoom::unpublish() {}
