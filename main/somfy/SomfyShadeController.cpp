// SomfyShadeController.cpp — SomfyShadeController implementation: startup/shutdown, NVS
// persistence (load/save/backup/legacy migration), frame processing, group-flag
// aggregation, repeater management, loop tick (movement check, auto-commit).
#include <cassert>
#include <esp_chip_info.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WebServer.h>
#include "SomfyShadeController.h"
#include "ConfigSettings.h"
#include "GitOTA.h"
#include "HomeKit.h"
#include "MQTT.h"
#include "ShadeConfigFile.h"
#include "Sockets.h"
#include "Utils.h"
#include "compat/preferences.h"

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern ConfigSettings settings;
extern MQTTClass mqtt;
extern Preferences pref;
extern GitUpdater git;

static const char *TAG = "SomfyController";

void SomfyShadeController::end() { this->transceiver.disableReceive(); }

SomfyShadeController::SomfyShadeController() {
  uint64_t mac = ESP.getEfuseMac();
  this->startingAddress = mac & 0x0FFFFF;
}

SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getRemoteAddress() == address) return &shade;
    else {
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        if(shade.getLinkedRemote(j).getRemoteAddress() == address) return &shade;
      }
    }
  }
  return nullptr;
}

SomfyGroup *SomfyShadeController::findGroupByRemoteAddress(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getRemoteAddress() == address) return &group;
  }
  return nullptr;
}

void SomfyShadeController::updateGroupFlags() {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group && group->getGroupId() != 255) {
      SomfyFlag oldFlags = group->flags;
      group->updateFlags();
      if(oldFlags != group->flags)
        group->emitState();
    }
  }
}

bool SomfyShadeController::begin() {
  // Load up all the configuration data.
  //ShadeConfigFile::getAppVersion(this->appVersion);
  ESP_LOGI(TAG, "App Version:%u.%u.%u", settings.appVersion.major, settings.appVersion.minor, settings.appVersion.build);
  if(ShadeConfigFile::exists()) {
    ESP_LOGI(TAG, "shades.cfg exists so we are using that");
    ShadeConfigFile::load(this);
  }
  else {
    ESP_LOGI(TAG, "Starting clean");
  }
  this->transceiver.begin();

  // Set the radio type for shades that have yet to be specified.
  bool saveFlag = false;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() != 255 && shade->bitLength == 0) {
      ESP_LOGD(TAG, "Setting bit length to %d", this->transceiver.config.type);
      shade->bitLength = this->transceiver.config.type;
      saveFlag = true;
    }
  }
  if(saveFlag) somfy.commit();
  return true;
}

void SomfyShadeController::commit() {
  if(git.lockFS) return;
  ShadeConfigFile file;
  file.begin();
  file.save(this);
  file.end();
  this->isDirty = false;
  this->lastCommit = millis();
}

void SomfyShadeController::writeBackup() {
  if(git.lockFS) return;
  ShadeConfigFile file;
  file.begin("/controller.backup", false);
  file.backup(this);
  file.end();
}

SomfyRoom * SomfyShadeController::getRoomById(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) return &this->rooms[i];
  }
  return nullptr;
}

SomfyShade * SomfyShadeController::getShadeById(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) return &this->shades[i];
  }
  return nullptr;
}

SomfyGroup * SomfyShadeController::getGroupById(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) return &this->groups[i];
  }
  return nullptr;
}

void SomfyShadeController::compressRepeaters() {
  for(uint8_t i = 0, j = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) {
      if(i != j) {
        this->repeaters[j] = this->repeaters[i];
        this->repeaters[i] = 0;
      }
      j++;
    }
  }
}

char mqttTopicBuffer[55];

void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
  }
}

void SomfyShadeController::processWaitingFrame() {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
    if(this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame();
}

void SomfyShadeController::emitState(uint8_t num) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    shade->emitState(num);
  }
}

void SomfyShadeController::publish() {
  this->updateGroupFlags();
  char arrIds[128] = "[";
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(strlen(arrIds) > 1) strlcat(arrIds, ",", sizeof(arrIds));
    itoa(shade->getShadeId(), &arrIds[strlen(arrIds)], 10);
    shade->publish();
  }
  strlcat(arrIds, "]", sizeof(arrIds));
  mqtt.publish("shades", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = this->getShadeById(i);
    if(shade) continue;
    else {
      SomfyShade::unpublish(i);
    }
  }
  strcpy(arrIds, "[");
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(strlen(arrIds) > 1) strlcat(arrIds, ",", sizeof(arrIds));
    itoa(group->getGroupId(), &arrIds[strlen(arrIds)], 10);
    group->publish();
  }
  strlcat(arrIds, "]", sizeof(arrIds));
  mqtt.publish("groups", arrIds, true);
  for(uint8_t i = 1; i <= SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = this->getGroupById(i);
    if(group) continue;
    else SomfyGroup::unpublish(i);
  }
}

