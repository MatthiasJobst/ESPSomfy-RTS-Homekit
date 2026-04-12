// SomfyRemote.cpp — SomfyRemote: rolling-code management, command dispatch,
// GPIO relay/remote handling, frame send and repeat. SomfyLinkedRemote constructor.
#include "compat/preferences.h"
#include "esp_log.h"
#ifdef SOMFY_TX_RMT
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "GitOTA.h"
#include "SomfyRemote.h"
#include "SomfyTransceiver.h"
#include "SomfyShade.h"
#include "SomfyController.h"
#include "Sockets.h"
#include "MQTT.h"

static const char *TAG = "SomfyRemote";

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern ConfigSettings settings;
extern MQTTClass mqtt;
extern Preferences pref;
extern GitUpdater git;

bool SomfyRemote::simMy() { return this->flags.simMy(); }

void SomfyRemote::setSimMy(bool bSimMy) { this->flags.setSimMy(bSimMy); }

bool SomfyRemote::hasSunSensor() { return this->flags.hasSunSensor(); }

bool SomfyRemote::hasLight() { return this->flags.hasLight(); }

void SomfyRemote::setSunSensor(bool bHasSensor) { this->flags.setSunSensor(bHasSensor); }

void SomfyRemote::setLight(bool bHasLight) { this->flags.setLight(bHasLight); }

void SomfyRemote::triggerGPIOs(somfy_frame_t &frame) { }

void SomfyRemote::toJSON(JsonResponse &json) {
  json.addElem("remoteAddress", (uint32_t)this->getRemoteAddress());
  json.addElem("lastRollingCode", (uint32_t)this->lastRollingCode);
}

void SomfyRemote::setRemoteAddress(uint32_t address) { this->m_remoteAddress = address; snprintf(this->m_remotePrefId, sizeof(this->m_remotePrefId), "_%lu", (unsigned long)this->m_remoteAddress); }

uint32_t SomfyRemote::getRemoteAddress() { return this->m_remoteAddress; }

somfy_commands SomfyRemote::transformCommand(somfy_commands cmd) {
  if(this->flipCommands) {
    switch(cmd) {
      case somfy_commands::Up:
        return somfy_commands::Down;
      case somfy_commands::MyUp:
        return somfy_commands::MyDown;
      case somfy_commands::Down:
        return somfy_commands::Up;
      case somfy_commands::MyDown:
        return somfy_commands::MyUp;
      case somfy_commands::StepUp:
        return somfy_commands::StepDown;
      case somfy_commands::StepDown:
        return somfy_commands::StepUp;
      default:
        break;
    }
  }
  return cmd;
}

void SomfyRemote::sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat) {
  SomfyFlag frameFlags = this->flags;
  if(isWindy > 0) frameFlags.setWindy(true);
  if(isSunny > 0) frameFlags.setSunny(true);
  if(isWindy == 0) frameFlags.setWindy(false);
  if(isSunny == 0) frameFlags.setSunny(false);

  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.rollingCode = (uint16_t)frameFlags.getRollingCode();
  this->lastFrame.encKey = 160; // Sensor commands are always encryption code 160.
  this->lastFrame.cmd = somfy_commands::Sensor;
  this->lastFrame.processed = false;
  ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
  ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
  ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
  ESP_LOGI(TAG, "REPEAT: %d", repeat);
  somfy.sendFrame(this->lastFrame, repeat);
  somfy.processFrame(this->lastFrame, true);
}

void SomfyRemote::sendCommand(somfy_commands cmd) { this->sendCommand(cmd, this->repeats); }

