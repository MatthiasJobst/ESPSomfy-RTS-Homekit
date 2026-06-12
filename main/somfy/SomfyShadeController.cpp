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

static const char *s_TAG = "SomfyController";

// Member-initializer order matches declaration order in the header (alphabetical).
// commandDispatcher binds transceiver by reference; transceiver is declared later
// and constructed after, but only the reference is bound here (no access), so this
// is well-defined.
SomfyShadeController::SomfyShadeController()
    : commandDispatcher(this->transceiver),
      groupController([this] { this->isDirty = true; }, [this] { this->commandDispatcher.cmdQueue.reset(); }),
      repeaterController([this] { this->isDirty = true; }),
      roomController([this] { this->isDirty = true; }, [this](uint8_t roomId) { this->onRoomRemoved(roomId); }),
      startingAddress(ESP.getEfuseMac() & 0x0FFFFF)
{
}

SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t address)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade &shade = this->shades[i];
        if (shade.getRemoteAddress() == address)
            return &shade;
        else {
            for (uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
                if (shade.getLinkedRemote(j).getRemoteAddress() == address) return &shade;
            }
        }
    }
    return nullptr;
}

void SomfyShadeController::applyDefaultBitLengths()
{
    // Set the radio type for shades that have yet to be specified.
    bool saveFlag = false;
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &this->shades[i];
        if (shade->getShadeId() != 255 && shade->bitLength == 0) {
            ESP_LOGD(s_TAG, "Setting bit length to %d", this->transceiver.config.type);
            shade->bitLength = this->transceiver.config.type;
            saveFlag = true;
        }
    }
    if (saveFlag) this->commit();
}

void SomfyShadeController::commit()
{
    if (git.lockFS) return;
    ShadeConfigFile file;
    file.begin();
    file.save(this);
    file.end();
    this->isDirty = false;
    this->lastCommit = millis();
}

void SomfyShadeController::writeBackup()
{
    if (git.lockFS) return;
    ShadeConfigFile file;
    file.begin("/controller.backup", false);
    file.backup(this);
    file.end();
}

SomfyShade *SomfyShadeController::getShadeById(uint8_t shadeId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        if (this->shades[i].getShadeId() == shadeId) return &this->shades[i];
    }
    return nullptr;
}

char mqttTopicBuffer[55];

void SomfyShadeController::processFrame(somfy_frame_t &frame, bool internal)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        if (this->shades[i].getShadeId() != 255) this->shades[i].processFrame(frame, internal);
    }
}

void SomfyShadeController::processWaitingFrame()
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
        if (this->shades[i].getShadeId() != 255) this->shades[i].processWaitingFrame();
}

void SomfyShadeController::emitState(uint8_t num)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &this->shades[i];
        if (shade->getShadeId() == 255) continue;
        shade->emitState(num);
    }
}

void SomfyShadeController::publish()
{
    this->groupController.updateGroupFlags();
    char arrIds[128] = "[";
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &this->shades[i];
        if (shade->getShadeId() == 255) continue;
        if (strlen(arrIds) > 1) strlcat(arrIds, ",", sizeof(arrIds));
        itoa(shade->getShadeId(), &arrIds[strlen(arrIds)], 10);
        shade->publish();
    }
    strlcat(arrIds, "]", sizeof(arrIds));
    mqtt.publish("shades", arrIds, true);
    for (uint8_t i = 1; i <= SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = this->getShadeById(i);
        if (shade)
            continue;
        else {
            SomfyShade::unpublish(i);
        }
    }
    strcpy(arrIds, "[");
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
        SomfyGroup *group = &this->groupController.groupSlot(i);
        if (group->getGroupId() == 255) continue;
        if (strlen(arrIds) > 1) strlcat(arrIds, ",", sizeof(arrIds));
        itoa(group->getGroupId(), &arrIds[strlen(arrIds)], 10);
        group->publish();
    }
    strlcat(arrIds, "]", sizeof(arrIds));
    mqtt.publish("groups", arrIds, true);
    for (uint8_t i = 1; i <= SOMFY_MAX_GROUPS; i++) {
        SomfyGroup *group = this->groupController.getGroupById(i);
        if (group)
            continue;
        else
            SomfyGroup::unpublish(i);
    }
}

uint8_t SomfyShadeController::getNextShadeId()
{
    // There is no shortcut for this since the deletion of
    // a shade in the middle makes all of this very difficult.
    for (uint8_t i = 1; i < SOMFY_MAX_SHADES - 1; i++) {
        bool id_exists = false;
        for (uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
            SomfyShade *shade = &this->shades[j];
            if (shade->getShadeId() == i) {
                id_exists = true;
                break;
            }
        }
        if (!id_exists) {
            ESP_LOGI(s_TAG, "Got next Shade Id:%d", i);
            return i;
        }
    }
    return 255;
}

