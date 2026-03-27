// SomfyRemote.cpp — Implementations for SomfyRoom, SomfyRemote, SomfyLinkedRemote and
// SomfyGroup: JSON serialisation/deserialisation, MQTT publishing, rolling-code
// management, command dispatch to linked shades, and NVS persistence helpers.
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "GitOTA.h"
#include "SomfyRemote.h"
#include "SomfyTransceiver.h"
#include "SomfyShade.h"
#include "SomfyController.h"
#include "Sockets.h"
#include "MQTT.h"

static const char *TAG = "SomfyRemote";
static char mqttTopicBuffer[55];

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern ConfigSettings settings;
extern MQTTClass mqtt;
extern Preferences pref;
extern GitUpdater git;

void SomfyRoom::clear() {
  this->roomId = 0;
  strcpy(this->name, "");
}

void SomfyGroup::clear() {
  this->setGroupId(255);
  this->setRemoteAddress(0);
  this->repeats = 0;
  this->roomId = 0;
  this->name[0] = 0x00;
  memset(&this->linkedShades, 0x00, sizeof(this->linkedShades));
}

bool SomfyGroup::linkShade(uint8_t shadeId) {
  // Check to see if the shade is already linked. If it is just return true
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) {
      return true;
    }
  }
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) {
      this->linkedShades[i] = shadeId;
      somfy.commit();
      return true;
    }
  }
  return false;
}

bool SomfyGroup::unlinkShade(uint8_t shadeId) {
  bool removed = false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) {
      this->linkedShades[i] = 0;
      removed = true;
    }
  }
  // Compress the linked shade ids so we can stop looking on the first 0
  if(removed) {
    this->compressLinkedShadeIds();
    somfy.commit();
  }
  return removed;
}

void SomfyGroup::compressLinkedShadeIds() {
  // [1,0,4,3,0,0,0] i:0,j:0
  // [1,0,4,3,0,0,0] i:1,j:1
  // [1,4,0,3,0,0,0] i:2,j:1
  // [1,4,3,0,0,0,0] i:3,j:2
  // [1,4,3,0,0,0,0] i:4,j:2

  // [1,2,0,0,3,0,0] i:0,j:0
  // [1,2,0,0,3,0,0] i:1,j:1
  // [1,2,0,0,3,0,0] i:2,j:2
  // [1,2,0,0,3,0,0] i:3,j:2
  // [1,2,3,0,0,0,0] i:4,j:2
  // [1,2,3,0,0,0,0] i:5,j:3
  for(uint8_t i = 0, j = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      if(i != j) {
        this->linkedShades[j] = this->linkedShades[i];
        this->linkedShades[i] = 0;
      }
      j++;
    }
  }
}

bool SomfyGroup::hasShadeId(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == 0) break;
    if(this->linkedShades[i] == shadeId) return true;
  }
  return false;
}

bool SomfyRemote::simMy() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SimMy)) > 0; }

void SomfyRemote::setSimMy(bool bSimMy) { bSimMy ? this->flags |= static_cast<uint8_t>(somfy_flags_t::SimMy) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SimMy)); }

bool SomfyRemote::hasSunSensor() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::SunSensor)) > 0;}

bool SomfyRemote::hasLight() { return (this->flags & static_cast<uint8_t>(somfy_flags_t::Light)) > 0; }

void SomfyRemote::setSunSensor(bool bHasSensor ) { bHasSensor ? this->flags |= static_cast<uint8_t>(somfy_flags_t::SunSensor) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::SunSensor)); }

void SomfyRemote::setLight(bool bHasLight ) { bHasLight ? this->flags |= static_cast<uint8_t>(somfy_flags_t::Light) : this->flags &= ~(static_cast<uint8_t>(somfy_flags_t::Light)); }


void SomfyGroup::updateFlags() { 
  uint8_t oldFlags = this->flags;
  this->flags = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) this->flags |= shade->flags;
    }
    else break;
  }
  if(oldFlags != this->flags) this->emitState();
}

void SomfyRemote::triggerGPIOs(somfy_frame_t &frame) { }

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

void SomfyGroup::publishState() {
  if(mqtt.connected()) {
    this->publish("direction", this->direction, true);
    this->publish("lastRollingCode", this->lastRollingCode, true);
    this->publish("flipCommands", this->flipCommands, true);
    const uint8_t sunFlag = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::SunFlag));
    const uint8_t isSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
    const uint8_t isWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
    this->publish("sunFlag", sunFlag);
    this->publish("sunny", isSunny);
    this->publish("windy", isWindy);    
  }  
}