uint8_t SomfyShadeController::getNextShadeId() {
  // There is no shortcut for this since the deletion of
  // a shade in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_SHADES - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
      SomfyShade *shade = &this->shades[j];
      if(shade->getShadeId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGI(TAG, "Got next Shade Id:%d", i);
      return i;
    }
  }
  return 255;
}

int8_t SomfyShadeController::getMaxShadeOrder() {
  int16_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade *shade = &this->shades[i];
    if(shade->getShadeId() == 255) continue;
    if(order < shade->sortOrder) order = shade->sortOrder;
  }
  assert(order <= 127);
  return static_cast<int8_t>(order);
}

int8_t SomfyShadeController::getMaxGroupOrder() {
  int16_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup *group = &this->groups[i];
    if(group->getGroupId() == 255) continue;
    if(order < group->sortOrder) order = group->sortOrder;
  }
  assert(order <= 127);
  return static_cast<int8_t>(order);
}

uint8_t SomfyShadeController::getNextGroupId() {
  // There is no shortcut for this since the deletion of
  // a group in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_GROUPS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
      SomfyGroup *group = &this->groups[j];
      if(group->getGroupId() == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGI(TAG, "Got next Group Id:%d", i);
      return i;
    }
  }
  return 255;
}

uint8_t SomfyShadeController::getNextRoomId() {
  // There is no shortcut for this since the deletion of
  // a room in the middle makes all of this very difficult.
  for(uint8_t i = 1; i < SOMFY_MAX_ROOMS - 1; i++) {
    bool id_exists = false;
    for(uint8_t j = 0; j < SOMFY_MAX_ROOMS; j++) {
      SomfyRoom *room = &this->rooms[j];
      if(room->roomId == i) {
        id_exists = true;
        break;
      }
    }
    if(!id_exists) {
      ESP_LOGI(TAG, "Got next room Id:%d", i);
      return i;
    }
  }
  return 0;
}

int8_t SomfyShadeController::getMaxRoomOrder() {
  int16_t order = -1;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &this->rooms[i];
    if(room->roomId == 0) continue;
    if(order < room->sortOrder) order = room->sortOrder;
  }
  assert(order <= 127);
  return static_cast<int8_t>(order);
}

uint8_t SomfyShadeController::repeaterCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] != 0) count++;
  }
  return count;
}

uint8_t SomfyShadeController::roomCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId != 0) count++;
  }
  return count;
}

uint8_t SomfyShadeController::shadeCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) count++;
  }
  return count;
}

uint8_t SomfyShadeController::groupCount() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() != 255) count++;
  }
  return count;
}

uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t id) {
  uint32_t address = this->startingAddress + id;
  uint8_t i = 0;
  // The assumption here is that the max number of groups will
  // always be less than or equal to the max number of shades.
  while(i < SOMFY_MAX_SHADES) {
    if((i < SOMFY_MAX_SHADES && this->shades[i].getShadeId() != 255 && this->shades[i].getRemoteAddress() == address) ||
      (i < SOMFY_MAX_GROUPS && this->groups[i].getGroupId() != 255 && this->groups[i].getRemoteAddress() == address)) {
      address++;
      i = 0; // Start over we cannot share addresses.
    }
    else i++;
  }

  return address;
}

SomfyShade *SomfyShadeController::addShade(JsonObject &obj) {
  SomfyShade *shade = this->addShade();
  if(shade) {
    shade->fromJSON(obj);
    shade->save();
    shade->emitState("shadeAdded");
    homekit.addShade(shade);
  }
  return shade;
}

SomfyShade *SomfyShadeController::addShade() {
  uint8_t shadeId = this->getNextShadeId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.  The time has come so the sort order is set below.
  if(shadeId == 255) return nullptr;
  SomfyShade *shade = &this->shades[shadeId - 1];
  if(shade) {
    // Compute the max BEFORE assigning the new id, so the new (still id=255)
    // slot is skipped by getMaxShadeOrder() and doesn't contribute its own
    // freshly-cleared sortOrder=0 to the max.
    shade->sortOrder = static_cast<int8_t>(this->getMaxShadeOrder() + 1);
    shade->setShadeId(shadeId);
    ESP_LOGI(TAG, "Sort order set to %d", shade->sortOrder);
    this->isDirty = true;
  }
  return shade;
}