int8_t SomfyShadeController::getMaxShadeOrder()
{
    int16_t order = -1;
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &this->shades[i];
        if (shade->getShadeId() == 255) continue;
        if (order < shade->sortOrder) order = shade->sortOrder;
    }
    assert(order <= 127);
    return static_cast<int8_t>(order);
}

uint8_t SomfyShadeController::shadeCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        if (this->shades[i].getShadeId() != 255) count++;
    }
    return count;
}

uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t id)
{
    uint32_t address = this->startingAddress + id;
    uint8_t i = 0;
    // The assumption here is that the max number of groups will
    // always be less than or equal to the max number of shades.
    while (i < SOMFY_MAX_SHADES) {
        if ((i < SOMFY_MAX_SHADES && this->shades[i].getShadeId() != 255 &&
             this->shades[i].getRemoteAddress() == address) ||
            (i < SOMFY_MAX_GROUPS && this->groupController.groupSlot(i).getGroupId() != 255 &&
             this->groupController.groupSlot(i).getRemoteAddress() == address)) {
            address++;
            i = 0; // Start over we cannot share addresses.
        } else
            i++;
    }

    return address;
}

SomfyShade *SomfyShadeController::addShade(JsonObject &obj)
{
    SomfyShade *shade = this->addShade();
    shade->fromJSON(obj);
    shade->save();
    shade->emitState("shadeAdded");
    homekit.addShade(shade);
    return shade;
}

SomfyShade *SomfyShadeController::addShade()
{
    uint8_t shadeId = this->getNextShadeId();
    // So the next shade id will be the first one we run into with an id of 255 so
    // if it gets deleted in the middle then it will get the first slot that is empty.
    // There is no apparent way around this.  In the future we might actually add an indexer
    // to it for sorting later.  The time has come so the sort order is set below.
    if (shadeId == 255) return nullptr;
    SomfyShade *shade = &this->shades[shadeId - 1];
    // Compute the max BEFORE assigning the new id, so the new (still id=255)
    // slot is skipped by getMaxShadeOrder() and doesn't contribute its own
    // freshly-cleared sortOrder=0 to the max.
    shade->sortOrder = static_cast<int8_t>(this->getMaxShadeOrder() + 1);
    shade->setShadeId(shadeId);
    ESP_LOGI(s_TAG, "Sort order set to %d", shade->sortOrder);
    this->isDirty = true;
    this->commandDispatcher.cmdQueue.reset(); // drop commands bound to now-stale shade slots
    return shade;
}

bool SomfyShadeController::deleteShade(uint8_t shadeId)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        if (this->shades[i].getShadeId() == shadeId) {
            shades[i].emitState("shadeRemoved");
            shades[i].unpublish();
            homekit.removeShade(&shades[i]);
            this->shades[i].clear();
            this->commandDispatcher.cmdQueue.reset(); // drop commands bound to the removed shade
        }
    }
    this->commit();
    return true;
}

void SomfyShadeController::onRoomRemoved(uint8_t roomId)
{
    for (uint8_t j = 0; j < SOMFY_MAX_SHADES; j++) {
        if (this->shades[j].roomId == roomId) {
            this->shades[j].roomId = 0;
            this->shades[j].emitState();
        }
    }
    for (uint8_t j = 0; j < SOMFY_MAX_GROUPS; j++) {
        if (this->groupController.groupSlot(j).roomId == roomId) {
            this->groupController.groupSlot(j).roomId = 0;
            this->groupController.groupSlot(j).emitState();
        }
    }
}

bool SomfyShadeController::loadShadesFile(const char *filename)
{
    return ShadeConfigFile::load(this, filename);
}

void SomfyShadeController::toJSONShades(JsonResponse &json)
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade &shade = this->shades[i];
        if (shade.getShadeId() != 255) {
            json.beginObject();
            shade.toJSON(json);
            json.endObject();
        }
    }
}


void SomfyShadeController::tickShades()
{
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        if (this->shades[i].getShadeId() != 255) {
            this->shades[i].checkMovement();
            this->shades[i].setGPIOs();
        }
    }
}

void SomfyShadeController::autoCommit()
{
    // Only commit the file once per second.
    if (this->isDirty && millis() - this->lastCommit > 1000) {
        this->commit();
    }
}
