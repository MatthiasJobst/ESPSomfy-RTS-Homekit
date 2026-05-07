// main.cpp — Application entry point.
// Provides app_main() directly; arduino-esp32 is initialised via initArduino()
// so all Arduino-based subsystems (WiFi, LittleFS, etc.) still work while we
// own the FreeRTOS task structure.  app_main() is kept minimal (thin stack);
// all initialisation and the poll loop run inside mainLoop which has a full
// 8192-byte stack.

#include <esp32-hal.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <mdns.h>
#include "esp_log.h"
#include "AppConfig.h"
#include "ConfigSettings.h"
#include "ControllerNetwork.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "SomfyShadeController.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "HomeKit.h"
#include <nvs.h>

static void setCodeForHomeKit();

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
ControllerNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;
HomeKitClass homekit;

static const char *TAG = "Main";

static void mainLoop(void*) {
  initArduino();
  // Changing the log level needs to be here, after initArduino() has set up the default logger, but before any other initialisation that might log messages. Setting it to ESP_LOG_DEBUG will log all messages; set to ESP_LOG_INFO to disable debug logs.
  esp_log_level_set("*", ESP_LOG_INFO);
  ESP_LOGI(TAG, "Startup/Boot....");
  ESP_LOGI(TAG, "Mounting File System...");
  if(LittleFS.begin(true)) ESP_LOGI(TAG, "File system mounted successfully");
  else ESP_LOGE(TAG, "Error mounting file system");
  settings.begin();
  setCodeForHomeKit();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_LOGI(TAG, "Initializing web server...");
  webServer.startup();
  webServer.begin();
  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGI(TAG, "Setting up network...");
  net.setup();
  // Register with the task watchdog before somfy.begin() so that
  // esp_task_wdt_reset() calls inside initialisation don't fail with
  // "task not found".
  static const esp_task_wdt_config_t wdt_cfg = {.timeout_ms = 7000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL);
  ESP_LOGI(TAG, "Initializing Somfy controller...");
  somfy.begin();

  uint32_t iterMaxMs = 0;
  uint32_t iterMaxReportedAt = 0;
  while(true) {
    if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
      ESP_LOGI(TAG, "Rebooting after %d ms", rebootDelay.rebootTime - (millis() - rebootDelay.rebootTime));
      net.end();
      esp_restart();
      return;
    }
    uint32_t iterStart = millis();
    uint32_t timing = iterStart;

    net.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Net: %ldms", millis() - timing);
    timing = millis();
    somfy.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Somfy: %ldms", millis() - timing);
    timing = millis();
    if(net.connected() || net.softAPOpened) {
      if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
        git.loop();
      }
      webServer.loop();
      if(millis() - timing > 100) ESP_LOGI(TAG, "Timing WebServer: %ldms", millis() - timing);
      if(!net.softAPOpened) homekit.begin();
      // esp-homekit-sdk's hap_mdns_init() hard-codes the mDNS hostname to
      // "MyHost", so <hostname>.local stops resolving once HAP starts.
      // Restore our hostname and register _http._tcp (which would also be
      // dropped if registered before hap_init()).
      static bool httpMdnsRegistered = false;
      if(!httpMdnsRegistered && homekit.isStarted()) {
        mdns_hostname_set(settings.hostname);
        if(mdns_service_add(NULL, "_http", "_tcp", APP_HTTP_PORT, NULL, 0) == ESP_OK) {
          ESP_LOGI(TAG, "mDNS _http._tcp registered, hostname=%s", settings.hostname);
          httpMdnsRegistered = true;
        }
      }
    }
    // Poll WebSocket unconditionally — must run every iteration regardless of
    // WiFi/AP state so the HTTP-101 upgrade handshake is never starved.
    timing = millis();
    sockEmit.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Socket: %ldms", millis() - timing);
    if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
      net.end();
      esp_restart();
    }
    esp_task_wdt_reset();

    uint32_t iterMs = millis() - iterStart;
    if(iterMs > iterMaxMs) iterMaxMs = iterMs;
    if(iterMs > 2000) ESP_LOGW(TAG, "Main loop iteration took %lums (net+somfy+git+web+sock)", iterMs);
    if(millis() - iterMaxReportedAt > 60000) {
      ESP_LOGI(TAG, "Main loop iteration max over last 60s: %lums", iterMaxMs);
      iterMaxMs = 0;
      iterMaxReportedAt = millis();
    }
  }
}

static void setCodeForHomeKit() {
  {
    nvs_handle_t h;
    char code[12] = {};
    size_t len = sizeof(code);
    bool found = nvs_open("homekit", NVS_READONLY, &h) == ESP_OK &&
                 nvs_get_str(h, "setup_code", code, &len) == ESP_OK;
    if (found) {
      nvs_close(h);
      homekit.setCode(code);
    } else {
      const char *generated = homekit.prefab();
      if (nvs_open("homekit", NVS_READWRITE, &h) == ESP_OK){
        nvs_set_str(h, "setup_code", generated);
        nvs_commit(h);
        nvs_close(h);
      }
    }
  }
} 

// arduino-esp32 compiles loopTask() unconditionally; it references setup()/loop()
// even when CONFIG_AUTOSTART_ARDUINO=n. These stubs satisfy the linker.
void setup() {}
void loop()  {}

extern "C" void app_main() {
  // Keep app_main minimal — main_task has a small default stack (~3584 bytes).
  // All initialisation and the poll loop run inside mainLoop (8192-byte stack).
  xTaskCreatePinnedToCore(mainLoop, "main", 8192, NULL, 6, NULL, 1);
}
