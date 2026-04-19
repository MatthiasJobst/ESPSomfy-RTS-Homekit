// SomfyTransceiver.cpp — CC1101 transceiver implementation: interrupt handler (IRAM),
// frame receive/decode pipeline, frequency scan, all SomfyTransceiver::* and
// transceiver_config_t::* method bodies (send, apply, load/save NVS, JSON I/O).
// Module-local state: rxmode, bit_length, somfy_rx, rx_queue, tx_queue, freq scan vars.
#include "compat/preferences.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SPI.h>
#include <esp_chip_info.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/task.h"
#include "SomfyTransceiver.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "MQTT.h"
#include "GitOTA.h"

static const char *TAG = "SomfyTransceiver";
// Module-level state (SomfyTransceiver private)
uint8_t rxmode = 0;   // 0=off 1=receive 3=frequency-scan
uint8_t bit_length = 56;  // current protocol frame width; set by apply(), read by sendCommand()
static int interruptPin = 0;

// RF noise watchdog state (written in ISR, read in loop)
static volatile bool noiseDetected = false;
static volatile uint32_t noiseWindowStart = 0;
static volatile uint16_t noisePulseCount = 0;
static const uint16_t NOISE_PULSE_THRESHOLD = 100;
static const uint32_t NOISE_WINDOW_MS = 10000;
static const uint32_t NOISE_COOLDOWN_MS = 30000;

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
// static const uint32_t tempo_wakeup_pulse = 9415;
static const uint32_t tempo_wakeup_min = static_cast<uint32_t>(9415 * TOLERANCE_MIN);
static const uint32_t tempo_wakeup_max = static_cast<uint32_t>(9415 * TOLERANCE_MAX);
//static const uint32_t tempo_wakeup_silence = 89565;
//static const uint32_t tempo_wakeup_silence_min = static_cast<uint32_t>(89565 * TOLERANCE_MIN);
//static const uint32_t tempo_wakeup_silence_max = static_cast<uint32_t>(89565 * TOLERANCE_MAX);
static const uint32_t tempo_synchro_hw_min = SYMBOL * 4 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_hw_max = SYMBOL * 4 * TOLERANCE_MAX;
static const uint32_t tempo_synchro_sw_min = 4850 * TOLERANCE_MIN;
static const uint32_t tempo_synchro_sw_max = 4850 * TOLERANCE_MAX;
static const uint32_t tempo_half_symbol_min = SYMBOL * TOLERANCE_MIN;
static const uint32_t tempo_half_symbol_max = SYMBOL * TOLERANCE_MAX;
static const uint32_t tempo_symbol_min = SYMBOL * 2 * TOLERANCE_MIN;
static const uint32_t tempo_symbol_max = SYMBOL * 2 * TOLERANCE_MAX;
//static const uint32_t tempo_if_gap = 30415;  // Gap between frames
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

// ─── RMT TX encoder ──────────────────────────────────────────────────────────

// State machine driven by the RMT driver's encode() callback.
// Each invocation handles ONE frame end-to-end. The driver may call back
// multiple times if the channel buffer fills; state persists in somfy_encoder_t
// across calls within the same transaction. Between transactions the driver
// invokes the encoder's reset hook, returning state to ST_WAKEUP.
typedef enum : uint8_t {
    ST_WAKEUP = 0,   // wakeup pulse (first frame in burst only)
    ST_SYNC,         // hardware sync pulses
    ST_SWSYNC,       // software sync + start-0 bit
    ST_DATA,         // Manchester-encoded payload bits
    ST_TAIL,         // inter-frame silence (56-bit, when more frames follow)
    ST_DONE
} tx_state_t;

typedef struct {
    rmt_encoder_t        base;       // MUST be first — driver casts to rmt_encoder_t*
    rmt_encoder_handle_t copy_enc;   // single-symbol copy encoder (IDF built-in)
    tx_state_t           state;
    uint16_t             idx;        // position within the current state
} somfy_encoder_t;