bool SomfyShadeController::unlinkRepeater(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  this->compressRepeaters();
  this->isDirty = true;
  return true;  
}

bool SomfyShadeController::linkRepeater(uint32_t address) {
  bool bSet = false;
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(!bSet && this->repeaters[i] == address) bSet = true;
    else if(bSet && this->repeaters[i] == address) this->repeaters[i] = 0;
  }
  if(!bSet) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
      if(this->repeaters[i] == 0) {
        this->repeaters[i] = address;
        return true;
      }
    }
  }
  return true;
}

SomfyRoom *SomfyShadeController::addRoom(JsonObject &obj) {
  SomfyRoom *room = this->addRoom();
  if(room) {
    room->fromJSON(obj);
    room->save();
    room->emitState("roomAdded");
  }
  return room;
}

SomfyRoom *SomfyShadeController::addRoom() {
  uint8_t roomId = this->getNextRoomId();
  // So the next room id will be the first one we run into with an id of 0 so
  if(roomId == 0) return nullptr;
  SomfyRoom *room = &this->rooms[roomId - 1];
  if(room) {
    // See addShade(): compute max before assigning id so the new slot is skipped.
    room->sortOrder = static_cast<int8_t>(this->getMaxRoomOrder() + 1);
    room->roomId    = roomId;
    this->isDirty   = true;
  }
  return room;
}


SomfyGroup *SomfyShadeController::addGroup(JsonObject &obj) {
  SomfyGroup *group = this->addGroup();
  if(group) {
    group->fromJSON(obj);
    group->save();
    group->emitState("groupAdded");
  }
  return group;
}

SomfyGroup *SomfyShadeController::addGroup() {
  uint8_t groupId = this->getNextGroupId();
  // So the next shade id will be the first one we run into with an id of 255 so
  // if it gets deleted in the middle then it will get the first slot that is empty.
  // There is no apparent way around this.  In the future we might actually add an indexer
  // to it for sorting later.
  if(groupId == 255) return nullptr;
  SomfyGroup *group = &this->groups[groupId - 1];
  if(group) {
    // See addShade(): compute max before assigning id so the new slot is skipped.
    group->sortOrder = static_cast<int8_t>(this->getMaxGroupOrder() + 1);
    group->setGroupId(groupId);
    this->isDirty = true;
  }
  return group;
}

void SomfyShadeController::sendFrame(somfy_frame_t &frame, uint8_t repeat) {
  // drainCommandQueue() guards with txBusy() before calling here, so this
  // should never be true. If it is, skip rather than blocking app_main and
  // triggering the task watchdog.
  if(this->transceiver.txBusy()) {
    ESP_LOGW(TAG, "sendFrame: TX busy — skipping frame to avoid blocking app_main");
    return;
  }
  this->transceiver.beginTransmit();
  this->transceiver.beginFrameTx(frame, repeat);
  // endTransmit() is deferred: Transceiver::loop() calls it when txBusy() clears.
}

bool SomfyShadeController::deleteShade(uint8_t shadeId) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() == shadeId) {
      shades[i].emitState("shadeRemoved");
      shades[i].unpublish();
      homekit.removeShade(&shades[i]);
      this->shades[i].clear();
    }
  }
  this->commit();
  return true;
}

bool SomfyShadeController::deleteRoom(uint8_t roomId) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    if(this->rooms[i].roomId == roomId) {
      rooms[i].unpublish();
      for(uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
        if(shades[j].roomId == roomId) {
          shades[j].roomId = 0;
          shades[j].emitState();
        }
      }
      for(uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if(groups[j].roomId == roomId) {
          groups[j].roomId = 0;
          groups[j].emitState();
        }
      }
      rooms[i].emitState("roomRemoved");
      this->rooms[i].clear();
    }
  }
  this->commit();
  return true;
}


bool SomfyShadeController::deleteGroup(uint8_t groupId) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(this->groups[i].getGroupId() == groupId) {
      groups[i].emitState("groupRemoved");
      groups[i].unpublish();
      this->groups[i].clear();
    }
  }
  this->commit();
  return true;
}


bool SomfyShadeController::loadShadesFile(const char *filename) { return ShadeConfigFile::load(this, filename); }

void SomfyShadeController::toJSONRooms(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) {
    SomfyRoom *room = &this->rooms[i];
    if(room->roomId != 0) {
      json.beginObject();
      room->toJSON(json);
      json.endObject();
    }
  }
}

