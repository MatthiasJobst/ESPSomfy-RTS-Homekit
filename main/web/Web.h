#include <WebServer.h>
#include "SomfyController.h"
#ifndef webserver_h
#define webserver_h
class Web {
  public:
    bool uploadSuccess = false;
    void sendCacheHeaders(uint32_t seconds=604800);
    void startup();
    void handleLogin(WebServer &server);
    void handleLogout(WebServer &server);
    void handleStreamFile(WebServer &server, const char *filename, const char *encoding);
    void handleController(WebServer &server);
    void handleHomeKit(WebServer &server);
    void handleHomeKitResetPairings(WebServer &server);
    void handleLoginContext(WebServer &server);
    void handleGetRooms(WebServer &server);
    void handleGetShades(WebServer &server);
    void handleGetGroups(WebServer &server);
    void handleShadeCommand(WebServer &server);
    void handleRepeatCommand(WebServer &server);
    void handleGroupCommand(WebServer &server);
    void handleTiltCommand(WebServer &server);
    void handleDiscovery(WebServer &server);
    void handleNotFound(WebServer &server);
    void handleRoom(WebServer &server);
    void handleShade(WebServer &server);
    void handleGroup(WebServer &server);
    void handleSetPositions(WebServer &server);
    void handleSetSensor(WebServer &server);
    void handleDownloadFirmware(WebServer &server);
    void handleBackup(WebServer &server, bool attach = false);
    void handleReboot(WebServer &server);
    void handleDeserializationError(WebServer &server, DeserializationError &err);
    // Settings
    void handleGetReleases(WebServer &server);
    void handleCancelFirmware(WebServer &server);
    void handleSaveSecurity(WebServer &server);
    void handleGetSecurity(WebServer &server);
    void handleSaveRadio(WebServer &server);
    void handleGetRadio(WebServer &server);
    void handleSetGeneral(WebServer &server);
    void handleSetNetwork(WebServer &server);
    void handleSetIP(WebServer &server);
    void handleConnectWifi(WebServer &server);
    void handleModuleSettings(WebServer &server);
    void handleNetworkSettings(WebServer &server);
    void handleConnectMQTT(WebServer &server);
    void handleMQTTSettings(WebServer &server);
    // Next ID / scaffold
    void handleGetNextRoom(WebServer &server);
    void handleGetNextShade(WebServer &server);
    void handleGetNextGroup(WebServer &server);
    // Sort order
    void handleRoomSortOrder(WebServer &server);
    void handleShadeSortOrder(WebServer &server);
    void handleGroupSortOrder(WebServer &server);
    // Scan / remote command
    void handleScanAPs(WebServer &server);
    void handleSendRemoteCommand(WebServer &server);
    void handleBeginFrequencyScan(WebServer &server);
    void handleEndFrequencyScan(WebServer &server);
    void handleRecoverFilesystem(WebServer &server);
    // Room
    void handleAddRoom(WebServer &server);
    void handleSaveRoom(WebServer &server);
    void handleDeleteRoom(WebServer &server);
    // Group
    void handleAddGroup(WebServer &server);
    void handleSaveGroup(WebServer &server);
    void handleGroupOptions(WebServer &server);
    void handleDeleteGroup(WebServer &server);
    void handleLinkToGroup(WebServer &server);
    void handleUnlinkFromGroup(WebServer &server);
    // Shade mutations
    void handleAddShade(WebServer &server);
    void handleSaveShade(WebServer &server);
    void handleDeleteShade(WebServer &server);
    void handleSetMyPosition(WebServer &server);
    void handleSetRollingCode(WebServer &server);
    void handleSetPaired(WebServer &server);
    void handleUnpairShade(WebServer &server);
    void handleLinkRepeater(WebServer &server);
    void handleUnlinkRepeater(WebServer &server);
    void handleLinkRemote(WebServer &server);
    void handleUnlinkRemote(WebServer &server);
    // OTA / firmware
    void handleRestore(WebServer &server);
    void handleRestoreUpload(WebServer &server);
    void handleUpdateFirmware(WebServer &server);
    void handleUpdateFirmwareUpload(WebServer &server);
    void handleUpdateShadeConfig(WebServer &server);
    void handleUpdateShadeConfigUpload(WebServer &server);
    void handleUpdateApplication(WebServer &server);
    void handleUpdateApplicationUpload(WebServer &server);
    void begin();
    void loop();
    void end();
    // Web Handlers
    bool createAPIToken(const IPAddress ipAddress, char *token);
    bool createAPIToken(const char *payload, char *token);
    bool createAPIPinToken(const IPAddress ipAddress, const char *pin, char *token);
    bool createAPIPasswordToken(const IPAddress ipAddress, const char *username, const char *password, char *token);
    bool isAuthenticated(WebServer &server, bool cfg = false);

    //void chunkRoomsResponse(WebServer &server, const char *elem = nullptr);
    //void chunkShadesResponse(WebServer &server, const char *elem = nullptr);
    //void chunkGroupsResponse(WebServer &server, const char *elem = nullptr);
    //void chunkGroupResponse(WebServer &server, SomfyGroup *, const char *prefix = nullptr);
};
#endif
