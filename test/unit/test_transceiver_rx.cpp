// test_transceiver_rx.cpp — unit tests for SomfyTransceiver's interrupt-driven RX state
// machine and RF noise watchdog.
//
// Strategy: SomfyTransceiver::handleReceive() is private-static, but enableReceive() installs
// it as a GPIO ISR via gpio_isr_handler_add(). The GPIO stub captures the function pointer in
// gpio_last_isr. Tests advance test_clock_us and call gpio_last_isr(nullptr) to simulate edges.
//
// After injecting a full pulse sequence, transceiver.receive() pops from the decoded rx_queue
// and returns whether the frame is valid.

#include <gtest/gtest.h>
#include <vector>
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyTransceiver.h"
#include "../../main/somfy/SomfyFrame.h"
#include "driver/gpio.h"
#include "Arduino.h"

// Defined in transceiver_globals_stub.cpp
extern uint64_t test_clock_ms;
extern uint64_t test_clock_us;
extern void (*gpio_last_isr)(void *);
extern void *gpio_last_isr_arg;
extern bool gpio_intr_enabled;
extern SomfyShadeController somfy;

// ── Protocol timing constants (mirror SomfyTransceiver.cpp) ─────────────────
static constexpr uint32_t SYMBOL = 640;
static constexpr uint32_t WAKEUP_US = 9415;
static constexpr uint32_t HW_SYNC_US = SYMBOL * 4; // 2560
static constexpr uint32_t SW_SYNC_US = 4850;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Fire the ISR with `duration` µs elapsed since last edge.
static void sendPulse(uint32_t duration_us)
{
    test_clock_us += duration_us;
    if (gpio_last_isr) gpio_last_isr(gpio_last_isr_arg);
}

// Send a wakeup pulse (one HIGH edge measured at wakeup duration).
static void sendWakeup()
{
    sendPulse(WAKEUP_US);
}

// Send `count` hardware-sync symbols. Each symbol = two edges of HW_SYNC_US each
// (one rising + one falling), so the ISR counts every edge.
static void sendHwSync(uint8_t count)
{
    for (uint8_t i = 0; i < count * 2; i++)
        sendPulse(HW_SYNC_US);
}

// Send the software sync transition (a single measured HIGH of SW_SYNC_US).
// This triggers the waiting_synchro → receiving_data transition.
static void sendSwSync()
{
    sendPulse(SW_SYNC_US);
}

// Generate the pulse stream for a Manchester-encoded bit sequence.
// After sendSwSync() the line is LOW (end of sw_sync HIGH→LOW edge was just
// processed; the sw_sync LOW/start-0 bit has NOT yet been sent as a pulse).
// Caller must follow up with sendDataBits() immediately after sendSwSync().
//
// Manchester encoding (from SomfyTransceiver.cpp transmitter):
//   bit 0 → HIGH(SYMBOL), LOW(SYMBOL)
//   bit 1 → LOW(SYMBOL),  HIGH(SYMBOL)
// Adjacent same-level segments merge into one longer ISR-visible duration.
static void sendDataBits(const uint8_t *bytes, uint8_t bitLen)
{
    // The sw_sync ends with a LOW(SYMBOL) "start-0" bit. Tracking starts from LOW.
    int cur_level = 0;
    uint32_t acc = SYMBOL; // pre-load the start-0 LOW(SYMBOL) interval

    auto flush = [&]() {
        if (acc > 0) {
            sendPulse(acc);
            acc = 0;
        }
    };

    for (uint8_t i = 0; i < bitLen; i++) {
        uint8_t bit = (bytes[i / 8] >> (7 - (i % 8))) & 1;
        // bit 0: first half HIGH, second half LOW
        // bit 1: first half LOW,  second half HIGH
        int first_level = bit ? 0 : 1;
        int second_level = 1 - first_level;

        // First half-symbol
        if (first_level == cur_level) {
            acc += SYMBOL;
        } else {
            flush();
            cur_level = first_level;
            acc = SYMBOL;
        }
        // Second half-symbol
        if (second_level == cur_level) {
            acc += SYMBOL;
        } else {
            flush();
            cur_level = second_level;
            acc = SYMBOL;
        }
    }
    flush();
}

