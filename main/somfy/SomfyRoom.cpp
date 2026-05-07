// SomfyRoom.cpp — SomfyRoom: NVS persistence, JSON serialisation, MQTT
// publishing and socket-emit for named rooms.
#include "SomfyRoom.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "MQTT.h"
#include "esp_log.h"

static const char *TAG = "SomfyRoom";

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern MQTTClass mqtt;

void SomfyRoom::clear() {
  this->roomId = 0;
  this->sortOrder = 0;
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
  this->publish();
}

void SomfyRoom::publish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.publish(topic, this->roomId, true);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.publish(topic, this->name, true);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.publish(topic, this->sortOrder, true);
  }
}

void SomfyRoom::unpublish() {
  if(mqtt.connected()) {
    char topic[64];
    sprintf(topic, "rooms/%d/roomId", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/name", this->roomId);
    mqtt.unpublish(topic);
    sprintf(topic, "rooms/%d/sortOrder", this->roomId);
    mqtt.unpublish(topic);
  }
}