void SomfyRemote::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  this->lastFrame.rollingCode = this->getNextRollingCode();
  this->lastFrame.remoteAddress = this->getRemoteAddress();
  this->lastFrame.cmd = this->transformCommand(cmd);
  this->lastFrame.repeats = repeat;
  this->lastFrame.bitLength = this->bitLength;
  this->lastFrame.stepSize = stepSize;
  this->lastFrame.valid = true;
  // Match the encKey to the rolling code.  These keys range from 160 to 175.
  this->lastFrame.encKey = 0xA0 | static_cast<uint8_t>(this->lastFrame.rollingCode & 0x000F);
  this->lastFrame.proto = this->proto;
  if(this->lastFrame.bitLength == 0) this->lastFrame.bitLength = bit_length;
  if(this->lastFrame.rollingCode == 0) ESP_LOGE(TAG, "ERROR: Setting rcode to 0");
  this->p_lastRollingCode(this->lastFrame.rollingCode);
  this->lastFrame.processed = false;
  if(this->proto == radio_proto::GP_Relay) {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "SETTING GPIO");
  }
  else if(this->proto == radio_proto::GP_Remote) {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "TRIGGER GPIO");
    this->triggerGPIOs(this->lastFrame);
  }
  else {
    ESP_LOGI(TAG, "CMD: %s", translateSomfyCommand(this->lastFrame.cmd).c_str());
    ESP_LOGI(TAG, "ADDR: %d", this->lastFrame.remoteAddress);
    ESP_LOGI(TAG, "RCODE: %d", this->lastFrame.rollingCode);
    ESP_LOGI(TAG, "REPEAT: %d", repeat);
    somfy.sendFrame(this->lastFrame, repeat);
  }
  somfy.processFrame(this->lastFrame, true);
}

bool SomfyRemote::isLastCommand(somfy_commands cmd) {
  if(this->lastFrame.cmd != cmd || this->lastFrame.rollingCode != this->lastRollingCode) {
    ESP_LOGI(TAG, "Not the last command %d: %d - %d", static_cast<uint8_t>(this->lastFrame.cmd), this->lastFrame.rollingCode, this->lastRollingCode);
    return false;
  }
  return true;
}

void SomfyRemote::repeatFrame(uint8_t repeat) {
  if(this->proto == radio_proto::GP_Relay)
    return;
  else if(this->proto == radio_proto::GP_Remote) {
    this->triggerGPIOs(this->lastFrame);
    return;
  }
#ifdef SOMFY_TX_RMT
  while(somfy.transceiver.txBusy()) vTaskDelay(1);
  somfy.transceiver.beginTransmit();
  somfy.transceiver.beginFrameTx(this->lastFrame, repeat);
  // endTransmit() deferred — Transceiver::loop() handles it when txBusy() clears.
#else
  somfy.transceiver.beginTransmit();
  byte frm[10];
  this->lastFrame.encodeFrame(frm);
  this->lastFrame.repeats++;
  somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 2 : 12, this->bitLength);
  for(uint8_t i = 0; i < repeat; i++) {
    this->lastFrame.repeats++;
    if(this->lastFrame.bitLength == 80) this->lastFrame.encode80BitFrame(&frm[0], this->lastFrame.repeats);
    somfy.transceiver.sendFrame(frm, this->bitLength == 56 ? 7 : 6, this->bitLength);
  }
  somfy.transceiver.endTransmit();
#endif
}

uint16_t SomfyRemote::getNextRollingCode() {
  pref.begin("ShadeCodes");
  uint16_t code = pref.getUShort(this->m_remotePrefId, 0);
  code++;
  pref.putUShort(this->m_remotePrefId, code);
  pref.end();
  this->p_lastRollingCode(code);
  ESP_LOGI(TAG, "Getting Next Rolling code %d", this->lastRollingCode);
  return code;
}

uint16_t SomfyRemote::p_lastRollingCode(uint16_t code) {
  uint16_t old = this->lastRollingCode;
  this->lastRollingCode = code;
  return old;
}

uint16_t SomfyRemote::setRollingCode(uint16_t code) {
  if(this->lastRollingCode != code) {
    pref.begin("ShadeCodes");
    pref.putUShort(this->m_remotePrefId, code);
    pref.end();
    this->lastRollingCode = code;
    ESP_LOGI(TAG, "Setting Last Rolling code %d", this->lastRollingCode);
  }
  return code;
}

SomfyLinkedRemote::SomfyLinkedRemote() {}

// Transceiver Implementation
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3
