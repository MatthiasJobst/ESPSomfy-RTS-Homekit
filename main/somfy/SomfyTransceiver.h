// SomfyTransceiver.h — CC1101 radio transceiver layer: transceiver_config_t (pin mapping,
// frequency, deviation, NVS persistence) and the SomfyTransceiver class (interrupt-driven
// receive, frame transmission, frequency scanning, GPIO management).  Also defines the
// SYMBOL, SETMY_REPEATS and TILT_REPEATS macros and the bit_length extern used by the
// send-command path.
#pragma once
#include "SomfyFrame.h"

#define SETMY_REPEATS 35
#define TILT_REPEATS  15

// bit_length is set by transceiver_config_t::apply() and read by SomfyRemote::sendCommand.
extern uint8_t bit_length;

struct transceiver_config_t {
    bool printBuffer = false;
    bool enabled = false;
    uint8_t type = 56;                // 56 or 80 bit protocol.
    radio_proto proto = radio_proto::RTS;
    uint8_t SCKPin = 18;
    uint8_t TXPin = 13;
    uint8_t RXPin = 12;
    uint8_t MOSIPin = 23;
    uint8_t MISOPin = 19;
    uint8_t CSNPin = 5;
    bool radioInit = false;
    float frequency = 433.42;         // Basic frequency
    float deviation = 47.60;          // Set the Frequency deviation in kHz. Value from 1.58 to 380.85. Default is 47.60 kHz.
    float rxBandwidth = 99.97;        // Receive bandwidth in kHz.  Value from 58.03 to 812.50.  Default is 99.97kHz.
    int8_t txPower = 10;              // Transmission power {-30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12}.  Default is 12.
    bool noiseDetection = false;      // Enable RF noise watchdog: disables RX if >100 pulses arrive within 10 s.
    void fromJSON(JsonObject& obj);
    //void toJSON(JsonObject& obj);
    void toJSON(JsonResponse& json);
    void save();
    void load();
    void apply();
    void removeNVSKey(const char *key);
};
class SomfyTransceiver {
  private:
    static void handleReceive();
    static void handleReceiveISR(void*);
    //bool _received = false;
    somfy_frame_t frame;
  public:
    transceiver_config_t config;
    bool printBuffer = false;
    //bool toJSON(JsonObject& obj);
    void toJSON(JsonResponse& json);
    bool fromJSON(JsonObject& obj);
    bool save();
    bool begin();
    void loop();
    bool end();
    bool receive(somfy_rx_t *rx);
    void clearReceived();
    void enableReceive();
    void disableReceive();
    somfy_frame_t& lastFrame();
    void sendFrame(byte *frame, uint8_t sync, uint8_t bitLength = 56);
    void beginTransmit();
    void endTransmit();
#ifdef SOMFY_TX_RMT
    void beginFrameTx(somfy_frame_t &frame, uint8_t repeats);
    void beginRawFrameTx(byte *payload, uint8_t sync, uint8_t bitLength);
    bool txBusy();
#endif
    void emitFrame(somfy_frame_t *frame, somfy_rx_t *rx = nullptr);
    void beginFrequencyScan();
    void endFrequencyScan();
    void processFrequencyScan(bool received = false);
    void emitFrequencyScan(uint8_t num = 255);
    bool usesPin(uint8_t pin);
};

