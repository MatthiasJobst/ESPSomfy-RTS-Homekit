// main.cpp — Application entry point.
// Provides app_main() directly; arduino-esp32 is initialised via initArduino()
// so all Arduino-based subsystems (WiFi, LittleFS, etc.) still work while we
// own the FreeRTOS task structure.  app_main() is kept minimal (thin stack);
// all initialisation and the poll loop run inside mainLoop which has a full
// 8192-byte stack.

#include <cstring>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <mdns.h>
#include <nvs.h>
#include <esp32-hal.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "AppConfig.h"
#include "ConfigSettings.h"
#include "ControllerNetwork.h"
#include "GitOTA.h"
#include "HomeKit.h"
#include "AuthService.h"
#include "MQTT.h"
#include "OtaService.h"
#include "Sockets.h"
#include "SomfyShadeController.h"
#include "SomfyStateMachine.h"
#include "Utils.h"
#include "Web.h"

static void setCodeForHomeKit();
static void logNvsUsage();

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
ControllerNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
SomfyStateMachine stateMachine(somfy);
MQTTClass mqtt;
GitUpdater git;
HomeKitClass homekit;
OtaService ota;
AuthService auth;

static const char *s_TAG = "Main";

/**
 * @brief Main application task — initialises all subsystems and runs the poll loop.
 *
 * @note Registered with the task watchdog (7 s timeout). Any blocking call on
 *       this task that exceeds 7 s will trigger a panic.
 */
static void mainLoop(void *)
{
    initArduino();
    // Changing the log level needs to be here, after initArduino() has set up
    // the default logger, but before any other initialisation that might log
    // messages. ESP_LOG_DEBUG logs all; ESP_LOG_INFO disables debug logs.
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(s_TAG, "Startup/Boot....");
    ESP_LOGI(s_TAG, "Mounting File System...");
    if (LittleFS.begin(true))
        ESP_LOGI(s_TAG, "File system mounted successfully");
    else
        ESP_LOGE(s_TAG, "Error mounting file system");
    settings.begin();
    setCodeForHomeKit();
    logNvsUsage();
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(s_TAG, "Initializing web server...");
    webServer.startup();
    webServer.begin();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(s_TAG, "Setting up network...");
    net.setup();
    // Register with the task watchdog before stateMachine.begin() so that
    // esp_task_wdt_reset() calls inside initialisation don't fail with
    // "task not found".
    static const esp_task_wdt_config_t wdt_cfg = {.timeout_ms = 7000, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);
    ESP_LOGI(s_TAG, "Initializing Somfy controller...");
    stateMachine.begin();

    uint32_t iterMaxMs = 0;
    uint32_t iterMaxReportedAt = 0;
    while (true) {
        if (rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
            ESP_LOGI(s_TAG, "Rebooting after %d ms", rebootDelay.rebootTime - (millis() - rebootDelay.rebootTime));
            net.end();
            esp_restart();
            return;
        }
        uint32_t iterStart = millis();
        uint32_t timing = iterStart;

        net.loop();
        if (millis() - timing > 100) ESP_LOGI(s_TAG, "Timing Net: %ldms", millis() - timing);
        timing = millis();
        stateMachine.loop();
        if (millis() - timing > 100) ESP_LOGI(s_TAG, "Timing Somfy: %ldms", millis() - timing);
        timing = millis();
        if (net.connected() || net.softAPOpened) {
            if (!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
                git.loop();
            }
            webServer.loop();
            if (millis() - timing > 100) ESP_LOGI(s_TAG, "Timing WebServer: %ldms", millis() - timing);
            if (!net.softAPOpened) homekit.begin();
            // esp-homekit-sdk's hap_mdns_init() hard-codes the mDNS hostname to
            // "MyHost", so <hostname>.local stops resolving once HAP starts.
            // Restore our hostname and register _http._tcp (which would also be
            // dropped if registered before hap_init()).
            static bool s_httpMdnsRegistered = false;
            if (!s_httpMdnsRegistered && homekit.isStarted()) {
                mdns_hostname_set(settings.hostname);
                if (mdns_service_add(NULL, "_http", "_tcp", APP_HTTP_PORT, NULL, 0) == ESP_OK) {
                    ESP_LOGI(s_TAG, "mDNS _http._tcp registered, hostname=%s", settings.hostname);
                    s_httpMdnsRegistered = true;
                }
            }
        }
        // Poll WebSocket unconditionally — must run every iteration regardless of
        // WiFi/AP state so the HTTP-101 upgrade handshake is never starved.
        timing = millis();
        sockEmit.loop();
        if (millis() - timing > 100) ESP_LOGI(s_TAG, "Timing Socket: %ldms", millis() - timing);
        if (rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
            net.end();
            esp_restart();
        }
        esp_task_wdt_reset();

        uint32_t iterMs = millis() - iterStart;
        if (iterMs > iterMaxMs) iterMaxMs = iterMs;
        if (iterMs > 2000) ESP_LOGW(s_TAG, "Main loop iteration took %lums (net+somfy+git+web+sock)", iterMs);
        if (millis() - iterMaxReportedAt > 60000) {
            ESP_LOGI(s_TAG, "Main loop iteration max over last 60s: %lums", iterMaxMs);
            iterMaxMs = 0;
            iterMaxReportedAt = millis();
        }
    }
}