// Per-transmission payload — passed via rmt_transmit's `data` pointer; the
// driver pins it until that transaction completes, so the encoder can read
// freely from it across multiple invocations.
typedef struct {
    uint8_t bytes[10];   // pre-encoded frame bytes
    uint8_t bit_length;  // 56 or 80
    uint8_t sync_count;  // sync pulses for this frame (2/12 initial, 7/6 repeat)
    bool    wakeup;      // emit wakeup pulse before sync (first frame only)
    bool    tail;        // emit ~27 ms inter-frame silence after data
} somfy_tx_frame_t;

// Maximum frames per burst (1 initial + up to N-1 repeats). Bound by the
// per-channel trans_queue_depth and by the static s_burst[] storage.
#define SOMFY_BURST_MAX 16

// Helper: write one symbol via the copy encoder.  Returns false and sets
// RMT_ENCODING_MEM_FULL in *state if the channel buffer is full.
#define SOMFY_WRITE_SYM(e, chan, sym, enc_state, ret)                            \
    do {                                                                         \
        rmt_encode_state_t _s = RMT_ENCODING_RESET;                             \
        (ret) += (e)->copy_enc->encode((e)->copy_enc, (chan),                    \
                    &(sym), sizeof(rmt_symbol_word_t), &_s);                     \
        if(_s & RMT_ENCODING_MEM_FULL) {                                         \
            *(enc_state) = static_cast<rmt_encode_state_t>(                      \
                               static_cast<int>(*(enc_state)) |                  \
                               static_cast<int>(RMT_ENCODING_MEM_FULL));         \
            return (ret);                                                         \
        }                                                                         \
    } while(0)

static size_t somfy_encode(rmt_encoder_t *enc, rmt_channel_handle_t chan,
                            const void *data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    somfy_encoder_t *e = __containerof(enc, somfy_encoder_t, base);
    const somfy_tx_frame_t *f = (const somfy_tx_frame_t *)data;
    *ret_state = RMT_ENCODING_RESET;
    size_t n = 0;
    rmt_symbol_word_t sym;

    while(e->state != ST_DONE) {
        switch(e->state) {

        case ST_WAKEUP:
            if(f->wakeup) {
                sym.level0 = 1; sym.duration0 = 10920;
                sym.level1 = 0; sym.duration1 = 7357;
                SOMFY_WRITE_SYM(e, chan, sym, ret_state, n);
            }
            e->idx   = 0;
            e->state = ST_SYNC;
            break;

        case ST_SYNC:
            if(e->idx < f->sync_count) {
                sym.level0 = 1; sym.duration0 = 4 * SYMBOL;
                sym.level1 = 0; sym.duration1 = 4 * SYMBOL;
                SOMFY_WRITE_SYM(e, chan, sym, ret_state, n);
                e->idx++;
            } else {
                e->state = ST_SWSYNC;
            }
            break;

        case ST_SWSYNC:
            // Software sync (H 4850 µs) + start-0 bit (L SYMBOL µs) in one symbol
            sym.level0 = 1; sym.duration0 = 4850;
            sym.level1 = 0; sym.duration1 = SYMBOL;
            SOMFY_WRITE_SYM(e, chan, sym, ret_state, n);
            e->idx   = 0;
            e->state = ST_DATA;
            break;

        case ST_DATA:
            if(e->idx < f->bit_length) {
                uint8_t bit = (f->bytes[e->idx / 8] >> (7 - (e->idx % 8))) & 1;
                if(bit) {
                    sym.level0 = 0; sym.duration0 = SYMBOL;
                    sym.level1 = 1; sym.duration1 = SYMBOL;
                } else {
                    sym.level0 = 1; sym.duration0 = SYMBOL;
                    sym.level1 = 0; sym.duration1 = SYMBOL;
                }
                SOMFY_WRITE_SYM(e, chan, sym, ret_state, n);
                e->idx++;
            } else {
                // 80-bit protocol has no per-frame tail; 56-bit only adds
                // inter-frame silence when more frames follow in the burst.
                e->state = (f->bit_length == 56 && f->tail) ? ST_TAIL : ST_DONE;
            }
            break;

        case ST_TAIL:
            // 56-bit inter-frame silence: ~27434 µs LOW
            sym.level0 = 0; sym.duration0 = 13717;
            sym.level1 = 0; sym.duration1 = 13717;
            SOMFY_WRITE_SYM(e, chan, sym, ret_state, n);
            e->state = ST_DONE;
            break;

        default:
            e->state = ST_DONE;
            break;
        }
    }

    *ret_state = static_cast<rmt_encode_state_t>(
                     static_cast<int>(*ret_state) |
                     static_cast<int>(RMT_ENCODING_COMPLETE));
    return n;
}

