// Entry point for the ESP-IDF build.
// arduino-esp32 (as an IDF component) provides app_main(), which starts a
// FreeRTOS task that calls setup() once and then loop() repeatedly.
// This file is functionally equivalent to SomfyController.ino.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "ConfigSettings.h"
#include "SomfyNetwork.h"
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
SomfyNetwork net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;

static const char *TAG = "Main";

uint32_t oldheap = 0;
void setup() {
  // Raise loop task priority above the HAP main task so it cannot pre-empt
  // the WebSocket / web server poll loop.  HAP is configured to run at
  // priority 5 (see HomeKit.cpp); we run at 6 to stay above it while still
  // being pre-emptable by IDF system tasks (lwIP/wifi at 23, etc.).
  // Must be first line of setup().
  vTaskPrioritySet(NULL, 6);
  ESP_LOGI(TAG, "Startup/Boot....");
  ESP_LOGI(TAG, "Mounting File System...");
  if(LittleFS.begin(true)) ESP_LOGI(TAG, "File system mounted successfully");
  else ESP_LOGE(TAG, "Error mounting file system");
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  delay(10);
  ESP_LOGI(TAG, "Initializing web server...");
  webServer.startup();
  webServer.begin();
  delay(1000);
  ESP_LOGI(TAG, "Setting up network...");
  net.setup();
  ESP_LOGI(TAG, "Initializing Somfy controller...");
  somfy.begin();
  // homekit.begin() is deferred — called in SomfyNetwork::setConnected() after mDNS is up.
  //git.checkForUpdate();
  // IDF v5: WDT API uses a config struct. arduino-esp32 may have already
  // initialized the WDT; use reconfigure to set our preferred timeout.
  static const esp_task_wdt_config_t wdt_cfg = {.timeout_ms = 7000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL); //add current thread to WDT watch
}

void loop() {
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    ESP_LOGI(TAG, "Rebooting after %d ms", rebootDelay.rebootTime - (millis() - rebootDelay.rebootTime));
    net.end();
    ESP.restart();
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
  timing = millis();
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
  }
  esp_task_wdt_reset();
}
