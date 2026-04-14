// globals_stub.cpp — defines all extern globals and provides empty implementations
// for classes whose .cpp files are not compiled in the test build.

#include "nvs.h"  // NVS in-memory store definitions

// ── NVS stub state ─────────────────────────────────────────────────────────
std::unordered_map<std::string, NvsStore> nvs_ns_stores;
std::unordered_map<uint32_t, std::string> nvs_handle_ns;

// ── GPIO stub state ────────────────────────────────────────────────────────
#include "driver/gpio.h"
std::unordered_map<int, uint32_t> gpio_pin_levels;

// ── MQTT stub state ────────────────────────────────────────────────────────
#include "MQTT.h"
bool mqtt_connected_flag = false;
std::unordered_map<std::string, std::string> mqtt_published;
std::unordered_map<std::string, std::string> mqtt_unpublished;
uint32_t nvs_stub_next_handle = 1;

// ── ELECHOUSE CC1101 stub instance ─────────────────────────────────────────
#include "ELECHOUSE_CC1101_SRC_DRV.h"
ELECHOUSE_CC1101 ELECHOUSE_cc1101;

// ── test clock ────────────────────────────────────────────────────────────
#include "Arduino.h"
uint64_t test_clock_ms = 0;

// Now pull in project types (stubs intercept the heavy headers)
#include "ConfigSettings.h"
#include "WResp.h"
#include "MQTT.h"
#include "Sockets.h"
#include "GitOTA.h"
#include "HomeKit.h"

// SomfyController.h lives in main/somfy/ so it can't be stubbed via stubs dir.
// We include it directly — its method bodies are provided below.
#include "../../main/somfy/SomfyController.h"

// ── Global instances ────────────────────────────────────────────────────────
// Must be defined AFTER their types are complete.
SomfyShadeController somfy;
SocketEmitter        sockEmit;
ConfigSettings       settings;
MQTTClass            mqtt;
GitUpdater           git;
HomeKitClass         homekit;

// Preferences is header-only (compat/preferences.h) so no separate instance needed —
// but SomfyShade.cpp does `extern Preferences pref`, so define it here.
#include "../../main/compat/preferences.h"
Preferences pref;

// bit_length is declared extern in SomfyTransceiver.h and used by sendCommand.
uint8_t bit_length = 56;

// ── SomfyShadeController stub implementations ──────────────────────────────
SomfyShadeController::SomfyShadeController() {}

void SomfyShadeController::updateGroupFlags() {}
uint8_t SomfyShadeController::getNextShadeId() { return 0; }
uint8_t SomfyShadeController::getNextGroupId() { return 0; }
uint8_t SomfyShadeController::getNextRoomId()  { return 0; }
int8_t  SomfyShadeController::getMaxShadeOrder() { return 0; }
int8_t  SomfyShadeController::getMaxGroupOrder() { return 0; }
int8_t  SomfyShadeController::getMaxRoomOrder()  { return 0; }
uint32_t SomfyShadeController::getNextRemoteAddress(uint8_t) { return 0; }
bool SomfyShadeController::begin()  { return false; }
void SomfyShadeController::loop()   {}
void SomfyShadeController::end()    {}
bool SomfyShadeController::deleteShade(uint8_t) { return false; }
bool SomfyShadeController::deleteGroup(uint8_t) { return false; }
bool SomfyShadeController::deleteRoom(uint8_t)  { return false; }
SomfyShade *SomfyShadeController::addShade()                 { return nullptr; }
SomfyShade *SomfyShadeController::addShade(JsonObject &)     { return nullptr; }
SomfyGroup *SomfyShadeController::addGroup()                 { return nullptr; }
SomfyGroup *SomfyShadeController::addGroup(JsonObject &)     { return nullptr; }
SomfyRoom  *SomfyShadeController::addRoom()                  { return nullptr; }
SomfyRoom  *SomfyShadeController::addRoom(JsonObject &)      { return nullptr; }
bool SomfyShadeController::linkRepeater(uint32_t)   { return false; }
bool SomfyShadeController::unlinkRepeater(uint32_t) { return false; }
void SomfyShadeController::compressRepeaters()  {}
uint8_t SomfyShadeController::repeaterCount()  { return 0; }
uint8_t SomfyShadeController::shadeCount()     { return 0; }
uint8_t SomfyShadeController::groupCount()     { return 0; }
uint8_t SomfyShadeController::roomCount()      { return 0; }
void SomfyShadeController::toJSONShades(JsonResponse &)    {}
void SomfyShadeController::toJSONGroups(JsonResponse &)    {}
void SomfyShadeController::toJSONRooms(JsonResponse &)     {}
void SomfyShadeController::toJSONRepeaters(JsonResponse &) {}
SomfyShade *SomfyShadeController::getShadeById(uint8_t)        { return nullptr; }
SomfyShade *SomfyShadeController::findShadeByRemoteAddress(uint32_t) { return nullptr; }
SomfyGroup *SomfyShadeController::getGroupById(uint8_t)        { return nullptr; }
SomfyGroup *SomfyShadeController::findGroupByRemoteAddress(uint32_t) { return nullptr; }
SomfyRoom  *SomfyShadeController::getRoomById(uint8_t)         { return nullptr; }
void SomfyShadeController::sendFrame(somfy_frame_t &, uint8_t) {}
void SomfyShadeController::processFrame(somfy_frame_t &, bool) {}
void SomfyShadeController::emitState(uint8_t)  {}
void SomfyShadeController::publish()           {}
void SomfyShadeController::processWaitingFrame() {}
void SomfyShadeController::commit()            {}
void SomfyShadeController::writeBackup()       {}
bool SomfyShadeController::loadShadesFile(const char *) { return false; }

// ── Transceiver stub implementations ───────────────────────────────────────
#include "../../main/somfy/SomfyTransceiver.h"

bool Transceiver::begin()  { return false; }
void Transceiver::loop()   {}
bool Transceiver::end()    { return false; }
bool Transceiver::receive(somfy_rx_t *) { return false; }
void Transceiver::clearReceived()       {}
void Transceiver::enableReceive()       {}
void Transceiver::disableReceive()      {}
somfy_frame_t &Transceiver::lastFrame() { static somfy_frame_t f; return f; }
void Transceiver::sendFrame(byte *, uint8_t, uint8_t) {}
void Transceiver::beginTransmit()  {}
void Transceiver::endTransmit()    {}
void Transceiver::emitFrame(somfy_frame_t *, somfy_rx_t *) {}
void Transceiver::beginFrequencyScan()  {}
void Transceiver::endFrequencyScan()    {}
void Transceiver::processFrequencyScan(bool) {}
void Transceiver::emitFrequencyScan(uint8_t) {}
bool transceiver_stub_uses_pin = false;
bool Transceiver::usesPin(uint8_t) { return transceiver_stub_uses_pin; }
bool Transceiver::save()           { return false; }
bool Transceiver::fromJSON(JsonObject &) { return false; }
void Transceiver::toJSON(JsonResponse &) {}
void Transceiver::handleReceive()   {}
void Transceiver::handleReceiveISR(void *) {}
void transceiver_config_t::fromJSON(JsonObject &) {}
void transceiver_config_t::toJSON(JsonResponse &) {}
void transceiver_config_t::save()   {}
void transceiver_config_t::load()   {}
void transceiver_config_t::apply()  {}
void transceiver_config_t::removeNVSKey(const char *) {}