static esp_err_t somfy_encoder_reset(rmt_encoder_t *enc) {
    somfy_encoder_t *e = __containerof(enc, somfy_encoder_t, base);
    e->copy_enc->reset(e->copy_enc);
    e->state = ST_WAKEUP;
    e->idx   = 0;
    return ESP_OK;
}

static esp_err_t somfy_encoder_del(rmt_encoder_t *enc) {
    somfy_encoder_t *e = __containerof(enc, somfy_encoder_t, base);
    rmt_del_encoder(e->copy_enc);
    free(e);
    return ESP_OK;
}

static somfy_encoder_t     *s_enc       = nullptr;
static rmt_channel_handle_t s_rmtTxChan = nullptr;
// Per-burst frame storage; the RMT driver reads s_burst[i] asynchronously
// until transaction i completes, so this must outlive beginFrameTx() — hence
// static storage, guarded by the s_rmtBusy interlock.
static somfy_tx_frame_t s_burst[SOMFY_BURST_MAX];
// s_rmtBusy: set by beginFrameTx/beginRawFrameTx, cleared by loop() after endTransmit().
// s_txDone:  set by the ISR callback when the LAST queued frame finishes; cleared by loop().
// s_pendingFrames: decremented by the ISR per frame; when it hits 0, s_txDone is set.
// They are kept separate so txBusy() (= s_rmtBusy) stays true until loop() has run
// endTransmit() — if the ISR cleared s_rmtBusy directly, the loop condition
// `s_rmtBusy && !txBusy()` would become `s_rmtBusy && !s_rmtBusy` (always false).
static volatile bool    s_rmtBusy       = false;
static volatile bool    s_txDone        = false;
static volatile uint8_t s_pendingFrames = 0;

// ISR callback: fired by the RMT driver once per completed transaction.
// For multi-frame bursts we count down and only set s_txDone after the last.
static bool IRAM_ATTR rmt_tx_done_cb(rmt_channel_handle_t chan,
                                      const rmt_tx_done_event_data_t *edata,
                                      void *user_ctx)
{
    if(s_pendingFrames > 0) {
        s_pendingFrames = s_pendingFrames - 1;
        if(s_pendingFrames == 0) s_txDone = true;
    }
    return false; // no high-priority task woken
}


void IRAM_ATTR SomfyTransceiver::handleReceiveISR(void*) { SomfyTransceiver::handleReceive(); }