// ── Test fixture ─────────────────────────────────────────────────────────────
class TransceiverRxTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_clock_ms = 0;
        test_clock_us = 0;
        gpio_last_isr = nullptr;
        gpio_last_isr_arg = nullptr;
        gpio_intr_enabled = true;
        gpio_pin_levels.clear();

        // Ensure transceiver starts from a clean receive state.
        somfy.transceiver.disableReceive();

        somfy.transceiver.config.enabled = true;
        somfy.transceiver.config.noiseDetection = false;
        somfy.transceiver.config.RXPin = 12;

        // enableReceive() installs the ISR and captures its pointer in gpio_last_isr.
        somfy.transceiver.enableReceive();
    }

    void TearDown() override { somfy.transceiver.disableReceive(); }

    // Drain the rx_queue so successive tests start clean.
    void drainQueue()
    {
        somfy_rx_t rx;
        while (somfy.transceiver.receive(&rx)) {}
    }
};

// ── State-machine transition tests ───────────────────────────────────────────

TEST_F(TransceiverRxTest, GlitchPulseIsIgnored)
{
    // A pulse shorter than bitMin (448 µs) must not advance cpt_synchro_hw.
    // We verify this indirectly: after 8 glitches the ISR should NOT have
    // queued a complete frame (rx_queue stays empty → receive() returns false).
    for (int i = 0; i < 8; i++)
        sendPulse(100);
    somfy_rx_t rx;
    EXPECT_FALSE(somfy.transceiver.receive(&rx));
}

TEST_F(TransceiverRxTest, HwSyncPulsesAccumulate)
{
    // Sending only hw_sync pulses (no sw_sync) never produces a frame.
    sendHwSync(4);
    somfy_rx_t rx;
    EXPECT_FALSE(somfy.transceiver.receive(&rx));
}

TEST_F(TransceiverRxTest, WakeupPulseDoesNotProduceFrame)
{
    // A single wakeup pulse (~9415 µs) resets state but doesn't queue a frame.
    sendWakeup();
    somfy_rx_t rx;
    EXPECT_FALSE(somfy.transceiver.receive(&rx));
}

TEST_F(TransceiverRxTest, InvalidTimingInReceivingDataResetsToWaiting)
{
    // After entering receiving_data (4 hw_sync + sw_sync), an out-of-range
    // duration resets to waiting_synchro, so no frame is ever queued.
    sendHwSync(4);
    sendSwSync();
    sendPulse(50000); // way out of range → reset
    somfy_rx_t rx;
    EXPECT_FALSE(somfy.transceiver.receive(&rx));
}

// ── 56-bit frame decode ───────────────────────────────────────────────────────

TEST_F(TransceiverRxTest, FullFrame56Bit_ValidChecksum)
{
    // Build a known Somfy RTS frame and encode it to bytes.
    somfy_frame_t tx_frame;
    tx_frame.remoteAddress = 0x123456;
    tx_frame.rollingCode = 0x0001;
    tx_frame.cmd = somfy_commands::Up;
    tx_frame.proto = radio_proto::RTS;
    tx_frame.bitLength = 56;
    tx_frame.encKey = 0xA0;
    uint8_t encoded[10] = {};
    tx_frame.encodeFrame(encoded);

    // Transmit: wakeup + 2 hw_sync symbols + sw_sync + data bits.
    sendWakeup();
    sendHwSync(2);
    sendSwSync();
    sendDataBits(encoded, 56);

    somfy_rx_t rx;
    bool got = somfy.transceiver.receive(&rx);
    EXPECT_TRUE(got) << "receive() should return true for a valid frame";

    const somfy_frame_t &decoded = somfy.transceiver.lastFrame();
    EXPECT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.remoteAddress, tx_frame.remoteAddress);
    EXPECT_EQ(decoded.rollingCode, tx_frame.rollingCode);
    EXPECT_EQ(decoded.cmd, tx_frame.cmd);
}

