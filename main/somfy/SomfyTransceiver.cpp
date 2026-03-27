// SomfyTransceiver.cpp — CC1101 transceiver implementation: interrupt handler (IRAM),
// frame receive/decode pipeline, frequency scan, all Transceiver::* and
// transceiver_config_t::* method bodies (send, apply, load/save NVS, JSON I/O).
// Module-local state: rxmode, bit_length, somfy_rx, rx_queue, tx_queue, freq scan vars.
#include <Preferences.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <esp_chip_info.h>
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "SomfyTransceiver.h"
#include "SomfyController.h"
#include "Sockets.h"
#include "MQTT.h"
#include "GitOTA.h"

static const char *TAG = "SomfyTransceiver";
// Module-level state (Transceiver private)
uint8_t rxmode = 0;   // 0=off 1=receive 3=frequency-scan
uint8_t bit_length = 56;  // current protocol frame width; set by apply(), read by sendCommand()
static int interruptPin = 0;

#define SETMY_REPEATS 35
#define TILT_REPEATS 15
#define TX_QUEUE_DELAY 100

#define SYMBOL 640
#if defined(ESP8266)
    #define RECEIVE_ATTR ICACHE_RAM_ATTR
#elif defined(ESP32)
    #define RECEIVE_ATTR IRAM_ATTR
#else
    #define RECEIVE_ATTR
#endif
#define TOLERANCE_MIN 0.7
#define TOLERANCE_MAX 1.3
static const uint32_t tempo_wakeup_pulse = 9415;
static const uint32_t tempo_wakeup_min = 9415 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_max = 9415 * TOLERANCE_MAX;
static const uint32_t tempo_wakeup_silence = 89565;
static const uint32_t tempo_wakeup_silence_min = 89565 * TOLERANCE_MIN;
static const uint32_t tempo_wakeup_silence_max = 89565 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_hw_min = SYMBOL * 4 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_hw_max = SYMBOL * 4 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_sw_min = 4850 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_sw_max = 4850 * TOLERANCE_MAX;
static const uint32_t tempo_half_symbol_min = SYMBOL * TOLERANCE_MIN;
static const uint32_t tempo_half_symbol_max = SYMBOL * TOLERANCE_MAX;
static const uint32_t tempo_symbol_min = SYMBOL * 2 * TOLERANCE_MIN;
static const uint32_t tempo_symbol_max = SYMBOL * 2 * TOLERANCE_MAX;
static const uint32_t tempo_if_gap = 30415;  // Gap between frames
static int16_t  bitMin = SYMBOL * TOLERANCE_MIN;
static somfy_rx_t somfy_rx;
static somfy_rx_queue_t rx_queue;
static somfy_tx_queue_t tx_queue;
float currFreq = 433.0f;
int currRSSI = -100;
float markFreq = 433.0f;
int markRSSI = -100;
uint32_t lastScan = 0;

extern SomfyShadeController somfy;
extern SocketEmitter sockEmit;
extern ConfigSettings settings;
extern MQTTClass mqtt;
extern Preferences pref;
extern GitUpdater git;

void Transceiver::sendFrame(byte *frame, uint8_t sync, uint8_t bitLength) {
  if(!this->config.enabled) return;
  uint32_t pin = 1 << this->config.TXPin;
  if (sync == 2 || sync == 12) {  // Only with the first frame.  Repeats do not get a wakeup pulse.
    // All information online for the wakeup pulse appears to be incorrect.  While there is a wakeup
    // pulse it only sends an initial pulse.  There is no further delay after this.
    
    // Wake-up pulse
    ESP_LOGD(TAG, "Sending wakeup pulse: %d", sync);
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
    esp_rom_delay_us(10920);
    // There is no silence after the wakeup pulse.  I tested this with Telis and no silence
    // was detected.  I suspect that for some battery powered shades the shade would go back
    // to sleep from the time of the initial pulse while the silence was occurring.
    REG_WRITE(GPIO_OUT_W1TC_REG, pin);
    esp_rom_delay_us(7357);
  }
  // Depending on the bitness of the protocol we will be sending a different hwsync.
  // 56-bit 2 pulses for the first frame and 7 for the repeats
  // 80-bit 24 pulses for the first frame and 14 pulses for the repeats
  for (int i = 0; i < sync; i++) {
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
    esp_rom_delay_us(4 * SYMBOL);
    REG_WRITE(GPIO_OUT_W1TC_REG, pin);
    esp_rom_delay_us(4 * SYMBOL);
  }
  // Software sync
  REG_WRITE(GPIO_OUT_W1TS_REG, pin);
  esp_rom_delay_us(4850);
  // Start 0
  REG_WRITE(GPIO_OUT_W1TC_REG, pin);
  esp_rom_delay_us(SYMBOL);
  // Payload starting with the most significant bit.  The frame is always supplied in 80 bits
  // but if the protocol is calling for 56 bits it will only send 56 bits of the frame.
  uint8_t last_bit = 0;
  for (byte i = 0; i < bitLength; i++) {
    if (((frame[i / 8] >> (7 - (i % 8))) & 1) == 1) {
      REG_WRITE(GPIO_OUT_W1TC_REG, pin);
      esp_rom_delay_us(SYMBOL);
      REG_WRITE(GPIO_OUT_W1TS_REG, pin);
      esp_rom_delay_us(SYMBOL);
      last_bit = 1;
    } else {
      REG_WRITE(GPIO_OUT_W1TS_REG, pin);
      esp_rom_delay_us(SYMBOL);
      REG_WRITE(GPIO_OUT_W1TC_REG, pin);
      esp_rom_delay_us(SYMBOL);
      last_bit = 0;
    }
  }
  // End with a 0 no matter what.  This accommodates the 56-bit protocol by telling the
  // motor that there are no more follow on bits.
  if(last_bit == 0) {
    REG_WRITE(GPIO_OUT_W1TS_REG, pin);
  }

  // Inter-frame silence for 56-bit protocols are around 34ms.  However, an 80 bit protocol should
  // reduce this by the transmission of SYMBOL * 24 or 15,360us.
  // Measured closer to 27500us in practice.
  REG_WRITE(GPIO_OUT_W1TC_REG, pin);
  if(bitLength != 80) {
    esp_rom_delay_us(13717);
    esp_rom_delay_us(13717);
  }
}

