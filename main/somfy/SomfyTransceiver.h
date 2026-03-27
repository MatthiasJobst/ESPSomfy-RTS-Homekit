// SomfyTransceiver.h — CC1101 radio transceiver layer: transceiver_config_t (pin mapping,
// frequency, deviation, NVS persistence) and the Transceiver class (interrupt-driven
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
/*    
    bool internalCCMode = false;      // Use internal transmission mode FIFO buffers.
    byte modulationMode = 2;          // Modulation mode. 0 = 2-FSK, 1 = GFSK, 2 = ASK/OOK, 3 = 4-FSK, 4 = MSK.
    uint8_t channel = 0;              // The channel number from 0 to 255
    float channelSpacing = 199.95;    // Channel spacing in multiplied by the channel number and added to the base frequency in kHz. 25.39 to 405.45.  Default 199.95
    float dataRate = 99.97;           // The data rate in kBaud.  0.02 to 1621.83 Default is 99.97.
    uint8_t syncMode = 0;             // 0=No preamble/sync, 
    // 1=16 sync word bits detected, 
    // 2=16/16 sync words bits detected. 
    // 3=30/32 sync word bits detected, 
    // 4=No preamble/sync carrier above threshold
    // 5=15/16 + carrier above threshold. 
    // 6=16/16 + carrier-sense above threshold
    // 7=0/32 + carrier-sense above threshold
    uint16_t syncWordHigh = 211;      // The sync word used to the sync mode.
    uint16_t syncWordLow = 145;       // The sync word used to the sync mode.
    uint8_t addrCheckMode = 0;        // 0=No address filtration
    // 1=Check address without broadcast.
    // 2=Address check with 0 as broadcast.
    // 3=Address check with 0 or 255 as broadcast.
    uint8_t checkAddr = 0;            // Packet filter address depending on addrCheck settings.
    bool dataWhitening = false;       // Indicates whether data whitening should be applied.
    uint8_t pktFormat = 0;            // 0=Use FIFO buffers form RX and TX
    // 1=Synchronous serial mode.  RX on GDO0 and TX on either GDOx pins.
    // 2=Random TX mode.  Send data using PN9 generator.
    // 3=Asynchronous serial mode.  RX on GDO0 and TX on either GDOx pins.
    uint8_t pktLengthMode = 0;        // 0=Fixed packet length
    // 1=Variable packet length
    // 2=Infinite packet length
    // 3=Reserved
    uint8_t pktLength = 0;            // Packet length
    bool useCRC = false;              // Indicates whether CRC is to be used.
    bool autoFlushCRC = false;        // Automatically flush RX FIFO when CRC fails.  If more than one packet is in the buffer it too will be flushed.
    bool disableDCFilter = false;     // Digital blocking filter for demodulator.  Only for data rates <= 250k.
    bool enableManchester = true;     // Enable/disable Manchester encoding.
    bool enableFEC = false;           // Enable/disable forward error correction.
    uint8_t minPreambleBytes = 0;     // The minimum number of preamble bytes to be transmitten.
    // 0=2bytes
    // 1=3bytes
    // 2=4bytes
    // 3=6bytes
    // 4=8bytes
    // 5=12bytes
    // 6=16bytes
    // 7=24bytes
    uint8_t pqtThreshold = 0;         // Preamble quality estimator threshold.  The preable quality estimator increase an internal counter by one each time a bit is received that is different than the prevoius bit and
    // decreases the bounter by 8 each time a bit is received that is the same as the lats bit.  A threshold of 4 PQT for this counter is used to gate sync word detection.  
    // When PQT = 0 a sync word is always accepted.
    bool appendStatus = false;        // Appends the RSSI and LQI values to the TX packed as well as the CRC.
 */
    void fromJSON(JsonObject& obj);
    //void toJSON(JsonObject& obj);
    void toJSON(JsonResponse& json);
    void save();
    void load();
    void apply();
    void removeNVSKey(const char *key);
};
class Transceiver {
  private:
    static void handleReceive();
    bool _received = false;
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
    void emitFrame(somfy_frame_t *frame, somfy_rx_t *rx = nullptr);
    void beginFrequencyScan();
    void endFrequencyScan();
    void processFrequencyScan(bool received = false);
    void emitFrequencyScan(uint8_t num = 255);
    bool usesPin(uint8_t pin);
};

