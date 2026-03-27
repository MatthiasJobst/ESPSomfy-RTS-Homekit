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
#include "esp_log.h"
#include "ConfigSettings.h"
#include "ControllerNetwork.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "SomfyController.h"
#include "MQTT.h"
#include "GitOTA.h"
#include "HomeKit.h"

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
ControllerNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;

static const char *TAG = "Main";

static void mainLoop(void*) {
  initArduino();
  ESP_LOGI(TAG, "Startup/Boot....");
  ESP_LOGI(TAG, "Mounting File System...");
  if(LittleFS.begin(true)) ESP_LOGI(TAG, "File system mounted successfully");
  else ESP_LOGE(TAG, "Error mounting file system");
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_LOGI(TAG, "Initializing web server...");
  webServer.startup();
  webServer.begin();
  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGI(TAG, "Setting up network...");
  net.setup();
  ESP_LOGI(TAG, "Initializing Somfy controller...");
  somfy.begin();
  // homekit.begin() is deferred — called in ControllerNetwork::setConnected() after mDNS is up.
  static const esp_task_wdt_config_t wdt_cfg = {.timeout_ms = 7000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL);

  while(true) {
    if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
      ESP_LOGI(TAG, "Rebooting after %d ms", rebootDelay.rebootTime - (millis() - rebootDelay.rebootTime));
      net.end();
      esp_restart();
      return;
    }
    uint32_t timing = millis();

    net.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Net: %ldms", millis() - timing);
    timing = millis();
    esp_task_wdt_reset();
    somfy.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Somfy: %ldms", millis() - timing);
    timing = millis();
    esp_task_wdt_reset();
    if(net.connected() || net.softAPOpened) {
      if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
        git.loop();
        esp_task_wdt_reset();
      }
      webServer.loop();
      esp_task_wdt_reset();
      if(millis() - timing > 100) ESP_LOGI(TAG, "Timing WebServer: %ldms", millis() - timing);
      esp_task_wdt_reset();
    }
    // Poll WebSocket unconditionally — must run every iteration regardless of
    // WiFi/AP state so the HTTP-101 upgrade handshake is never starved.
    timing = millis();
    sockEmit.loop();
    if(millis() - timing > 100) ESP_LOGI(TAG, "Timing Socket: %ldms", millis() - timing);
    esp_task_wdt_reset();
    if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
      net.end();
      esp_restart();
    }
    esp_task_wdt_reset();
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
