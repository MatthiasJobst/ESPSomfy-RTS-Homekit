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
uint64_t test_clock_us = 0;

// Now pull in project types (stubs intercept the heavy headers)
#include "ConfigSettings.h"
#include "WResp.h"
#include "MQTT.h"
#include "Sockets.h"
#include "GitOTA.h"
#include "HomeKit.h"

// SomfyShadeController.h lives in main/somfy/ so it can't be stubbed via stubs dir.
// We include it directly — production methods are now compiled from SomfyShadeController.cpp.
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyStateMachine.h"

// ── ShadeConfigFile stub state ─────────────────────────────────────────────
#include "ShadeConfigFile.h"
bool stub_shadeconfig_exists = false;
int  stub_save_call_count    = 0;
int  stub_backup_call_count  = 0;

// ── Global instances ────────────────────────────────────────────────────────
SomfyShadeController somfy;
SomfyStateMachine stateMachine(somfy);
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

// ── Transceiver stub implementations ───────────────────────────────────────
#include "../../main/somfy/SomfyTransceiver.h"

bool SomfyTransceiver::begin()  { return false; }
void SomfyTransceiver::loop()   {}
bool SomfyTransceiver::end()    { return false; }
bool SomfyTransceiver::receive(somfy_rx_t *) { return false; }
void SomfyTransceiver::clearReceived()       {}
void SomfyTransceiver::enableReceive()       {}
void SomfyTransceiver::disableReceive()      {}
somfy_frame_t &SomfyTransceiver::lastFrame() { static somfy_frame_t f; return f; }
void SomfyTransceiver::beginTransmit()  {}
void SomfyTransceiver::endTransmit()    {}
void SomfyTransceiver::emitFrame(somfy_frame_t *, somfy_rx_t *) {}
void SomfyTransceiver::beginFrequencyScan()  {}
void SomfyTransceiver::endFrequencyScan()    {}
void SomfyTransceiver::processFrequencyScan(bool) {}
void SomfyTransceiver::emitFrequencyScan(uint8_t) {}
bool transceiver_stub_uses_pin = false;
// Test-controllable TX state: tests toggle transceiver_stub_tx_busy to exercise
// the "TX busy → skip" branches, and inspect transceiver_stub_begin_tx_count to
// confirm whether a frame transmission was actually started.
bool transceiver_stub_tx_busy = false;
int  transceiver_stub_begin_tx_count = 0;
void SomfyTransceiver::beginFrameTx(somfy_frame_t &, uint8_t) { transceiver_stub_begin_tx_count++; }
void SomfyTransceiver::beginRawFrameTx(byte *, uint8_t, uint8_t) {}
bool SomfyTransceiver::txBusy() { return transceiver_stub_tx_busy; }
bool SomfyTransceiver::usesPin(uint8_t) { return transceiver_stub_uses_pin; }
bool SomfyTransceiver::save()           { return false; }
bool SomfyTransceiver::fromJSON(JsonObject &) { return false; }
void SomfyTransceiver::toJSON(JsonResponse &) {}
void SomfyTransceiver::handleReceive()   {}
void SomfyTransceiver::handleReceiveISR(void *) {}
void transceiver_config_t::fromJSON(JsonObject &) {}
void transceiver_config_t::toJSON(JsonResponse &) {}
void transceiver_config_t::save()   {}
void transceiver_config_t::load()   {}
void transceiver_config_t::apply()  {}
void transceiver_config_t::removeNVSKey(const char *) {}