void SomfyGroup::publish() {
  if(mqtt.connected()) {
    this->publish("groupId", this->groupId, true);
    this->publish("name", this->name, true);
    this->publish("remoteAddress", this->getRemoteAddress(), true);
    this->publish("groupType", static_cast<uint8_t>(this->groupType), true);
    this->publish("flags", this->flags, true);
    this->publish("sunSensor", this->hasSunSensor(), true);
    this->publishState();
  }
}

void SomfyGroup::unpublish() { SomfyGroup::unpublish(this->groupId); }

void SomfyGroup::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyGroup::unpublish(id, "groupId");
    SomfyGroup::unpublish(id, "name");
    SomfyGroup::unpublish(id, "remoteAddress");
    SomfyGroup::unpublish(id, "groupType");
    SomfyGroup::unpublish(id, "direction");
    SomfyGroup::unpublish(id, "lastRollingCode");
    SomfyGroup::unpublish(id, "flags");
    SomfyGroup::unpublish(id, "SunSensor");
    SomfyGroup::unpublish(id, "flipCommands");
  }
}

void SomfyGroup::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}

bool SomfyGroup::publish(const char *topic, const char *val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyGroup::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "groups/%u/%s", this->groupId, topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}
// State Setters

int8_t SomfyGroup::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->publish("direction", this->direction);
  }
  return old;
}

void SomfyRoom::emitState(const char *evt) { this->emitState(255, evt); }

void SomfyRoom::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("roomId", this->roomId);
  json->addElem("name", this->name);
  json->addElem("sortOrder", this->sortOrder);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  ClientSocketEvent e(evt);
  char buf[55];
  uint8_t flags = 0;
  snprintf(buf, sizeof(buf), "{\"roomId\":%d,", this->roomId);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"name\":\"%s\",", this->name);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"sortOrder\":%d}", this->sortOrder);
  e.appendMessage(buf);
  if(num >= 255) sockEmit.sendToClients(&e);
  else sockEmit.sendToClient(num, &e);
  */
  this->publish();
}

void SomfyGroup::emitState(const char *evt) { this->emitState(255, evt); }

void SomfyGroup::emitState(uint8_t num, const char *evt) {
  uint8_t flags = 0;
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("groupId", this->groupId);
  json->addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json->addElem("name", this->name);
  json->addElem("sunSensor", this->hasSunSensor());
  json->beginArray("shades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 255 && this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) json->addElem(this->linkedShades[i]);
      flags |= shade->flags;
    }
  }
  json->endArray();
  json->addElem("flags", flags);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  ClientSocketEvent e(evt);
  char buf[55];
  uint8_t flags = 0;
  snprintf(buf, sizeof(buf), "{\"groupId\":%d,", this->groupId);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"remoteAddress\":%d,", this->getRemoteAddress());
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"name\":\"%s\",", this->name);
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"sunSensor\":%s,", this->hasSunSensor() ? "true" : "false");
  e.appendMessage(buf);
  snprintf(buf, sizeof(buf), "\"shades\":[");
  e.appendMessage(buf);
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 255) {
      if(this->linkedShades[i] != 0) {
        SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
        if(shade) {
          flags |= shade->flags;
          snprintf(buf, sizeof(buf), "%s%d", i != 0 ? "," : "", this->linkedShades[i]);
          e.appendMessage(buf);
        }
      }
    }
  }
  snprintf(buf, sizeof(buf), "],\"flags\":%d}", flags);
  e.appendMessage(buf);
  
  if(num >= 255) sockEmit.sendToClients(&e);
  else sockEmit.sendToClient(num, &e);
  */
  this->publish();
}

void SomfyGroup::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }

void SomfyGroup::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  // This sendCommand function will always be called externally. sendCommand at the remote level
  // is expected to be called internally when the motor needs commanded.
  if(this->bitLength == 0) this->bitLength = somfy.transceiver.config.type;
  SomfyRemote::sendCommand(cmd, repeat, stepSize);
  
  switch(cmd) {
    case somfy_commands::My:
      this->p_direction(0);
      break;
    case somfy_commands::Up:
      this->p_direction(-1);
      break;
    case somfy_commands::Down:
      this->p_direction(1);
      break;
    default:
      break;
  }
  
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) {
        shade->processInternalCommand(cmd, repeat);
        shade->emitCommand(cmd, "group", this->getRemoteAddress());
      }
    }
  }
  this->updateFlags();
  this->emitState();
  
}  

bool SomfyRoom::save() { somfy.commit(); return true; }

bool SomfyGroup::save() { somfy.commit(); return true; }

