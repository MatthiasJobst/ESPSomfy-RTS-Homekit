// SomfyFrame.h — Protocol layer: radio protocol enums, command enums, shade/tilt/group type
// enums, timing macros, status flags, and the core data structures (somfy_frame_t,
// somfy_rx_t/queue, somfy_tx_t/queue, somfy_relay_t). Also declares sort_asc and
// translateSomfyCommand helpers.  All other Somfy modules depend on this header.
#pragma once
#include "ConfigSettings.h"
#include "WResp.h"


#define SOMFY_MAX_SHADES 32
#define SOMFY_MAX_GROUPS 16
#define SOMFY_MAX_LINKED_REMOTES 7
#define SOMFY_MAX_GROUPED_SHADES 32
#define SOMFY_MAX_ROOMS 16
#define SOMFY_MAX_REPEATERS 7
#define TX_QUEUE_DELAY 100

#define SECS_TO_MILLIS(x) ((x) * 1000)
#define MINS_TO_MILLIS(x) SECS_TO_MILLIS((x) * 60)

#define SOMFY_SUN_TIMEOUT MINS_TO_MILLIS(2)
#define SOMFY_NO_SUN_TIMEOUT MINS_TO_MILLIS(20)

#define SOMFY_WIND_TIMEOUT SECS_TO_MILLIS(2)
#define SOMFY_NO_WIND_TIMEOUT MINS_TO_MILLIS(12)
#define SOMFY_NO_WIND_REMOTE_TIMEOUT SECS_TO_MILLIS(30)


enum class radio_proto : byte { // Ordinal byte 0-255
  RTS = 0x00,
  RTW = 0x01,
  RTV = 0x02,
  GP_Relay = 0x08,
  GP_Remote = 0x09
};
enum class somfy_commands : byte {
    Unknown0 = 0x0,
    My = 0x1,
    Up = 0x2,
    MyUp = 0x3,
    Down = 0x4,
    MyDown = 0x5,
    UpDown = 0x6,
    MyUpDown = 0x7,
    Prog = 0x8,
    SunFlag = 0x9,
    Flag = 0xA,
    StepDown = 0xB,
    Toggle = 0xC,
    UnknownD = 0xD,
    Sensor = 0xE,
    RTWProto = 0xF, // RTW Protocol
    // Command extensions for 80 bit frames
    StepUp = 0x8B,
    Favorite = 0xC1,
    Stop = 0xF1
};
enum class group_types : byte {
  channel = 0x00
};
enum class shade_types : byte {
  roller = 0x00,
  blind = 0x01,
  ldrapery = 0x02,
  awning = 0x03,
  shutter = 0x04,
  garage1 = 0x05,
  garage3 = 0x06,
  rdrapery = 0x07,
  cdrapery = 0x08,
  drycontact = 0x09,
  drycontact2 = 0x0A,
  lgate = 0x0B,
  cgate = 0x0C,
  rgate = 0x0D,
  lgate1 = 0x0E,
  cgate1 = 0x0F,
  rgate1 = 0x10
};
enum class tilt_types : byte {
  none = 0x00,
  tiltmotor = 0x01,
  integrated = 0x02,
  tiltonly = 0x03,
  euromode = 0x04
};
String translateSomfyCommand(const somfy_commands cmd);
somfy_commands translateSomfyCommand(const String& string);

#define MAX_TIMINGS 300
#define MAX_RX_BUFFER 3
#define MAX_TX_BUFFER 5

typedef enum {
    waiting_synchro = 0,
    receiving_data = 1,
    complete = 2
} t_status;

struct somfy_rx_t {
    void clear() {
      this->status = t_status::waiting_synchro;
      this->bit_length = 56;
      this->cpt_synchro_hw = 0;
      this->cpt_bits = 0;
      this->previous_bit = 0;
      this->waiting_half_symbol = false;
      memset(this->payload, 0, sizeof(this->payload));
      memset(this->pulses, 0, sizeof(this->pulses));
      this->pulseCount = 0;
    }
    t_status status;
    uint8_t bit_length = 56;
    uint8_t cpt_synchro_hw = 0;
    uint8_t cpt_bits = 0;
    uint8_t previous_bit = 0;
    bool waiting_half_symbol;
    uint8_t payload[10];
    unsigned int pulses[MAX_TIMINGS];
    uint16_t pulseCount = 0;
};
// A simple FIFO queue to hold rx buffers.  We are using
// a byte index to make it so we don't have to reorganize
// the storage each time we push or pop.
struct somfy_rx_queue_t {
  void init();
  uint8_t length = 0;
  uint8_t index[MAX_RX_BUFFER];
  somfy_rx_t items[MAX_RX_BUFFER];
  void push(somfy_rx_t *rx);
  bool pop(somfy_rx_t *rx);
};
struct somfy_tx_t {
  void clear() {
    this->hwsync = 0;
    this->bit_length = 0;
    memset(this->payload, 0x00, sizeof(this->payload));
  }
  uint8_t hwsync = 0;
  uint8_t bit_length = 0;
  uint8_t payload[10] = {};
};
struct somfy_tx_queue_t {
  somfy_tx_queue_t() { this->clear(); }
  void clear() {
    for (uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
      this->index[i] = 255;
      this->items[i].clear();
    }
    this->length = 0;
  }
  unsigned long delay_time = 0;
  uint8_t length = 0;
  uint8_t index[MAX_TX_BUFFER] = {255};
  somfy_tx_t items[MAX_TX_BUFFER];
  bool pop(somfy_tx_t *tx);
  void push(somfy_rx_t *rx); // Used for repeats
  void push(uint8_t hwsync, byte *payload, uint8_t bit_length);
};

enum class somfy_flags_t : byte {
    SunFlag = 0x01,
    SunSensor = 0x02,
    DemoMode = 0x04,
    Light = 0x08,
    Windy = 0x10,
    Sunny = 0x20,
    Lighted = 0x40,
    SimMy = 0x80
};
enum class gpio_flags_t : byte {
  LowLevelTrigger = 0x01
};
struct somfy_relay_t {
  uint32_t remoteAddress = 0;
  uint8_t sync = 0;
  byte frame[10] = {0};
};
struct somfy_frame_t {
    bool valid = false;
    bool processed = false;
    bool synonym = false;
    radio_proto proto = radio_proto::RTS;
    int rssi = 0;
    byte lqi = 0x0;
    somfy_commands cmd;
    uint32_t remoteAddress = 0;
    uint16_t rollingCode = 0;
    uint8_t encKey = 0xA7;
    uint8_t checksum = 0;
    uint8_t hwsync = 0;
    uint8_t repeats = 0;
    uint32_t await = 0;
    uint8_t bitLength = 56;
    uint16_t pulseCount = 0;
    uint8_t stepSize = 0;
    void print();
    void encode80BitFrame(byte *frame, uint8_t repeat);
    byte calc80Checksum(byte b0, byte b1, byte b2);
    byte encode80Byte7(byte start, uint8_t repeat);
    void encodeFrame(byte *frame);
    void decodeFrame(byte* frame);
    void decodeFrame(somfy_rx_t *rx);
    bool isRepeat(somfy_frame_t &f);
    bool isSynonym(somfy_frame_t &f);
    void copy(somfy_frame_t &f);
};

