// SomfyGroup.cpp — SomfyGroup: linked-shade management, command dispatch,
// flag aggregation, JSON serialisation, and socket-emit.
#include "SomfyGroup.h"
#include "SomfyShade.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "esp_log.h"

static const char *TAG = "SomfyGroup";

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;

void SomfyGroup::clear() {
  ESP_LOGD(TAG, "Clearing group.");
  this->setGroupId(255);
  this->setRemoteAddress(0);
  this->repeats = 0;
  this->roomId = 0;
  this->name[0] = 0x00;
  memset(&this->linkedShades, 0x00, sizeof(this->linkedShades));
}

bool SomfyGroup::save() { ESP_LOGD(TAG, "Saving group."); somfy.commit(); return true; }

bool SomfyGroup::linkShade(uint8_t shadeId) {
  ESP_LOGD(TAG, "Linking shade %d to group.", shadeId);
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) return true;
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
  ESP_LOGD(TAG, "Unlinking shade %d from group.", shadeId);
  bool removed = false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] == shadeId) {
      this->linkedShades[i] = 0;
      removed = true;
    }
  }
  if(removed) {
    this->compressLinkedShadeIds();
    somfy.commit();
  }
  return removed;
}

void SomfyGroup::compressLinkedShadeIds() {
  ESP_LOGD(TAG, "Compressing linked shade IDs for group.");
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

void SomfyGroup::updateFlags() {
  ESP_LOGD(TAG, "Updating group flags based on linked shades.");
  SomfyFlag oldFlags = this->flags;
  this->flags.resetFlags();
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++) {
    if(this->linkedShades[i] != 0) {
      SomfyShade *shade = somfy.getShadeById(this->linkedShades[i]);
      if(shade) this->flags |= shade->flags;
    }
    else break;
  }
  if(oldFlags != this->flags) this->emitState();
}

int8_t SomfyGroup::p_direction(int8_t dir) {
  int8_t old = this->direction;
  this->direction = dir;
  return old;
}

void SomfyGroup::emitState(const char *evt) { this->emitState(255, evt); }

void SomfyGroup::emitState(uint8_t num, const char *evt) {
  ESP_LOGD(TAG, "Emitting group state with num %d and event %s.", num, evt);
  SomfyFlag flags = SomfyFlag();
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
      if(shade) {
        json->addElem(this->linkedShades[i]);
        flags |= shade->flags;
      }
    }
  }
  json->endArray();
  json->addElem("flags", flags.getFlags());
  json->endObject();
  sockEmit.endEmit(num);
}

void SomfyGroup::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }

void SomfyGroup::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  ESP_LOGD(TAG, "Sending command %d to group with repeat %d and stepSize %d.", static_cast<uint8_t>(cmd), repeat, stepSize);
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

bool SomfyGroup::fromJSON(JsonObject &obj) {
  ESP_LOGD(TAG, "Updating group from JSON.");
  if(obj.containsKey("name")) strlcpy(this->name, obj["name"], sizeof(this->name));
  if(obj.containsKey("roomId")) this->roomId = obj["roomId"];
  if(obj.containsKey("remoteAddress")) this->setRemoteAddress(obj["remoteAddress"]);
  if(obj.containsKey("bitLength")) this->bitLength = obj["bitLength"];
  if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
  if(obj.containsKey("flipCommands")) this->flipCommands = obj["flipCommands"].as<bool>();
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
  ESP_LOGD(TAG, "Reading group from Json.");
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
  json.addElem("flags", this->flags.getFlags());
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
  json.addElem("flags", this->flags.getFlags());
  json.addElem("repeats", this->repeats);
  json.addElem("sortOrder", this->sortOrder);
}

void SomfyGroup::unpublish() {}
void SomfyGroup::unpublish(uint8_t id) { (void)id; }