bool SomfyRoom::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("sortOrder")) this->sortOrder = obj["sortOrder"];
  return true;
}
/*

bool SomfyRoom::toJSON(JsonObject &obj) {
  obj["roomId"] = this->roomId;
  obj["name"] = this->name;
  obj["sortOrder"] = this->sortOrder;
  return true;
}
*/
void SomfyRoom::toJSON(JsonResponse &json) {
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("sortOrder", this->sortOrder);
}


bool SomfyGroup::fromJSON(JsonObject &obj) {
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("roomId")) this->roomId = obj["roomId"];
  if(obj.containsKey("remoteAddress")) this->setRemoteAddress(obj["remoteAddress"]);
  if(obj.containsKey("bitLength")) this->bitLength = obj["bitLength"];
  if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
  if(obj.containsKey("flipCommands")) this->flipCommands = obj["flipCommands"].as<bool>();
  
  //if(obj.containsKey("sunSensor")) this->hasSunSensor() = obj["sunSensor"];  This is calculated
  if(obj.containsKey("repeats")) this->repeats = obj["repeats"];
  if(obj.containsKey("linkedShades")) {
    uint8_t linkedShades[SOMFY_MAX_GROUPED_SHADES];
    memset(linkedShades, 0x00, sizeof(linkedShades));
    JsonArray arr = obj["linkedShades"];
    uint8_t i = 0;
    for(uint8_t shadeId : arr) {
      linkedShades[i++] = shadeId;
    }
  }
  return true;
}

void SomfyGroup::toJSON(JsonResponse &json) {
  this->updateFlags();
  json.addElem("groupId", this->getGroupId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flags", this->flags);
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);
  json.beginArray("linkedShades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    uint8_t shadeId = this->linkedShades[i];
    if(shadeId > 0 && shadeId < 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        json.beginObject();
        shade->toJSONRef(json);
        json.endObject();
      }
    }
  }
  json.endArray();
}

void SomfyGroup::toJSONRef(JsonResponse &json) {
  this->updateFlags();
  json.addElem("groupId", this->getGroupId());
  json.addElem("roomId", this->roomId);
  json.addElem("name", this->name);
  json.addElem("remoteAddress", (uint32_t)this->m_remoteAddress);
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
  json.addElem("bitLength", this->bitLength);
  json.addElem("proto", static_cast<uint8_t>(this->proto));
  json.addElem("sunSensor", this->hasSunSensor());
  json.addElem("flipCommands", this->flipCommands);
  json.addElem("flags", this->flags);
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);
}

/*

bool SomfyGroup::toJSON(JsonObject &obj) {
  this->updateFlags();
  obj["groupId"] = this->getGroupId();
  obj["roomId"] = this->roomId;
  obj["name"] = this->name;
  obj["remoteAddress"] = this->m_remoteAddress;
  obj["lastRollingCode"] = this->lastRollingCode;
  obj["bitLength"] = this->bitLength;
  obj["proto"] = static_cast<uint8_t>(this->proto);
  obj["sunSensor"] = this->hasSunSensor();
  obj["flipCommands"] = this->flipCommands;
  obj["flags"] = this->flags;
  obj["repeats"] = this->repeats;
  obj["sortOrder"] = this->sortOrder;
  SomfyRemote::toJSON(obj);
  JsonArray arr = obj.createNestedArray("linkedShades");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    uint8_t shadeId = this->linkedShades[i];
    if(shadeId > 0 && shadeId < 255) {
      SomfyShade *shade = somfy.getShadeById(shadeId);
      if(shade) {
        JsonObject lsd = arr.createNestedObject();
        shade->toJSONRef(lsd);
      }
    }
  }
  return true;
}
*/
void SomfyRemote::toJSON(JsonResponse &json) {
  json.addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
}
/*

bool SomfyRemote::toJSON(JsonObject &obj) {
  //obj["remotePrefId"] = this->getRemotePrefId();
  obj["remoteAddress"] = this->getRemoteAddress();
  obj["lastRollingCode"] = this->lastRollingCode;
  return true;  
}
*/
void SomfyRemote::setRemoteAddress(uint32_t address) { this->m_remoteAddress = address; snprintf(this->m_remotePrefId, sizeof(this->m_remotePrefId), "_%lu", (unsigned long)this->m_remoteAddress); }

uint32_t SomfyRemote::getRemoteAddress() { return this->m_remoteAddress; }

somfy_commands SomfyRemote::transformCommand(somfy_commands cmd) {
  if(this->flipCommands) {
    switch(cmd) {
      case somfy_commands::Up:
        return somfy_commands::Down;
      case somfy_commands::MyUp:
        return somfy_commands::MyDown;
      case somfy_commands::Down:
        return somfy_commands::Up;
      case somfy_commands::MyDown:
        return somfy_commands::MyUp;
      case somfy_commands::StepUp:
        return somfy_commands::StepDown;
      case somfy_commands::StepDown:
        return somfy_commands::StepUp;
      default:
        break;
    }
  }
  return cmd;
}