void RECEIVE_ATTR Transceiver::handleReceive() {
    static unsigned long last_time = 0;
    const long time = micros();
    const unsigned int duration = time - last_time;
    if (duration < bitMin) {
        // The incoming bit is < 448us so it is probably a glitch so blow it off.
        // We need to ignore this bit.
        // REMOVE THIS AFTER WE DETERMINE THAT THE out-of-bounds stuff isn't a problem.  If there are bits
        // from the previous frame then we will capture this data here.
        if(somfy_rx.pulseCount < MAX_TIMINGS && somfy_rx.cpt_synchro_hw > 0) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        return;
    }
    last_time = time;
    switch (somfy_rx.status) {
    case waiting_synchro:
        if(somfy_rx.pulseCount < MAX_TIMINGS) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        if (duration > tempo_synchro_hw_min && duration < tempo_synchro_hw_max) {
            // We have found a hardware sync bit.  There should be at least 4 of these.
            ++somfy_rx.cpt_synchro_hw;
        }
        else if (duration > tempo_synchro_sw_min && duration < tempo_synchro_sw_max && somfy_rx.cpt_synchro_hw >= 4) {
            // If we have a full hardware sync then we should look for the software sync.  If we have a software sync
            // bit and enough hardware sync bits then we should start receiving data.  It turns out that a 56 bit packet
            // with give 4 or 14 bits of hardware sync.  An 80 bit packet gives 12, 13 or 24 bits of hw sync.  Early on
            // I had some shorter and longer hw syncs but I can no longer repeat this.
            memset(somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
            somfy_rx.previous_bit = 0x00;
            somfy_rx.waiting_half_symbol = false;
            somfy_rx.cpt_bits = 0;
            // Keep an eye on this as it is possible that we might get fewer or more synchro bits.
            if (somfy_rx.cpt_synchro_hw <= 7) somfy_rx.bit_length = 56;
            else if (somfy_rx.cpt_synchro_hw == 14) somfy_rx.bit_length = 56;
            else if (somfy_rx.cpt_synchro_hw == 13) somfy_rx.bit_length = 80; // The RS485 device sends this sync.
            else if (somfy_rx.cpt_synchro_hw == 12) somfy_rx.bit_length = 80;
            else if (somfy_rx.cpt_synchro_hw > 17) somfy_rx.bit_length = 80;
            else somfy_rx.bit_length = 56;
            //somfy_rx.bit_length = 80;
            somfy_rx.status = receiving_data;
        }
        else {
            // Reset and start looking for hardware sync again.
            somfy_rx.cpt_synchro_hw = 0;
            // Try to capture the wakeup pulse.
            if(duration > tempo_wakeup_min && duration < tempo_wakeup_max)
            {
                memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
                somfy_rx.previous_bit = 0x00;
                somfy_rx.waiting_half_symbol = false;
                somfy_rx.cpt_bits = 0;
                somfy_rx.bit_length = 56;
            }
            else if((somfy_rx.pulseCount > 20 && somfy_rx.cpt_synchro_hw == 0) || duration > 250000) {
              somfy_rx.pulseCount = 0;
            }
        }
        break;
    case receiving_data:
        if(somfy_rx.pulseCount < MAX_TIMINGS) somfy_rx.pulses[somfy_rx.pulseCount++] = duration;
        // We should be receiving data at this point.
        if (duration > tempo_symbol_min && duration < tempo_symbol_max && !somfy_rx.waiting_half_symbol) {
            somfy_rx.previous_bit = 1 - somfy_rx.previous_bit;
            // Bits come in high order bit first.
            somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
            ++somfy_rx.cpt_bits;
        }
        else if (duration > tempo_half_symbol_min && duration < tempo_half_symbol_max) {
            if (somfy_rx.waiting_half_symbol) {
                somfy_rx.waiting_half_symbol = false;
                somfy_rx.payload[somfy_rx.cpt_bits / 8] += somfy_rx.previous_bit << (7 - somfy_rx.cpt_bits % 8);
                ++somfy_rx.cpt_bits;
            }
            else {
                somfy_rx.waiting_half_symbol = true;
            }
        }
        else {
            //++somfy_rx.cpt_bits;
            // Start over we are not within our parameters for bit timing.
            memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
            somfy_rx.pulseCount = 1;
            somfy_rx.cpt_synchro_hw = 0;
            somfy_rx.previous_bit = 0x00;
            somfy_rx.waiting_half_symbol = false;
            somfy_rx.cpt_bits = 0;
            somfy_rx.bit_length = 56;
            somfy_rx.status = waiting_synchro;
            somfy_rx.pulses[0] = duration;
        }
        break;
    default:
        break;
    }
    if (somfy_rx.status == receiving_data && somfy_rx.cpt_bits >= somfy_rx.bit_length) {
        // Since we are operating within the interrupt all data really needs to be static
        // for the handoff to the frame decoder.  For this reason we are buffering up to
        // 3 total frames.  Althought it may not matter considering the length of a packet
        // will likely not push over the loop timing.  For now lets assume that there
        // may be some pressure on the loop for features.
        if(rx_queue.length >= MAX_RX_BUFFER) {
          // We have overflowed the buffer simply empty the last item
          // in this instance we will simply throw it away.
          uint8_t ndx = rx_queue.index[MAX_RX_BUFFER - 1];
          if(ndx < MAX_RX_BUFFER) rx_queue.items[ndx].pulseCount = 0;
          //memset(&this->items[ndx], 0x00, sizeof(somfy_rx_t));
          rx_queue.index[MAX_RX_BUFFER - 1] = 255;
          rx_queue.length--;
        }
        uint8_t first = 0;
        // Place this record in the first empty slot.  There will
        // be one since we cleared a space above should there
        // be an overflow.
        for(uint8_t i = 0; i < MAX_RX_BUFFER; i++) {
          if(rx_queue.items[i].pulseCount == 0) {
            first = i;
            memcpy(&rx_queue.items[i], &somfy_rx, sizeof(somfy_rx_t));
            break;
          }
        }
        // Move the index so that it is the at position 0.  The oldest item will fall off.
        for(uint8_t i = MAX_RX_BUFFER - 1; i > 0; i--) {
          rx_queue.index[i] = rx_queue.index[i - 1];
        }
        rx_queue.length++;
        rx_queue.index[0] = first;
        memset(&somfy_rx.payload, 0x00, sizeof(somfy_rx.payload));
        somfy_rx.cpt_synchro_hw = 0;
        somfy_rx.previous_bit = 0x00;
        somfy_rx.waiting_half_symbol = false;
        somfy_rx.cpt_bits = 0;
        somfy_rx.pulseCount = 0;
        somfy_rx.status = waiting_synchro;
    }
}

void Transceiver::beginFrequencyScan() {
  if(this->config.enabled) {
    this->disableReceive();
    this->config.apply();
    rxmode = 3;
    pinMode(this->config.RXPin, INPUT);
    interruptPin = digitalPinToInterrupt(this->config.RXPin);
    ELECHOUSE_cc1101.setRxBW(this->config.rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.SetRx();
    markFreq = currFreq = 433.0f;
    markRSSI = -100;
    ELECHOUSE_cc1101.setMHZ(currFreq);
    ESP_LOGI(TAG, "Begin frequency scan on Pin #%d", this->config.RXPin);
    attachInterrupt(interruptPin, handleReceive, CHANGE);
    this->emitFrequencyScan();
  }
}

void Transceiver::processFrequencyScan(bool received) {
  if(this->config.enabled && rxmode == 3) {
    if(received) {
      currRSSI = ELECHOUSE_cc1101.getRssi();
      if((long)(markFreq * 100) == (long)(currFreq * 100)) {
        markRSSI = currRSSI;
      }
      else if(currRSSI >-75) {
        if(currRSSI > markRSSI) {
          markRSSI = currRSSI;
          markFreq = currFreq;
        }
      }
    }
    else {
      currRSSI = -100;
    }
    
    if(millis() - lastScan > 100 && somfy_rx.status == waiting_synchro) {
      lastScan = millis();
      this->emitFrequencyScan();
      currFreq += 0.01f;
      if(currFreq > 434.0f) currFreq = 433.0f;
      ELECHOUSE_cc1101.setMHZ(currFreq);
    }
  }
}

void Transceiver::endFrequencyScan() {
  if(rxmode == 3) {
    rxmode = 0;
    if(interruptPin > 0) detachInterrupt(interruptPin); 
    interruptPin = 0;
    this->config.apply();
    this->emitFrequencyScan();
  }
}

void Transceiver::emitFrequencyScan(uint8_t num) {
  JsonSockEvent *json = sockEmit.beginEmit("frequencyScan");
  json->beginObject();
  json->addElem("scanning", rxmode == 3);
  json->addElem("testFreq", currFreq);
  json->addElem("testRSSI", (int32_t)currRSSI);
  json->addElem("frequency", markFreq);
  json->addElem("RSSI", (int32_t)markRSSI);
  json->endObject();
  sockEmit.endEmit(num);
  /*
  char buf[420];
  snprintf(buf, sizeof(buf), "{\"scanning\":%s,\"testFreq\":%f,\"testRSSI\":%d,\"frequency\":%f,\"RSSI\":%d}", rxmode == 3 ? "true" : "false", currFreq, currRSSI, markFreq, markRSSI); 
  if(num >= 255) sockEmit.sendToClients("frequencyScan", buf);
  else sockEmit.sendToClient(num, "frequencyScan", buf);
  */
}

bool Transceiver::receive(somfy_rx_t *rx) {
    // Check to see if there is anything in the buffer
    if(rx_queue.length > 0) {
      ESP_LOGD(TAG, "Processing receive %d", rx_queue.length);
      rx_queue.pop(rx);
      this->frame.decodeFrame(rx);
      this->emitFrame(&this->frame, rx);
      return this->frame.valid;
    }
    return false;
}

void Transceiver::emitFrame(somfy_frame_t *frame, somfy_rx_t *rx) {
  if(sockEmit.activeClients(ROOM_EMIT_FRAME) > 0) {
    JsonSockEvent *json = sockEmit.beginEmit("remoteFrame");
    json->beginObject();
    json->addElem("encKey", frame->encKey);
    json->addElem("address", (uint32_t)frame->remoteAddress);
    json->addElem("rcode", (uint32_t)frame->rollingCode);
    json->addElem("command", translateSomfyCommand(frame->cmd).c_str());
    json->addElem("rssi", (int32_t)frame->rssi);
    json->addElem("bits", rx->bit_length);
    json->addElem("proto", static_cast<uint8_t>(frame->proto));
    json->addElem("valid", frame->valid);
    json->addElem("sync", frame->hwsync);
    if(frame->cmd == somfy_commands::StepUp || frame->cmd == somfy_commands::StepDown)
      json->addElem("stepSize", frame->stepSize);
    json->beginArray("pulses");
    if(rx) {
      for(uint16_t i = 0; i < rx->pulseCount; i++) {
        json->addElem((uint32_t)rx->pulses[i]);
      }
    }
    json->endArray();
    json->endObject();
    sockEmit.endEmitRoom(ROOM_EMIT_FRAME);
    /*
    ClientSocketEvent evt("remoteFrame");
    char buf[30];
    snprintf(buf, sizeof(buf), "{\"encKey\":%d,", frame->encKey);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"address\":%d,", frame->remoteAddress);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"rcode\":%d,", frame->rollingCode);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"command\":\"%s\",", translateSomfyCommand(frame->cmd).c_str());
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"rssi\":%d,", frame->rssi);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"bits\":%d,", rx->bit_length);
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"proto\":%d,", static_cast<uint8_t>(frame->proto));
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"valid\":%s,", frame->valid ? "true" : "false");
    evt.appendMessage(buf);
    snprintf(buf, sizeof(buf), "\"sync\":%d,\"pulses\":[", frame->hwsync);
    evt.appendMessage(buf);
    
    if(rx) {
      for(uint16_t i = 0; i < rx->pulseCount; i++) {
        snprintf(buf, sizeof(buf), "%s%d", i != 0 ? "," : "", rx->pulses[i]);
        evt.appendMessage(buf);
      }
    }
    evt.appendMessage("]}");
    sockEmit.sendToRoom(ROOM_EMIT_FRAME, &evt);
    */
  }
}

void Transceiver::clearReceived(void) {
    //packet_received = false;
    //memset(receive_buffer, 0x00, sizeof(receive_buffer));
    if(this->config.enabled)
      //attachInterrupt(interruptPin, handleReceive, FALLING);
      attachInterrupt(interruptPin, handleReceive, CHANGE);
}

void Transceiver::enableReceive(void) {
    uint32_t timing = millis();
    if(rxmode > 0) return;
    if(this->config.enabled) {
      rxmode = 1;
      pinMode(this->config.RXPin, INPUT);
      interruptPin = digitalPinToInterrupt(this->config.RXPin);
      ELECHOUSE_cc1101.SetRx();
      //attachInterrupt(interruptPin, handleReceive, FALLING);
      attachInterrupt(interruptPin, handleReceive, CHANGE);
      ESP_LOGI(TAG, "Enabled receive on Pin #%d Timing: %ld", this->config.RXPin, millis() - timing);
    }
}

void Transceiver::disableReceive(void) { 
  rxmode = 0;
  if(interruptPin > 0) detachInterrupt(interruptPin); 
  interruptPin = 0;
  
}

void Transceiver::toJSON(JsonResponse& json) {
    json.beginObject("config");
    this->config.toJSON(json);
    json.endObject();
}

bool Transceiver::fromJSON(JsonObject& obj) {
    if (obj.containsKey("config")) {
      JsonObject objConfig = obj["config"];
      this->config.fromJSON(objConfig);
    }
    return true;
}

bool Transceiver::usesPin(uint8_t pin) {
  if(this->config.enabled) {
    if(this->config.SCKPin == pin ||
      this->config.TXPin == pin ||
      this->config.RXPin == pin ||
      this->config.MOSIPin == pin ||
      this->config.MISOPin == pin ||
      this->config.CSNPin == pin)
      return true;
  }
  return false;  
}

bool Transceiver::save() {
    this->config.save();
    this->config.apply();
    return true;
}

bool Transceiver::end() {
    this->disableReceive();
    return true;
}

void transceiver_config_t::fromJSON(JsonObject& obj) {
    ESP_LOGD(TAG, "Deserialize Radio JSON Config");
    if(obj.containsKey("type")) this->type = obj["type"];
    if(obj.containsKey("CSNPin")) this->CSNPin = obj["CSNPin"];
    if(obj.containsKey("MISOPin")) this->MISOPin = obj["MISOPin"];
    if(obj.containsKey("MOSIPin")) this->MOSIPin = obj["MOSIPin"];
    if(obj.containsKey("RXPin")) this->RXPin = obj["RXPin"];
    if(obj.containsKey("SCKPin")) this->SCKPin = obj["SCKPin"];
    if(obj.containsKey("TXPin")) this->TXPin = obj["TXPin"];
    if(obj.containsKey("rxBandwidth")) this->rxBandwidth = obj["rxBandwidth"]; // float
    if(obj.containsKey("frequency")) this->frequency = obj["frequency"];  // float
    if(obj.containsKey("deviation")) this->deviation = obj["deviation"];  // float
    if(obj.containsKey("enabled")) this->enabled = obj["enabled"];
    if(obj.containsKey("txPower")) this->txPower = obj["txPower"];
    if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    /*
    if (obj.containsKey("internalCCMode")) this->internalCCMode = obj["internalCCMode"];
    if (obj.containsKey("modulationMode")) this->modulationMode = obj["modulationMode"];
    if (obj.containsKey("channel")) this->channel = obj["channel"];
    if (obj.containsKey("channelSpacing")) this->channelSpacing = obj["channelSpacing"]; // float
    if (obj.containsKey("dataRate")) this->dataRate = obj["dataRate"]; // float
    if (obj.containsKey("syncMode")) this->syncMode = obj["syncMode"];
    if (obj.containsKey("syncWordHigh")) this->syncWordHigh = obj["syncWordHigh"];
    if (obj.containsKey("syncWordLow")) this->syncWordLow = obj["syncWordLow"];
    if (obj.containsKey("addrCheckMode")) this->addrCheckMode = obj["addrCheckMode"];
    if (obj.containsKey("checkAddr")) this->checkAddr = obj["checkAddr"];
    if (obj.containsKey("dataWhitening")) this->dataWhitening = obj["dataWhitening"];
    if (obj.containsKey("pktFormat")) this->pktFormat = obj["pktFormat"];
    if (obj.containsKey("pktLengthMode")) this->pktLengthMode = obj["pktLengthMode"];
    if (obj.containsKey("pktLength")) this->pktLength = obj["pktLength"];
    if (obj.containsKey("useCRC")) this->useCRC = obj["useCRC"];
    if (obj.containsKey("autoFlushCRC")) this->autoFlushCRC = obj["autoFlushCRC"];
    if (obj.containsKey("disableDCFilter")) this->disableDCFilter = obj["disableCRCFilter"];
    if (obj.containsKey("enableManchester")) this->enableManchester = obj["enableManchester"];
    if (obj.containsKey("enableFEC")) this->enableFEC = obj["enableFEC"];
    if (obj.containsKey("minPreambleBytes")) this->minPreambleBytes = obj["minPreambleBytes"];
    if (obj.containsKey("pqtThreshold")) this->pqtThreshold = obj["pqtThreshold"];
    if (obj.containsKey("appendStatus")) this->appendStatus = obj["appendStatus"];
    if (obj.containsKey("printBuffer")) this->printBuffer = obj["printBuffer"];
    */
    ESP_LOGI(TAG, "SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}

void transceiver_config_t::toJSON(JsonResponse &json) {
    json.addElem("type", this->type);
    json.addElem("TXPin", this->TXPin);
    json.addElem("RXPin", this->RXPin);
    json.addElem("SCKPin", this->SCKPin);
    json.addElem("MOSIPin", this->MOSIPin);
    json.addElem("MISOPin", this->MISOPin);
    json.addElem("CSNPin", this->CSNPin);
    json.addElem("rxBandwidth", this->rxBandwidth); // float
    json.addElem("frequency", this->frequency);  // float
    json.addElem("deviation", this->deviation);  // float
    json.addElem("txPower", this->txPower);
    json.addElem("proto", static_cast<uint8_t>(this->proto));
    json.addElem("enabled", this->enabled);
    json.addElem("radioInit", this->radioInit);
}

void transceiver_config_t::save() {
    pref.begin("CC1101");
    pref.clear();
    pref.putUChar("type", this->type);
    pref.putUChar("TXPin", this->TXPin);
    pref.putUChar("RXPin", this->RXPin);
    pref.putUChar("SCKPin", this->SCKPin);
    pref.putUChar("MOSIPin", this->MOSIPin);
    pref.putUChar("MISOPin", this->MISOPin);
    pref.putUChar("CSNPin", this->CSNPin);
    pref.putFloat("frequency", this->frequency);  // float
    pref.putFloat("deviation", this->deviation);  // float
    pref.putFloat("rxBandwidth", this->rxBandwidth); // float
    pref.putBool("enabled", this->enabled);
    pref.putBool("radioInit", true);
    pref.putChar("txPower", this->txPower);
    pref.putChar("proto", static_cast<uint8_t>(this->proto));
    
    /*
    pref.putBool("internalCCMode", this->internalCCMode);
    pref.putUChar("modulationMode", this->modulationMode);
    pref.putUChar("channel", this->channel);
    pref.putFloat("channelSpacing", this->channelSpacing); // float
    pref.putFloat("rxBandwidth", this->rxBandwidth); // float
    pref.putFloat("dataRate", this->dataRate); // float
    pref.putChar("txPower", this->txPower);
    pref.putUChar("syncMode", this->syncMode);
    pref.putUShort("syncWordHigh", this->syncWordHigh);
    pref.putUShort("syncWordLow", this->syncWordLow);
    pref.putUChar("addrCheckMode", this->addrCheckMode);
    pref.putUChar("checkAddr", this->checkAddr);
    pref.putBool("dataWhitening", this->dataWhitening);
    pref.putUChar("pktFormat", this->pktFormat);
    pref.putUChar("pktLengthMode", this->pktLengthMode);
    pref.putUChar("pktLength", this->pktLength);
    pref.putBool("useCRC", this->useCRC);
    pref.putBool("autoFlushCRC", this->autoFlushCRC);
    pref.putBool("disableDCFilter", this->disableDCFilter);
    pref.putBool("enableManchester", this->enableManchester);
    pref.putBool("enableFEC", this->enableFEC);
    pref.putUChar("minPreambleBytes", this->minPreambleBytes);
    pref.putUChar("pqtThreshold", this->pqtThreshold);
    pref.putBool("appendStatus", this->appendStatus);
    */
    pref.end();
   
    ESP_LOGI(TAG, "Save Radio Settings ");
    ESP_LOGI(TAG, "SCK:%u MISO:%u MOSI:%u CSN:%u RX:%u TX:%u", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin, this->RXPin, this->TXPin);
}

void transceiver_config_t::removeNVSKey(const char *key) {
  if(pref.isKey(key)) {
    ESP_LOGI(TAG, "Removing NVS Key: CC1101.%s", key);
    pref.remove(key);
  }
}

void transceiver_config_t::load() {
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    switch(ci.model) {
      case esp_chip_model_t::CHIP_ESP32S3:
        ESP_LOGI(TAG, "Setting S3 Transceiver Defaults...");
        this->TXPin = 15;
        this->RXPin = 14;
        this->MOSIPin = 11;
        this->MISOPin = 13;
        this->SCKPin = 12;
        this->CSNPin = 10;
        break;
      case esp_chip_model_t::CHIP_ESP32S2:
        this->TXPin = 15;
        this->RXPin = 14;
        this->MOSIPin = 35;
        this->MISOPin = 37;
        this->SCKPin = 36;
        this->CSNPin = 34;
        break;
      case esp_chip_model_t::CHIP_ESP32C3:
        this->TXPin = 13;
        this->RXPin = 12;
        this->MOSIPin = 16;
        this->MISOPin = 17;
        this->SCKPin = 15;
        this->CSNPin = 14;
        break;
      default:
        this->TXPin = 13;
        this->RXPin = 12;
        this->MOSIPin = 23;
        this->MISOPin = 19;
        this->SCKPin = 18;
        this->CSNPin = 5;
        break;
    }
    pref.begin("CC1101");
    this->type = pref.getUChar("type", 56);
    this->TXPin = pref.getUChar("TXPin", this->TXPin);
    this->RXPin = pref.getUChar("RXPin", this->RXPin);
    this->SCKPin = pref.getUChar("SCKPin", this->SCKPin);
    this->MOSIPin = pref.getUChar("MOSIPin", this->MOSIPin);
    this->MISOPin = pref.getUChar("MISOPin", this->MISOPin);
    this->CSNPin = pref.getUChar("CSNPin", this->CSNPin);
    this->frequency = pref.getFloat("frequency", this->frequency);  // float
    this->deviation = pref.getFloat("deviation", this->deviation);  // float
    this->enabled = pref.getBool("enabled", this->enabled);
    this->txPower = pref.getChar("txPower", this->txPower);
    this->rxBandwidth = pref.getFloat("rxBandwidth", this->rxBandwidth);
    this->proto = static_cast<radio_proto>(pref.getChar("proto", static_cast<uint8_t>(this->proto)));
    this->removeNVSKey("internalCCMode");
    this->removeNVSKey("modulationMode");
    this->removeNVSKey("channel");
    this->removeNVSKey("channelSpacing");
    this->removeNVSKey("dataRate");
    this->removeNVSKey("syncMode");
    this->removeNVSKey("syncWordHigh");
    this->removeNVSKey("syncWordLow");
    this->removeNVSKey("addrCheckMode");
    this->removeNVSKey("checkAddr");
    this->removeNVSKey("dataWhitening");
    this->removeNVSKey("pktFormat");
    this->removeNVSKey("pktLengthMode");
    this->removeNVSKey("pktLength");
    this->removeNVSKey("useCRC");
    this->removeNVSKey("autoFlushCRC");
    this->removeNVSKey("disableDCFilter");
    this->removeNVSKey("enableManchester");
    this->removeNVSKey("enableFEC");
    this->removeNVSKey("minPreambleBytes");
    this->removeNVSKey("pqtThreshold");
    this->removeNVSKey("appendStatus");
    pref.end();
    //this->printBuffer = somfy.transceiver.printBuffer;
}

void transceiver_config_t::apply() {
    somfy.transceiver.disableReceive();
    bit_length = this->type;    
    if(this->enabled) {
      bool radioInit = true;
      // Always configure SPI pin assignments first, regardless of radioInit guard,
      // so that any subsequent library calls have valid pin numbers.
      ELECHOUSE_cc1101.setSpiPin(this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin);
      pref.begin("CC1101");
      radioInit = pref.getBool("radioInit", true);
      // If the radio locks up then we can simply reboot and re-enable the radio.
      pref.putBool("radioInit", false);
      this->radioInit = false;
      pref.end();
      if(!radioInit) return;
      ESP_LOGI(TAG, "Applying radio settings ");
      ESP_LOGI(TAG, "Setting Data Pins RX:%u TX:%u", this->RXPin, this->TXPin);
      // Configure all pins through the GPIO matrix before SPI/CC1101 init.
      // Without this, ESP32-S3 reports "IO x is not set as GPIO" and the SPI
      // transfer hangs, triggering the watchdog.
      pinMode(this->SCKPin,  OUTPUT);
      pinMode(this->MOSIPin, OUTPUT);
      pinMode(this->MISOPin, INPUT);
      pinMode(this->CSNPin,  OUTPUT);
      digitalWrite(this->CSNPin, HIGH);
      if(this->TXPin != this->RXPin)
        pinMode(this->TXPin, OUTPUT);
      pinMode(this->RXPin, INPUT);
      if(this->TXPin == this->RXPin)
        ELECHOUSE_cc1101.setGDO0(this->TXPin); // This pin may be shared.
      else
        ELECHOUSE_cc1101.setGDO(this->TXPin, this->RXPin); // GDO0, GDO2
      ESP_LOGI(TAG, "Setting SPI Pins SCK:%u MISO:%u MOSI:%u CSN:%u", this->SCKPin, this->MISOPin, this->MOSIPin, this->CSNPin);
      ESP_LOGI(TAG, "Radio Pins Configured!");
      ELECHOUSE_cc1101.Init();
      ELECHOUSE_cc1101.setCCMode(0);                            // set config for internal transmission mode.
      ELECHOUSE_cc1101.setMHZ(this->frequency);                 // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.
      ELECHOUSE_cc1101.setRxBW(this->rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
      ELECHOUSE_cc1101.setDeviation(this->deviation);           // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
      ELECHOUSE_cc1101.setPA(this->txPower);                    // Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12) Default is max!
      ELECHOUSE_cc1101.setModulation(2);                        // Set modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
      ELECHOUSE_cc1101.setManchester(1);                        // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
      ELECHOUSE_cc1101.setPktFormat(3);                         // Format of RX and TX data. 
                                                                // 0 = Normal mode, use FIFOs for RX and TX. 
                                                                // 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 
                                                                // 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 
                                                                // 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
      ELECHOUSE_cc1101.setDcFilterOff(0);                       // Disable digital DC blocking filter before demodulator. Only for data rates â‰¤ 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 
                                                                // 1 = Disable (current optimized). 
                                                                // 0 = Enable (better sensitivity).
      ELECHOUSE_cc1101.setCrc(0);                               // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
      ELECHOUSE_cc1101.setCRC_AF(0);                            // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
      ELECHOUSE_cc1101.setSyncMode(4);                          // Combined sync-word qualifier mode. 
                                                                // 0 = No preamble/sync. 
                                                                // 1 = 16 sync word bits detected. 
                                                                // 2 = 16/16 sync word bits detected. 
                                                                // 3 = 30/32 sync word bits detected. 
                                                                // 4 = No preamble/sync, carrier-sense above threshold. 
                                                                // 5 = 15/16 + carrier-sense above threshold. 
                                                                // 6 = 16/16 + carrier-sense above threshold. 
                                                                // 7 = 30/32 + carrier-sense above threshold.
      ELECHOUSE_cc1101.setAdrChk(0);                            // Controls address check configuration of received packages. 
                                                                // 0 = No address check. 
                                                                // 1 = Address check, no broadcast. 
                                                                // 2 = Address check and 0 (0x00) broadcast. 
                                                                // 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    
      
      if (!ELECHOUSE_cc1101.getCC1101()) {
          ESP_LOGI(TAG, "Error setting up the radio");
          this->radioInit = false;
      }
      else {
          ESP_LOGI(TAG, "Successfully set up the radio");
          somfy.transceiver.enableReceive();
          this->radioInit = true;
      }
      pref.begin("CC1101");
      pref.putBool("radioInit", true);
      pref.end();
      
    }
    else {
      if(this->radioInit) ELECHOUSE_cc1101.setSidle();
      somfy.transceiver.disableReceive();
      this->radioInit = false;
    }
    /*
    ELECHOUSE_cc1101.setChannel(this->channel);               // Set the Channelnumber from 0 to 255. Default is cahnnel 0.
    ELECHOUSE_cc1101.setChsp(this->channelSpacing);           // The channel spacing is multiplied by the channel number CHAN and added to the base frequency in kHz. Value from 25.39 to 405.45. Default is 199.95 kHz.
    ELECHOUSE_cc1101.setDRate(this->dataRate);                // Set the Data Rate in kBaud. Value from 0.02 to 1621.83. Default is 99.97 kBaud!
    ELECHOUSE_cc1101.setSyncMode(this->syncMode);             // Combined sync-word qualifier mode. 0 = No preamble/sync. 1 = 16 sync word bits detected. 2 = 16/16 sync word bits detected. 3 = 30/32 sync word bits detected. 4 = No preamble/sync, carrier-sense above threshold. 5 = 15/16 + carrier-sense above threshold. 6 = 16/16 + carrier-sense above threshold. 7 = 30/32 + carrier-sense above threshold.
    ELECHOUSE_cc1101.setSyncWord(this->syncWordHigh, this->syncWordLow); // Set sync word. Must be the same for the transmitter and receiver. (Syncword high, Syncword low)
    ELECHOUSE_cc1101.setAdrChk(this->addrCheckMode);          // Controls address check configuration of received packages. 0 = No address check. 1 = Address check, no broadcast. 2 = Address check and 0 (0x00) broadcast. 3 = Address check and 0 (0x00) and 255 (0xFF) broadcast.
    ELECHOUSE_cc1101.setAddr(this->checkAddr);                // Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).
    ELECHOUSE_cc1101.setWhiteData(this->dataWhitening);       // Turn data whitening on / off. 0 = Whitening off. 1 = Whitening on.
    ELECHOUSE_cc1101.setPktFormat(this->pktFormat);           // Format of RX and TX data. 0 = Normal mode, use FIFOs for RX and TX. 1 = Synchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins. 2 = Random TX mode; sends random data using PN9 generator. Used for test. Works as normal mode, setting 0 (00), in RX. 3 = Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins.
    ELECHOUSE_cc1101.setLengthConfig(this->pktLengthMode);    // 0 = Fixed packet length mode. 1 = Variable packet length mode. 2 = Infinite packet length mode. 3 = Reserved
    ELECHOUSE_cc1101.setPacketLength(this->pktLength);        // Indicates the packet length when fixed packet length mode is enabled. If variable packet length mode is used, this value indicates the maximum packet length allowed.
    ELECHOUSE_cc1101.setCrc(this->useCRC);                    // 1 = CRC calculation in TX and CRC check in RX enabled. 0 = CRC disabled for TX and RX.
    ELECHOUSE_cc1101.setCRC_AF(this->autoFlushCRC);           // Enable automatic flush of RX FIFO when CRC is not OK. This requires that only one packet is in the RXIFIFO and that packet length is limited to the RX FIFO size.
    ELECHOUSE_cc1101.setDcFilterOff(this->disableDCFilter);   // Disable digital DC blocking filter before demodulator. Only for data rates â‰¤ 250 kBaud The recommended IF frequency changes when the DC blocking is disabled. 1 = Disable (current optimized). 0 = Enable (better sensitivity).
    ELECHOUSE_cc1101.setManchester(this->enableManchester);   // Enables Manchester encoding/decoding. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setFEC(this->enableFEC);                 // Enable Forward Error Correction (FEC) with interleaving for packet payload (Only supported for fixed packet length mode. 0 = Disable. 1 = Enable.
    ELECHOUSE_cc1101.setPRE(this->minPreambleBytes);          // Sets the minimum number of preamble bytes to be transmitted. Values: 0 : 2, 1 : 3, 2 : 4, 3 : 6, 4 : 8, 5 : 12, 6 : 16, 7 : 24
    ELECHOUSE_cc1101.setPQT(this->pqtThreshold);              // Preamble quality estimator threshold. The preamble quality estimator increases an internal counter by one each time a bit is received that is different from the previous bit, and decreases the counter by 8 each time a bit is received that is the same as the last bit. A threshold of 4âˆ™PQT for this counter is used to gate sync word detection. When PQT=0 a sync word is always accepted.
    ELECHOUSE_cc1101.setAppendStatus(this->appendStatus);     // When enabled, two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.
    */
    //somfy.transceiver.printBuffer = this->printBuffer;
}

bool Transceiver::begin() {
    this->config.load();
    // Clear any radioInit=false guard left by a previous crash so that apply()
    // doesn't skip init on the first boot after a watchdog reset.
    Preferences pref;
    pref.begin("CC1101");
    pref.putBool("radioInit", true);
    pref.end();
    this->config.apply();
    rx_queue.init();
    return true;
}

void Transceiver::loop() {
  somfy_rx_t rx;
  if(rxmode == 3) {
    if(this->receive(&rx))
      this->processFrequencyScan(true);
    else
      this->processFrequencyScan(false);
  }
  else if (this->receive(&rx)) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
      if(somfy.repeaters[i] == frame.remoteAddress) {
        tx_queue.push(&rx);
        ESP_LOGI(TAG, "Queued repeater frame...");
        break;
      }
    }
    somfy.processFrame(this->frame, false);
  }
  else {
    somfy.processWaitingFrame();
    // Check to see if there is anything in the buffer
    if(tx_queue.length > 0 && millis() > tx_queue.delay_time && somfy_rx.cpt_synchro_hw == 0) {
      this->beginTransmit();
      somfy_tx_t tx;
      
      tx_queue.pop(&tx);
      ESP_LOGI(TAG, "Sending frame %d - %d-BIT [", tx.hwsync, tx.bit_length);
      for(uint8_t j = 0; j < 10; j++) {
        ESP_LOGI(TAG, "%d", tx.payload[j]);
        if(j < 9) ESP_LOGI(TAG, ", ");
      }
      ESP_LOGI(TAG, "]");

      this->sendFrame(tx.payload, tx.hwsync, tx.bit_length);
      tx_queue.delay_time = millis() + TX_QUEUE_DELAY;
      
      this->endTransmit();
    }
  }
}

somfy_frame_t& Transceiver::lastFrame() { return this->frame; }

void Transceiver::beginTransmit() {
    if(this->config.enabled) {
      this->disableReceive();
      pinMode(this->config.TXPin, OUTPUT);
      digitalWrite(this->config.TXPin, 0);
      ELECHOUSE_cc1101.SetTx();
    }
}

void Transceiver::endTransmit() {
    if(this->config.enabled) {
      ELECHOUSE_cc1101.setSidle();
      //delay(100);
      this->enableReceive();
    }
}