void RECEIVE_ATTR SomfyTransceiver::handleReceive() {
    static unsigned long last_time = 0;
    const unsigned long time = micros();
    const unsigned int duration = time - last_time;

    // RF noise watchdog: count rapid pulses; disable RX if threshold is exceeded.
    if(somfy.transceiver.config.noiseDetection) {
        uint32_t now = millis();
        if(now - noiseWindowStart > NOISE_WINDOW_MS) {
            noiseWindowStart = now;
            noisePulseCount = 0;
        }
        noisePulseCount = noisePulseCount + 1;
        if(noisePulseCount > NOISE_PULSE_THRESHOLD) {
            gpio_intr_disable((gpio_num_t)interruptPin);
            noiseDetected = true;
            noisePulseCount = 0;
            return;
        }
    }

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

void SomfyTransceiver::beginFrequencyScan() {
  if(this->config.enabled) {
    this->disableReceive();
    this->config.apply();
    rxmode = 3;
    gpio_set_direction((gpio_num_t)this->config.RXPin, GPIO_MODE_INPUT);
    interruptPin = this->config.RXPin;
    ELECHOUSE_cc1101.setRxBW(this->config.rxBandwidth);              // Set the Receive Bandwidth in kHz. Value from 58.03 to 812.50. Default is 812.50 kHz.
    ELECHOUSE_cc1101.SetRx();
    markFreq = currFreq = 433.0f;
    markRSSI = -100;
    ELECHOUSE_cc1101.setMHZ(currFreq);
    ESP_LOGI(TAG, "Begin frequency scan on Pin #%d", this->config.RXPin);
    gpio_set_intr_type((gpio_num_t)interruptPin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add((gpio_num_t)interruptPin, SomfyTransceiver::handleReceiveISR, NULL);
    this->emitFrequencyScan();
  }
}

void SomfyTransceiver::processFrequencyScan(bool received) {
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

void SomfyTransceiver::endFrequencyScan() {
  if(rxmode == 3) {
    rxmode = 0;
    if(interruptPin > 0) gpio_isr_handler_remove((gpio_num_t)interruptPin);
    interruptPin = 0;
    this->config.apply();
    this->emitFrequencyScan();
  }
}

void SomfyTransceiver::emitFrequencyScan(uint8_t num) {
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

bool SomfyTransceiver::receive(somfy_rx_t *rx) {
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

void SomfyTransceiver::emitFrame(somfy_frame_t *frame, somfy_rx_t *rx) {
  if(sockEmit.activeClients(ROOM_EMIT_FRAME) > 0) {
    JsonSockEvent *json = sockEmit.beginEmit("remoteFrame");
    json->beginObject();
    json->addElem("encKey", frame->encKey);
    json->addElem("address", (uint32_t)frame->remoteAddress);
    json->addElem("rcode", (uint32_t)frame->rollingCode);
    json->addElem("command", translateSomfyCommand(frame->cmd).c_str());
    json->addElem("rssi", (int32_t)frame->rssi);
    json->addElem("bits", rx ? rx->bit_length : static_cast<uint8_t>(0));
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
  }
}

void SomfyTransceiver::clearReceived(void) {
    if(this->config.enabled)
      gpio_isr_handler_add((gpio_num_t)interruptPin, SomfyTransceiver::handleReceiveISR, NULL);
}

void SomfyTransceiver::enableReceive(void) {
    uint32_t timing = millis();
    if(rxmode > 0) return;
    if(this->config.enabled) {
      rxmode = 1;
      gpio_set_direction((gpio_num_t)this->config.RXPin, GPIO_MODE_INPUT);
      interruptPin = this->config.RXPin;
      ELECHOUSE_cc1101.SetRx();
      gpio_set_intr_type((gpio_num_t)interruptPin, GPIO_INTR_ANYEDGE);
      gpio_uninstall_isr_service();
      gpio_install_isr_service(0);
      gpio_isr_handler_add((gpio_num_t)interruptPin, SomfyTransceiver::handleReceiveISR, NULL);
      ESP_LOGI(TAG, "Enabled receive on Pin #%d Timing: %ld", this->config.RXPin, millis() - timing);
    }
}

void SomfyTransceiver::disableReceive(void) {
  rxmode = 0;
  if(interruptPin > 0) gpio_isr_handler_remove((gpio_num_t)interruptPin);
  interruptPin = 0;
}

void SomfyTransceiver::toJSON(JsonResponse& json) {
    json.beginObject("config");
    this->config.toJSON(json);
    json.endObject();
}

bool SomfyTransceiver::fromJSON(JsonObject& obj) {
    if (obj.containsKey("config")) {
      JsonObject objConfig = obj["config"];
      this->config.fromJSON(objConfig);
    }
    return true;
}

bool SomfyTransceiver::usesPin(uint8_t pin) {
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

bool SomfyTransceiver::save() {
    this->config.save();
    this->config.apply();
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
    if(obj.containsKey("txPower")) this->txPower = obj["txPower"].as<int8_t>();
    if(obj.containsKey("proto")) this->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(obj.containsKey("noiseDetection")) this->noiseDetection = obj["noiseDetection"];
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
    json.addElem("proto", static_cast<int8_t>(this->proto));
    json.addElem("enabled", this->enabled);
    json.addElem("radioInit", this->radioInit);
    json.addElem("noiseDetection", this->noiseDetection);
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
    pref.putChar("proto", static_cast<int8_t>(this->proto));
    pref.putBool("noiseDet", this->noiseDetection);
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
    this->proto = static_cast<radio_proto>(pref.getChar("proto", static_cast<int8_t>(this->proto)));
    this->noiseDetection = pref.getBool("noiseDet", this->noiseDetection);
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
      gpio_set_direction((gpio_num_t)this->SCKPin,  GPIO_MODE_OUTPUT);
      gpio_set_direction((gpio_num_t)this->MOSIPin, GPIO_MODE_OUTPUT);
      gpio_set_direction((gpio_num_t)this->MISOPin, GPIO_MODE_INPUT);
      gpio_set_direction((gpio_num_t)this->CSNPin,  GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)this->CSNPin, 1);
      // Only (re-)route TXPin through the plain GPIO matrix when the RMT
      // peripheral has not claimed it.  gpio_set_direction() internally calls
      // esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX) which overwrites
      // the RMT routing; once rmt_new_tx_channel() owns the pin, skip this.
      if(this->TXPin != this->RXPin && s_rmtTxChan == nullptr)
        gpio_set_direction((gpio_num_t)this->TXPin, GPIO_MODE_OUTPUT);
      gpio_set_direction((gpio_num_t)this->RXPin, GPIO_MODE_INPUT);
      if(s_rmtTxChan == nullptr) {
        // RMT not yet initialised — safe to let the CC1101 library configure TXPin.
        if(this->TXPin == this->RXPin)
          ELECHOUSE_cc1101.setGDO0(this->TXPin);
        else
          ELECHOUSE_cc1101.setGDO(this->TXPin, this->RXPin);
      }
      // else: RMT owns TXPin — setGDO() → GDO_Set() → pinMode(TXPin, OUTPUT)
      // would overwrite the RMT GPIO routing; skip it entirely.
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
}

bool SomfyTransceiver::begin() {
    this->config.load();
    // Clear any radioInit=false guard left by a previous crash so that apply()
    // doesn't skip init on the first boot after a watchdog reset.
    Preferences pref;
    pref.begin("CC1101");
    pref.putBool("radioInit", true);
    pref.end();
    this->config.apply();
    rx_queue.init();
    ESP_LOGI(TAG, "RMT build: transceiver enabled=%d TXPin=%d", this->config.enabled, this->config.TXPin);
    if(this->config.enabled && s_rmtTxChan == nullptr) {
        rmt_tx_channel_config_t ch_cfg = {};
        ch_cfg.gpio_num          = (gpio_num_t)this->config.TXPin;
        ch_cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
        ch_cfg.resolution_hz     = 1000000; // 1 tick = 1 µs
        ch_cfg.mem_block_symbols = 96; // one frame fits (56-bit ≤61 syms, 80-bit ≤94 syms)
        ch_cfg.trans_queue_depth = SOMFY_BURST_MAX; // queue the whole burst up-front
        ch_cfg.flags.with_dma    = false;
        ESP_ERROR_CHECK(rmt_new_tx_channel(&ch_cfg, &s_rmtTxChan));

        somfy_encoder_t *enc = (somfy_encoder_t *)calloc(1, sizeof(somfy_encoder_t));
        enc->base.encode = somfy_encode;
        enc->base.reset  = somfy_encoder_reset;
        enc->base.del    = somfy_encoder_del;
        rmt_copy_encoder_config_t copy_cfg = {};
        ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &enc->copy_enc));
        s_enc = enc;

        rmt_tx_event_callbacks_t cbs = {};
        cbs.on_trans_done = rmt_tx_done_cb;
        ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(s_rmtTxChan, &cbs, nullptr));
        ESP_ERROR_CHECK(rmt_enable(s_rmtTxChan));
        ESP_LOGI(TAG, "RMT TX channel initialised on GPIO %d", this->config.TXPin);
    }
    return true;
}

bool SomfyTransceiver::end() {
    this->disableReceive();
    if(s_rmtTxChan) {
        rmt_tx_wait_all_done(s_rmtTxChan, 2000);
        rmt_disable(s_rmtTxChan);
        rmt_del_channel(s_rmtTxChan);
        s_rmtTxChan = nullptr;
    }
    if(s_enc) {
        rmt_del_encoder(&s_enc->base);
        s_enc = nullptr;
    }
    s_rmtBusy = false;
    s_txDone  = false;
    return true;
}

void SomfyTransceiver::loop() {
  // RF noise watchdog: re-enable RX after cooldown period.
  if(noiseDetected) {
    static uint32_t noiseDisabledAt = 0;
    if(noiseDisabledAt == 0) {
      noiseDisabledAt = millis();
      ESP_LOGW(TAG, "RF noise detected — RX disabled for %lu ms", (unsigned long)NOISE_COOLDOWN_MS);
    } else if(millis() - noiseDisabledAt > NOISE_COOLDOWN_MS) {
      ESP_LOGI(TAG, "RF noise cooldown elapsed — re-enabling RX");
      noiseDetected = false;
      noiseDisabledAt = 0;
      noisePulseCount = 0;
      noiseWindowStart = millis();
      gpio_intr_enable((gpio_num_t)interruptPin);
    }
    return;
  }

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
    // Finish a completed RMT transmission before doing anything else.
    static uint32_t s_busyWarnAt = 0;
    if(s_rmtBusy && s_txDone) {
      ESP_LOGI(TAG, "RMT TX done — calling endTransmit");
      this->endTransmit();
      s_rmtBusy = false;
      s_txDone  = false;
      s_busyWarnAt = 0;
      tx_queue.delay_time = millis() + TX_QUEUE_DELAY;
    } else if(s_rmtBusy) {
      if(s_busyWarnAt == 0) s_busyWarnAt = millis();
      else if(millis() - s_busyWarnAt > 3000) {
        ESP_LOGW(TAG, "RMT busy for >3s — txDone=%d (ISR never fired?)", (int)s_txDone);
        s_busyWarnAt = millis();
      }
    } else {
      s_busyWarnAt = 0;
    }
    somfy.processWaitingFrame();
    // Drain one repeater frame from the tx_queue when the channel is free.
    if(tx_queue.length > 0 && millis() > tx_queue.delay_time && somfy_rx.cpt_synchro_hw == 0 && !s_rmtBusy) {
      somfy_tx_t tx;
      tx_queue.pop(&tx);
      ESP_LOGI(TAG, "Sending frame %d - %d-BIT [", tx.hwsync, tx.bit_length);
      for(uint8_t j = 0; j < 10; j++) {
        ESP_LOGI(TAG, "%d", tx.payload[j]);
        if(j < 9) ESP_LOGI(TAG, ", ");
      }
      ESP_LOGI(TAG, "]");
      this->beginTransmit();
      this->beginRawFrameTx(tx.payload, tx.hwsync, tx.bit_length);
      // endTransmit() deferred — handled above when txBusy() clears
    }
  }
}

somfy_frame_t& SomfyTransceiver::lastFrame() { return this->frame; }

// Encode a Somfy frame burst (1 initial + N repeats) as separate RMT
// transactions and queue them all. Each transaction carries one self-contained
// frame (≤ mem_block_symbols), so the encoder never has to span DMA refills
// within a frame — eliminating the buffer-underrun class of on-air glitches.
// beginTransmit() must be called first. endTransmit() is deferred: it will
// be called by SomfyTransceiver::loop() once txBusy() returns false.
void SomfyTransceiver::beginFrameTx(somfy_frame_t &frame, uint8_t repeats) {
    if(repeats >= SOMFY_BURST_MAX) {
        ESP_LOGW(TAG, "Capping repeats %u→%u (burst queue depth)",
                 repeats, SOMFY_BURST_MAX - 1);
        repeats = SOMFY_BURST_MAX - 1;
    }
    const uint8_t total = repeats + 1;

    // Initial frame — wakeup + first_sync pulses. Tail if more frames follow.
    frame.encodeFrame(s_burst[0].bytes);
    s_burst[0].bit_length = frame.bitLength;
    s_burst[0].sync_count = (frame.bitLength == 56) ? 2 : 12;
    s_burst[0].wakeup     = true;
    s_burst[0].tail       = (total > 1);

    // Repeat frames — no wakeup, rep_sync pulses. Tail on all but the last.
    for(uint8_t i = 1; i < total; i++) {
        if(frame.bitLength == 80)
            frame.encode80BitFrame(s_burst[i].bytes, i);
        else
            memcpy(s_burst[i].bytes, s_burst[0].bytes, 10);
        s_burst[i].bit_length = frame.bitLength;
        s_burst[i].sync_count = (frame.bitLength == 56) ? 7 : 6;
        s_burst[i].wakeup     = false;
        s_burst[i].tail       = (i < total - 1);
    }

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count              = 0;
    tx_cfg.flags.eot_level         = 0; // line LOW between frames
    tx_cfg.flags.queue_nonblocking = 1; // never block app_main

    // Set bookkeeping BEFORE first transmit so an early ISR can't see stale state.
    s_pendingFrames = total;
    s_txDone  = false;
    s_rmtBusy = true;

    ESP_LOGI(TAG, "RMT beginFrameTx: bitlen=%d frames=%d", frame.bitLength, total);

    uint8_t queued = 0;
    for(uint8_t i = 0; i < total; i++) {
        esp_err_t err = rmt_transmit(s_rmtTxChan, &s_enc->base,
                                      &s_burst[i], sizeof(somfy_tx_frame_t), &tx_cfg);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_transmit frame %u/%u failed: %s",
                     i + 1, total, esp_err_to_name(err));
            break;
        }
        queued++;
    }

    if(queued < total) {
        // Some frames didn't queue. Best-effort adjust pendingFrames so the
        // ISR still reaches zero. Race with ISR is benign in the typical case
        // (queue_depth ≥ total means this branch never runs).
        const uint8_t skipped = total - queued;
        if(s_pendingFrames > skipped) s_pendingFrames -= skipped;
        else { s_pendingFrames = 0; s_txDone = true; }
    }
    if(queued == 0) {
        s_rmtBusy = false;
        s_txDone  = false;
    }
    ESP_LOGI(TAG, "RMT beginFrameTx: queued %u/%u", queued, total);
}

