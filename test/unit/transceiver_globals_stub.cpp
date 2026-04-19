// transceiver_globals_stub.cpp — globals for the transceiver_tests executable.
// Mirrors globals_stub.cpp but omits the SomfyTransceiver method stubs and the
// `bit_length` definition (both come from the real SomfyTransceiver.cpp compiled here).

// ── NVS stub state ─────────────────────────────────────────────────────────
#include "nvs.h"
std::unordered_map<std::string, NvsStore> nvs_ns_stores;
std::unordered_map<uint32_t, std::string> nvs_handle_ns;
uint32_t nvs_stub_next_handle = 1;

// ── GPIO stub state ────────────────────────────────────────────────────────
#include "driver/gpio.h"
std::unordered_map<int, uint32_t> gpio_pin_levels;
void (*gpio_last_isr)(void *)    = nullptr;
void  *gpio_last_isr_arg         = nullptr;
bool   gpio_intr_enabled         = true;

// ── MQTT stub state ────────────────────────────────────────────────────────
#include "MQTT.h"
bool mqtt_connected_flag = false;
std::unordered_map<std::string, std::string> mqtt_published;
std::unordered_map<std::string, std::string> mqtt_unpublished;

// ── CC1101 stub instance ────────────────────────────────────────────────────
#include "ELECHOUSE_CC1101_SRC_DRV.h"
ELECHOUSE_CC1101 ELECHOUSE_cc1101;
bool cc1101_init_ok = true;

// ── RMT stub state ─────────────────────────────────────────────────────────
#include "driver/rmt_tx.h"
rmt_tx_done_callback_t rmt_stub_done_cb     = nullptr;
void                  *rmt_stub_done_cb_ctx = nullptr;
rmt_stub_queued_t      rmt_stub_queue[RMT_STUB_QUEUE_MAX] = {};
int                    rmt_stub_queue_count = 0;

// ── Test clocks ────────────────────────────────────────────────────────────
#include "Arduino.h"
uint64_t test_clock_ms = 0;
uint64_t test_clock_us = 0;

// ── Project headers ────────────────────────────────────────────────────────
#include "ConfigSettings.h"
#include "WResp.h"
#include "Sockets.h"
#include "GitOTA.h"
#include "HomeKit.h"
#include "../../main/somfy/SomfyShadeController.h"
#include "ShadeConfigFile.h"
bool stub_shadeconfig_exists = false;
int  stub_save_call_count    = 0;
int  stub_backup_call_count  = 0;

// ── Global instances ────────────────────────────────────────────────────────
SomfyShadeController somfy;
SocketEmitter        sockEmit;
ConfigSettings       settings;
MQTTClass            mqtt;
GitUpdater           git;
HomeKitClass         homekit;

#include "../../main/compat/preferences.h"
Preferences pref;