// ── Bit-length detection ──────────────────────────────────────────────────────

TEST_F(TransceiverRxTest, BitLength56_WhenHwSyncLe7)
{
    // cpt_synchro_hw ≤ 7 → 56-bit protocol.
    // Send 4 hw_sync symbols (= cpt_synchro_hw of 8 edge-counts, but only 4 symbols).
    // Actually each hw_sync edge = one ISR call; 4 symbols × 2 edges = 8 edges.
    // The ISR sets bit_length=56 for cpt_synchro_hw ≤ 7 at sw_sync time.
    // Here cpt_synchro_hw will be 8 after 4 symbols — that maps to else→56.
    somfy_frame_t tx_frame;
    tx_frame.remoteAddress = 0xABCDEF;
    tx_frame.rollingCode = 0x0002;
    tx_frame.cmd = somfy_commands::Down;
    tx_frame.proto = radio_proto::RTS;
    tx_frame.bitLength = 56;
    tx_frame.encKey = 0xA0;
    uint8_t encoded[10] = {};
    tx_frame.encodeFrame(encoded);

    sendWakeup();
    sendHwSync(2); // 4 edges → cpt_synchro_hw=4 at sw_sync → ≤7 → 56-bit
    sendSwSync();
    sendDataBits(encoded, 56);

    somfy_rx_t rx;
    EXPECT_TRUE(somfy.transceiver.receive(&rx));
    EXPECT_EQ(rx.bit_length, 56u);
}

TEST_F(TransceiverRxTest, BitLength80_WhenHwSync12)
{
    // cpt_synchro_hw > 17 → 80-bit protocol.
    // 12 hw_sync symbols × 2 edges = 24 ISR calls → cpt_synchro_hw=24 at sw_sync.
    // Goal: verify bit_length selection, not frame checksum validity.
    // We use the 56-bit frame bytes but pad to 80 bits so the ISR captures a full
    // 80-bit payload and queues it (rx_queue entry has bit_length=80).
    somfy_frame_t tx_frame;
    tx_frame.remoteAddress = 0x654321;
    tx_frame.rollingCode = 0x0003;
    tx_frame.cmd = somfy_commands::My;
    tx_frame.proto = radio_proto::RTS;
    tx_frame.bitLength = 56;
    tx_frame.encKey = 0xA0;
    uint8_t encoded[10] = {};
    tx_frame.encodeFrame(encoded);

    sendWakeup();
    sendHwSync(12); // 24 edges → cpt_synchro_hw=24 → >17 → 80-bit
    sendSwSync();
    sendDataBits(encoded, 80); // feed 80 bits so ISR reaches cpt_bits == bit_length

    somfy_rx_t rx;
    somfy.transceiver.receive(&rx); // may or may not be valid — we only check bit_length
    EXPECT_EQ(rx.bit_length, 80u) << "hw_sync count >17 should select 80-bit frame length";
}

// ── Bit-length classifier matrix (Tahoma capture documentation) ──────────────
// SDR capture of a Tahoma controller, three transmissions per command:
//   tx #1 (initial):    ~13 hw-sync symbols + sw_sync + 80-bit data
//   tx #2/#3 (repeats): ~6-7 hw-sync symbols + sw_sync + 80-bit data
//
// Classifier (SomfyTransceiver.cpp:336):
//   bit_length = (cpt_synchro_hw == 12 || cpt_synchro_hw == 13 ||
//                 cpt_synchro_hw > 17) ? 80 : 56
// where cpt_synchro_hw = 2 × hw-sync symbol count emitted by sendHwSync(N).
//
// Use this matrix to pin actual hardware behaviour. If a real Tahoma repeat
// produces 7 symbols (cpt_synchro_hw=14), it is currently classified as 56-bit
// — colliding with standard 56-bit RTS repeats and requiring a separate
// disambiguator (e.g. carry bit_length forward from the first frame of the
// same address). 6 symbols (cpt_synchro_hw=12) lands cleanly on the 80-bit
// branch.
struct BitLenCase {
    uint8_t hwSyncSymbols;
    uint8_t expectedBitLength;
    const char *origin;
};