/**
 * @brief Load the HomeKit pairing code from NVS, or generate and persist a new one.
 */
static void setCodeForHomeKit()
{
    {
        nvs_handle_t h;
        char code[12] = {};
        size_t len = sizeof(code);
        bool found =
            nvs_open("homekit", NVS_READONLY, &h) == ESP_OK && nvs_get_str(h, "setup_code", code, &len) == ESP_OK;
        if (found) {
            nvs_close(h);
            homekit.setCode(code);
        } else {
            const char *generated = homekit.prefab();
            if (nvs_open("homekit", NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_str(h, "setup_code", generated);
                nvs_commit(h);
                nvs_close(h);
            }
        }
    }
}

/**
 * @brief Log NVS partition usage and a per-namespace entry breakdown.
 *
 * Diagnostic for the `nvs` partition filling up (seen as PHY calibration
 * write failures, ESP_ERR_NVS_NOT_ENOUGH_SPACE 0x1105). HomeKit (HAP) stores
 * its runtime pairing data in this same partition, so it can crowd out the app
 * config and the PHY cal blob. Prints overall used/free/total entries and the
 * number of keys per namespace so the dominant consumer can be identified.
 */
static void logNvsUsage()
{
    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK) {
        ESP_LOGI(s_TAG, "NVS 'nvs' partition: used=%zu free=%zu total=%zu entries (%.0f%% full), namespaces=%zu",
                 stats.used_entries, stats.free_entries, stats.total_entries,
                 stats.total_entries ? 100.0f * stats.used_entries / stats.total_entries : 0.0f,
                 stats.namespace_count);
    } else {
        ESP_LOGW(s_TAG, "NVS stats unavailable");
        return;
    }

    // Tally keys per namespace. Namespace names are short (<= 15 chars); track a
    // small fixed set of distinct names and their counts.
    struct {
        char name[16];
        uint32_t count;
    } seen[24] = {};
    size_t distinct = 0;
    uint32_t total = 0;

    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        total++;
        size_t i = 0;
        for (; i < distinct; i++) {
            if (strncmp(seen[i].name, info.namespace_name, sizeof(seen[i].name)) == 0) break;
        }
        if (i == distinct && distinct < sizeof(seen) / sizeof(seen[0])) {
            strncpy(seen[distinct].name, info.namespace_name, sizeof(seen[distinct].name) - 1);
            distinct++;
        }
        if (i < sizeof(seen) / sizeof(seen[0])) seen[i].count++;
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    for (size_t i = 0; i < distinct; i++) {
        ESP_LOGI(s_TAG, "  NVS ns [%s]: %lu keys", seen[i].name, seen[i].count);
    }
    ESP_LOGI(s_TAG, "NVS total keys enumerated: %lu", total);
}

/** @brief Linker stub — arduino-esp32 references setup() even when CONFIG_AUTOSTART_ARDUINO=n. */
void setup() {}
/** @brief Linker stub — arduino-esp32 references loop() even when CONFIG_AUTOSTART_ARDUINO=n. */
void loop() {}

/**
 * @brief ESP-IDF application entry point.
 *
 * Kept minimal because main_task has a small default stack (~3584 bytes).
 * Spawns mainLoop as a FreeRTOS task with an 8192-byte stack on core 1.
 */
extern "C" void app_main()
{
    xTaskCreatePinnedToCore(mainLoop, "main", 8192, NULL, 6, NULL, 1);
}