void SomfyRemote::sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat) {
  uint8_t flags = (this->flags >> 4) & 0x0F;
  if(isWindy > 0) flags |= 0x01;
  if(isSunny > 0) flags |= 0x02;
  if(isWindy == 0) flags &= ~0x01;
  if(isSunny == 0) flags &= ~0x02;

  // Now ship this off as an 80 bit command.
  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.rollingCode = (uint16_t)flags;
  this->lastFrame.encKey = 160; // Sensor commands are always encryption code 160.
  this->lastFrame.cmd = somfy_commands::Sensor;
  this->lastFrame.processed = false;
  ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
  ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
  ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
  ESP_LOGI(TAG, "REPEAT: %d", repeat);
  somfy.sendFrame(this->lastFrame, repeat);
  somfy.processFrame(this->lastFrame, true);
}

void SomfyRemote::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }

void SomfyRemote::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  this->lastFrame.rollingCode = this->getNextRollingCode();
  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.cmd = this->transformCommand(cmd);
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.stepSize = stepSize;
  this->lastFrame.valid = true;
  // Match the encKey to the rolling code.  These keys range from 160 to 175.
  this->lastFrame.encKey = 0xA0 | static_cast<uint8_t>(this->lastFrame.rollingCode & 0x000F);
  this->lastFrame.proto = this->proto;
  if(this->lastFrame.bitLength == 0) this->lastFrame.bitLength = bit_length;
  if(this->lastFrame.rollingCode == 0) ESP_LOGE(TAG, "ERROR: Setting rcode to 0");
  this->p_lastRollingCode(this->lastFrame.rollingCode);
  // We have to set the processed to clear this if we are sending
  // another command.
  this->lastFrame.processed = false;
  if(this->proto == radio_proto::GP_Relay) {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "SETTING GPIO");
  }
  else if(this->proto == radio_proto::GP_Remote) {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "TRIGGER GPIO");
    this->triggerGPIOs(this->lastFrame);
  }
  else {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "REPEAT: %d", repeat);
    somfy.sendFrame(this->lastFrame, repeat);
  }
  somfy.processFrame(this->lastFrame, true);
}

bool SomfyRemote::isLastCommand(somfy_commands cmd) {
  if(this->lastFrame.cmd != cmd || this->lastFrame.rollingCode != this->lastRollingCode) {
    ESP_LOGI(TAG, "Not the last command %d: %d - %d", static_cast<uint8_t>(this->lastFrame.cmd), this->lastFrame.rollingCode, this->lastRollingCode);
    return false;
  }
  return true;
}

void SomfyRemote::repeatFrame(uint8_t repeat) {
  if(this->proto == radio_proto::GP_Relay)
    return;
  else if(this->proto == radio_proto::GP_Remote) {
    this->triggerGPIOs(this->lastFrame);
    return;
  }
  somfy.transceiver.beginTransmit();
  byte frm[10];
  this->lastFrame.encodeFrame(frm);
  this->lastFrame.repeats++;
  somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 2 : 12, this->bitLength);
  for(uint8_t i = 0; i < repeat; i++) {
    this->lastFrame.repeats++;
    if(this->lastFrame.bitLength == 80) this->lastFrame.encode80BitFrame(&frm[0], this->lastFrame.repeats);
    somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 7 : 6, this->bitLength);
    esp_task_wdt_reset();
  }
  somfy.transceiver.endTransmit();
  //somfy.processFrame(this->lastFrame, true);
}

uint16_t SomfyRemote::getNextRollingCode() {
  pref.begin("ShadeCodes");
  uint16_t code = pref.getUShort(this->m_remotePrefId, 0);
  code++;
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();
  this->p_lastRollingCode(code);
  ESP_LOGI(TAG, "Getting Next Rolling code %d", this->lastRollingCode);
  return code;
}

uint16_t SomfyRemote::p_lastRollingCode(uint16_t code) { 
  uint16_t old = this->lastRollingCode;
  this->lastRollingCode = code; 
  return old;
}

uint16_t SomfyRemote::setRollingCode(uint16_t code) {
  if(this->lastRollingCode != code) {
    pref.begin("ShadeCodes");
    pref.putUShort(this->m_remotePrefId, code);
    pref.end();  
    this->lastRollingCode = code;
    ESP_LOGI(TAG, "Setting Last Rolling code %d", this->lastRollingCode);
  }
  return code;
}

SomfyLinkedRemote::SomfyLinkedRemote() {}

// Transceiver Implementation
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3