class TransceiverRxBitLengthMatrix : public TransceiverRxTest, public ::testing::WithParamInterface<BitLenCase> {};

TEST_P(TransceiverRxBitLengthMatrix, ClassifiesByHwSyncCount)
{
    const BitLenCase c = GetParam();

    somfy_frame_t tx;
    tx.remoteAddress = 0xA0B0C0;
    tx.rollingCode = 0x0001;
    tx.cmd = somfy_commands::My;
    tx.proto = radio_proto::RTS;
    tx.bitLength = 56; // payload encoder is 56-bit; we feed enough
                       // bits to satisfy `expectedBitLength` so the
                       // ISR pushes a queue entry.
    tx.encKey = 0xA0;
    uint8_t encoded[10] = {};
    tx.encodeFrame(encoded);

    sendWakeup();
    sendHwSync(c.hwSyncSymbols);
    sendSwSync();
    sendDataBits(encoded, c.expectedBitLength);

    somfy_rx_t rx;
    somfy.transceiver.receive(&rx);
    EXPECT_EQ(rx.bit_length, c.expectedBitLength) << "hwSyncSymbols=" << static_cast<int>(c.hwSyncSymbols)
                                                  << " (cpt_synchro_hw=" << 2 * static_cast<int>(c.hwSyncSymbols) << ")"
                                                  << " — " << c.origin;
}

INSTANTIATE_TEST_SUITE_P(BitLengthBuckets, TransceiverRxBitLengthMatrix,
                         ::testing::Values(BitLenCase{2u, 56u, "standard 56-bit first tx (hw=4)"},
                                           BitLenCase{7u, 56u, "standard 56-bit repeat (hw=14)"},
                                           BitLenCase{6u, 80u, "80-bit via hw==12 — Tahoma repeat hypothesis A"},
                                           BitLenCase{12u, 80u, "80-bit via hw>17 path (hw=24)"},
                                           BitLenCase{13u, 80u, "80-bit via hw>17 — Tahoma first tx (hw=26)"}),
                         [](const ::testing::TestParamInfo<BitLenCase> &info) {
                             return std::string("Sym") + std::to_string(info.param.hwSyncSymbols) + "_Bits" +
                                    std::to_string(info.param.expectedBitLength);
                         });

// ── Queue overflow ────────────────────────────────────────────────────────────

// Helper: send one complete 56-bit frame with the given command.
static void sendOneFrame(somfy_commands cmd, uint32_t addr = 0x111111)
{
    somfy_frame_t f;
    f.remoteAddress = addr;
    f.rollingCode = 1;
    f.cmd = cmd;
    f.proto = radio_proto::RTS;
    f.bitLength = 56;
    f.encKey = 0xA0;
    uint8_t enc[10] = {};
    f.encodeFrame(enc);

    sendWakeup();
    sendHwSync(2);
    sendSwSync();
    sendDataBits(enc, 56);
}

TEST_F(TransceiverRxTest, QueueOverflow_OldestFrameDropped)
{
    // MAX_RX_BUFFER == 3. Push 4 frames; the queue should hold exactly 3,
    // and receive() should succeed for 3 calls then return false.
    sendOneFrame(somfy_commands::Up);
    sendOneFrame(somfy_commands::Down);
    sendOneFrame(somfy_commands::My);
    sendOneFrame(somfy_commands::Prog); // 4th frame — triggers overflow

    somfy_rx_t rx;
    int count = 0;
    while (somfy.transceiver.receive(&rx))
        count++;

    EXPECT_EQ(count, MAX_RX_BUFFER) << "Queue should hold MAX_RX_BUFFER frames after overflow";
}