// Raw variant used by the repeater path: pre-encoded bytes, single frame.
// `sync` values of 2 or 12 mark an initial-frame (wakeup pulse), others are
// treated as repeat sync counts.
void SomfyTransceiver::beginRawFrameTx(byte *payload, uint8_t sync, uint8_t bitLength) {
    memcpy(s_burst[0].bytes, payload, 10);
    s_burst[0].bit_length = bitLength;
    s_burst[0].sync_count = sync;
    s_burst[0].wakeup     = (sync == 2 || sync == 12);
    s_burst[0].tail       = false;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count              = 0;
    tx_cfg.flags.eot_level         = 0;
    tx_cfg.flags.queue_nonblocking = 1;

    s_pendingFrames = 1;
    s_txDone  = false;
    s_rmtBusy = true;

    esp_err_t err = rmt_transmit(s_rmtTxChan, &s_enc->base,
                                  &s_burst[0], sizeof(somfy_tx_frame_t), &tx_cfg);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit raw failed: %s", esp_err_to_name(err));
        s_pendingFrames = 0;
        s_rmtBusy = false;
    }
}

bool SomfyTransceiver::txBusy() {
    return s_rmtBusy;
}

void SomfyTransceiver::beginTransmit() {
    if(this->config.enabled) {
      this->disableReceive();
      // RMT owns TXPin — skip gpio_set_direction() which would invoke
      // esp_rom_gpio_connect_out_signal(SIG_GPIO_OUT_IDX) and disconnect the peripheral.
      ELECHOUSE_cc1101.SetTx();
    }
}

void SomfyTransceiver::endTransmit() {
    if(this->config.enabled) {
      ELECHOUSE_cc1101.setSidle();
      //delay(100);
      this->enableReceive();
    }
}