void SomfyShadeController::toJSONShades(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    SomfyShade &shade = this->shades[i];
    if(shade.getShadeId() != 255) {
      json.beginObject();
      shade.toJSON(json);
      json.endObject();
    }
  }
}

void SomfyShadeController::toJSONGroups(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    SomfyGroup &group = this->groups[i];
    if(group.getGroupId() != 255) {
      json.beginObject();
      group.toJSON(json);
      json.endObject();
    }
  }
}

void SomfyShadeController::toJSONRepeaters(JsonResponse &json) {
  for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
    if(somfy.repeaters[i] != 0) json.addElem((uint32_t)somfy.repeaters[i]);
  }
}

void SomfyShadeController::drainCommandQueue() {
  if(this->cmdQueue.empty() || !this->cmdQueue.ready()) return;
  if(this->transceiver.txBusy()) return; // Don't block app_main — loop() will drain on the next tick
  this->cmdQueue.lastDrain = millis();
  queued_cmd_t c;
  if(!this->cmdQueue.pop(c)) return;
  switch(c.type) {
    case cmd_queue_type_t::ShadeCommand: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->sendCommand(c.cmd, c.repeat, c.stepSize);
      break;
    }
    case cmd_queue_type_t::ShadeTarget: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->moveToTarget(c.target);
      break;
    }
    case cmd_queue_type_t::ShadeTargetForced: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->moveToTargetForced(c.target);
      break;
    }
    case cmd_queue_type_t::ShadeTiltTarget: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->moveToTiltTarget(c.target);
      break;
    }
    case cmd_queue_type_t::ShadeTiltCommand: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->sendTiltCommand(c.cmd);
      break;
    }
    case cmd_queue_type_t::ShadeSensor: {
      SomfyShade *s = this->getShadeById(c.entityId);
      if(s) s->sendSensorCommand(c.isWindy, c.isSunny, c.repeat);
      break;
    }
    case cmd_queue_type_t::GroupCommand: {
      SomfyGroup *g = this->getGroupById(c.entityId);
      if(g) g->sendCommand(c.cmd, c.repeat);
      break;
    }
    case cmd_queue_type_t::GroupSensor: {
      SomfyGroup *g = this->getGroupById(c.entityId);
      if(g) g->sendSensorCommand(c.isWindy, c.isSunny, c.repeat);
      break;
    }
  }
}

bool SomfyShadeController::enqueueShadeCommand(uint8_t shadeId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeCommand; c.entityId = shadeId; c.cmd = cmd; c.repeat = repeat; c.stepSize = stepSize;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueShadeTarget(uint8_t shadeId, float target) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeTarget; c.entityId = shadeId; c.target = target;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueShadeTargetForced(uint8_t shadeId, float target) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeTargetForced; c.entityId = shadeId; c.target = target;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueShadeTiltTarget(uint8_t shadeId, float target) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeTiltTarget; c.entityId = shadeId; c.target = target;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueShadeTiltCommand(uint8_t shadeId, somfy_commands cmd) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeTiltCommand; c.entityId = shadeId; c.cmd = cmd;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueShadeSensor(uint8_t shadeId, int8_t isWindy, int8_t isSunny, uint8_t repeat) {
  queued_cmd_t c; c.type = cmd_queue_type_t::ShadeSensor; c.entityId = shadeId; c.isWindy = isWindy; c.isSunny = isSunny; c.repeat = repeat;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueGroupCommand(uint8_t groupId, somfy_commands cmd, uint8_t repeat) {
  queued_cmd_t c; c.type = cmd_queue_type_t::GroupCommand; c.entityId = groupId; c.cmd = cmd; c.repeat = repeat;
  return this->cmdQueue.push(c);
}
bool SomfyShadeController::enqueueGroupSensor(uint8_t groupId, int8_t isWindy, int8_t isSunny, uint8_t repeat) {
  queued_cmd_t c; c.type = cmd_queue_type_t::GroupSensor; c.entityId = groupId; c.isWindy = isWindy; c.isSunny = isSunny; c.repeat = repeat;
  return this->cmdQueue.push(c);
}

void SomfyShadeController::loop() {
  this->transceiver.loop();
  this->drainCommandQueue();
  for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
    if(this->shades[i].getShadeId() != 255) {
      this->shades[i].checkMovement();
      this->shades[i].setGPIOs();
    }
  }
  // Only commit the file once per second.
  if(this->isDirty && millis() - this->lastCommit > 1000) {
    this->commit();
  }
}