// ── RF noise watchdog ─────────────────────────────────────────────────────────

TEST_F(TransceiverRxTest, NoiseWatchdog_DisabledByDefault_RxStaysOn)
{
    // noiseDetection = false: >100 pulses within 10 ms must not disable the ISR.
    somfy.transceiver.config.noiseDetection = false;
    gpio_intr_enabled = true;

    for (int i = 0; i < 110; i++)
        sendPulse(50); // rapid glitch-like pulses

    EXPECT_TRUE(gpio_intr_enabled) << "Interrupt should remain enabled when noiseDetection is off";
}

TEST_F(TransceiverRxTest, NoiseWatchdog_Enabled_DisablesRxOnThreshold)
{
    // noiseDetection = true: >100 pulses within NOISE_WINDOW_MS (10 s) disables RX.
    somfy.transceiver.config.noiseDetection = true;
    gpio_intr_enabled = true;
    test_clock_ms = 0;

    // Inject 101 rapid pulses within the 10 s window (50 µs each → well within 10 s).
    for (int i = 0; i < 101; i++) {
        sendPulse(50);
        test_clock_ms = (test_clock_us / 1000); // keep ms clock consistent
    }

    EXPECT_FALSE(gpio_intr_enabled) << "Interrupt should be disabled after noise threshold exceeded";
}

TEST_F(TransceiverRxTest, NoiseWatchdog_CounterResets_AfterWindow)
{
    // The pulse counter resets at the start of each 10 s window. Sending 50 pulses,
    // advancing 11 s, then 50 more should NOT trigger the watchdog (each window < 100).
    somfy.transceiver.config.noiseDetection = true;
    gpio_intr_enabled = true;

    for (int i = 0; i < 50; i++)
        sendPulse(50);
    test_clock_ms = 11000; // skip past the 10 s window

    for (int i = 0; i < 50; i++)
        sendPulse(50);

    EXPECT_TRUE(gpio_intr_enabled) << "Interrupt should stay enabled when pulses are spread across windows";
}

// ── Half-symbol boundary: pulse durations that reset receiving_data state ────
//
// The ISR has three duration zones inside receiving_data:
//   < bitMin (448)              → glitch, absorbed (no state change)
//   > 448 && < 832              → valid half-symbol
//   > 896 && < 1664             → valid full symbol
//   anything else               → reset to waiting_synchro
//
// Exact boundaries 448 and 832 are the "neither fish nor fowl" values: 448
// passes the glitch filter (>= bitMin) but fails the strict `> 448` half-symbol
// check; 832 fails the strict `< 832` half-symbol check.  Both reset state.

struct HalfSymBoundaryParam {
    uint32_t probe_us;
};

class HalfSymbolResetBoundaryTest : public TransceiverRxTest,
                                    public ::testing::WithParamInterface<HalfSymBoundaryParam> {};

TEST_P(HalfSymbolResetBoundaryTest, ResetsToWaitingOnOutOfRangePulse)
{
    // Enter receiving_data, inject the probe, verify no frame is ever queued.
    sendWakeup();
    sendHwSync(2);
    sendSwSync();
    sendPulse(GetParam().probe_us);

    somfy_rx_t rx{};
    EXPECT_FALSE(somfy.transceiver.receive(&rx))
        << "probe=" << GetParam().probe_us << " µs should reset receiving_data";
}

INSTANTIATE_TEST_SUITE_P(
    Boundaries, HalfSymbolResetBoundaryTest,
    ::testing::Values(HalfSymBoundaryParam{448},  // == bitMin: passes glitch filter, NOT > half_symbol_min → RESET
                      HalfSymBoundaryParam{832},  // == half_symbol_max: NOT < half_symbol_max → RESET
                      HalfSymBoundaryParam{895},  // gap between half-symbol and full-symbol ranges → RESET
                      HalfSymBoundaryParam{50000} // far out of range → RESET
                      ));
