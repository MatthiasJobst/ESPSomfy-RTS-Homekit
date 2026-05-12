// SomfyShadeController.h — Top-level shade controller: SomfyShadeController aggregates all
// shades, groups, rooms and the transceiver.  Owns NVS load/save, OTA commit, frame
// dispatch, MQTT/socket state broadcasting and HomeKit bridge lifecycle.
#pragma once
#include "SomfyFrame.h"
#include "SomfyTransceiver.h"
#include "SomfyRoom.h"
#include "SomfyGroup.h"
#include "SomfyShade.h"
#include "SomfyCommandQueue.h"

#define SOMFY_MAX_SHADES 32
#define SOMFY_MAX_GROUPS 16
#define SOMFY_MAX_LINKED_REMOTES 7
#define SOMFY_MAX_GROUPED_SHADES 32
#define SOMFY_MAX_ROOMS 16
#define SOMFY_MAX_REPEATERS 7

class SomfyShadeController {
  protected:
    void drainCommandQueue();

  public:
    uint32_t lastCommit = 0;
    SomfyCommandQueue cmdQueue;
    bool isDirty = false;
    uint32_t startingAddress;
    uint8_t getNextRoomId();
    uint8_t getNextShadeId();
    uint8_t getNextGroupId();
    int8_t getMaxRoomOrder();
    int8_t getMaxShadeOrder();
    int8_t getMaxGroupOrder();
    uint32_t getNextRemoteAddress(uint8_t shadeId);
    SomfyShadeController();
    SomfyTransceiver transceiver;
    SomfyRoom *addRoom();
    SomfyRoom *addRoom(JsonObject &obj);
    SomfyShade *addShade();
    SomfyShade *addShade(JsonObject &obj);
    SomfyGroup *addGroup();
    SomfyGroup *addGroup(JsonObject &obj);
    bool deleteRoom(uint8_t roomId);
    bool deleteShade(uint8_t shadeId);
    bool deleteGroup(uint8_t groupId);
    bool begin();
    void loop();
    void end();
    void compressRepeaters();
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0};
    SomfyRoom rooms[SOMFY_MAX_ROOMS];
    SomfyShade shades[SOMFY_MAX_SHADES];
    SomfyGroup groups[SOMFY_MAX_GROUPS];
    bool linkRepeater(uint32_t address);
    bool unlinkRepeater(uint32_t address);
    void toJSONShades(JsonResponse &json);
    void toJSONRooms(JsonResponse &json);
    void toJSONGroups(JsonResponse &json);
    void toJSONRepeaters(JsonResponse &json);
    uint8_t repeaterCount();
    uint8_t roomCount();
    uint8_t shadeCount();
    uint8_t groupCount();
    void updateGroupFlags();
    SomfyShade *getShadeById(uint8_t shadeId);
    SomfyRoom *getRoomById(uint8_t roomId);
    SomfyGroup *getGroupById(uint8_t groupId);
    SomfyShade *findShadeByRemoteAddress(uint32_t address);
    SomfyGroup *findGroupByRemoteAddress(uint32_t address);
    bool enqueueShadeCommand(uint8_t shadeId, somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    bool enqueueShadeTarget(uint8_t shadeId, float target);
    bool enqueueShadeTargetForced(uint8_t shadeId, float target);
    bool enqueueShadeTiltTarget(uint8_t shadeId, float target);
    bool enqueueShadeTiltCommand(uint8_t shadeId, somfy_commands cmd);
    bool enqueueShadeSensor(uint8_t shadeId, int8_t isWindy, int8_t isSunny, uint8_t repeat);
    bool enqueueGroupCommand(uint8_t groupId, somfy_commands cmd, uint8_t repeat);
    bool enqueueGroupSensor(uint8_t groupId, int8_t isWindy, int8_t isSunny, uint8_t repeat);
    void sendFrame(somfy_frame_t &frame, uint8_t repeats = 0);
    void processFrame(somfy_frame_t &frame, bool internal = false);
    void emitState(uint8_t num = 255);
    void publish();
    void processWaitingFrame();
    void commit();
    void writeBackup();
    bool loadShadesFile(const char *filename);
};
