// test_transceiver_loop.cpp — integration tests for SomfyTransceiver::loop().
//
// loop() is the main-loop hook that:
//   1. Calls receive() → decodes a queued RX frame → passes it to somfy.processFrame()
//   2. Calls endTransmit() (re-enables RX) when s_rmtBusy && s_txDone
//   3. Returns early when noiseDetected is true, deferring all other work
//
// Setup resets noiseDetected (module-static) via the cooldown path so that
// tests are independent regardless of execution order.

#include <gtest/gtest.h>
#include <cstring>
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyTransceiver.h"
#include "../../main/somfy/SomfyFrame.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "Arduino.h"
#include "nvs.h"

extern SomfyShadeController somfy;
extern bool    cc1101_init_ok;
extern bool    gpio_intr_enabled;
extern void  (*gpio_last_isr)(void *);
extern void   *gpio_last_isr_arg;
extern uint64_t test_clock_ms;
extern uint64_t test_clock_us;

// ── Protocol constants ────────────────────────────────────────────────────────
static constexpr uint32_t SYMBOL     = 640;
static constexpr uint32_t WAKEUP_US  = 9415;
static constexpr uint32_t HW_SYNC_US = SYMBOL * 4;
static constexpr uint32_t SW_SYNC_US = 4850;

// ── Pulse injection helpers (mirrors test_transceiver_rx.cpp) ─────────────────

static void sendPulse(uint32_t us) {
    test_clock_us += us;
    if (gpio_last_isr) gpio_last_isr(gpio_last_isr_arg);
}

static void sendHwSync(uint8_t count) {
    for (uint8_t i = 0; i < count * 2; i++) sendPulse(HW_SYNC_US);
}

static void sendSwSync() { sendPulse(SW_SYNC_US); }

static void sendDataBits(const uint8_t *bytes, uint8_t bitLen) {
    int cur_level = 0;
    uint32_t acc  = SYMBOL;

    auto flush = [&]() { if (acc > 0) { sendPulse(acc); acc = 0; } };

    for (uint8_t i = 0; i < bitLen; i++) {
        uint8_t bit = (bytes[i / 8] >> (7 - (i % 8))) & 1;
        int first  = bit ? 0 : 1;
        int second = 1 - first;

        if (first == cur_level)        acc += SYMBOL;
        else { flush(); cur_level = first;  acc = SYMBOL; }

        if (second == cur_level)       acc += SYMBOL;
        else { flush(); cur_level = second; acc = SYMBOL; }
    }
    flush();
}

// Inject one complete valid 56-bit frame into the RX ISR.
static somfy_frame_t injectFrame(uint32_t addr = 0x123456,
                                 somfy_commands cmd = somfy_commands::Up) {
    somfy_frame_t f;
    f.remoteAddress = addr;
    f.rollingCode   = 1;
    f.cmd           = cmd;
    f.proto         = radio_proto::RTS;
    f.bitLength     = 56;
    f.encKey        = 0xA0;
    uint8_t enc[10] = {};
    f.encodeFrame(enc);

    sendPulse(WAKEUP_US);
    sendHwSync(2);
    sendSwSync();
    sendDataBits(enc, 56);
    return f;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class TransceiverLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        // noiseDetected is a module-static volatile bool. Reset it by simulating
        // the cooldown: two loop() calls 35 s apart trigger the reset branch.
        test_clock_ms = 35000;
        somfy.transceiver.loop();
        test_clock_ms = 70000;
        somfy.transceiver.loop();

        nvs_stub_reset_all();
        rmt_stub_queue_count  = 0;
        rmt_stub_done_cb      = nullptr;
        rmt_stub_done_cb_ctx  = nullptr;
        test_clock_ms         = 0;
        test_clock_us         = 0;
        gpio_last_isr         = nullptr;
        gpio_last_isr_arg     = nullptr;
        gpio_intr_enabled     = true;
        cc1101_init_ok        = true;
        gpio_pin_levels.clear();

        somfy.transceiver.end();
        somfy.transceiver.config              = transceiver_config_t{};
        somfy.transceiver.config.enabled      = true;
        somfy.transceiver.config.RXPin        = 12;
        somfy.transceiver.config.TXPin        = 13;
        somfy.transceiver.config.noiseDetection = false;
        somfy.transceiver.config.save();
        somfy.transceiver.begin();
        rmt_stub_queue_count = 0;  // discard begin()'s own rmt_transmit calls
    }

    void TearDown() override {
        somfy.transceiver.end();
        nvs_stub_reset_all();
    }
};

// ── RX path ───────────────────────────────────────────────────────────────────

TEST_F(TransceiverLoopTest, Loop_DrainsRxQueue) {
    // After loop() processes the queued frame, a subsequent receive() returns false.
    injectFrame();

    somfy.transceiver.loop();

    somfy_rx_t rx;
    EXPECT_FALSE(somfy.transceiver.receive(&rx))
        << "loop() should have drained the rx_queue";
}

TEST_F(TransceiverLoopTest, Loop_DecodesFrame_IntoLastFrame) {
    // loop() calls receive() which decodes into this->frame; lastFrame() exposes it.
    const uint32_t expectedAddr = 0xABCDEF;
    injectFrame(expectedAddr, somfy_commands::Down);

    somfy.transceiver.loop();

    EXPECT_EQ(somfy.transceiver.lastFrame().remoteAddress, expectedAddr);
}

// ── TX completion path ────────────────────────────────────────────────────────

TEST_F(TransceiverLoopTest, Loop_EndTransmit_WhenTxDone) {
    // beginFrameTx queues 1 frame (s_pendingFrames=1). Fire the done callback so
    // s_txDone=true. loop() should call endTransmit() → enableReceive() → ISR set.
    somfy_frame_t f;
    f.remoteAddress = 0x111111; f.rollingCode = 1;
    f.cmd = somfy_commands::Up; f.proto = radio_proto::RTS;
    f.bitLength = 56; f.encKey = 0xA0;

    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);  // total = 1

    gpio_last_isr = nullptr;  // clear so we can detect the re-install

    if (rmt_stub_done_cb) rmt_stub_done_cb(nullptr, nullptr, rmt_stub_done_cb_ctx);

    somfy.transceiver.loop();

    EXPECT_FALSE(somfy.transceiver.txBusy())
        << "loop() must clear s_rmtBusy after endTransmit";
    EXPECT_NE(gpio_last_isr, nullptr)
        << "endTransmit() calls enableReceive() which re-installs the ISR";
}

// ── Noise detection path ──────────────────────────────────────────────────────

TEST_F(TransceiverLoopTest, Loop_NoiseDetected_PreventsTxCompletion) {
    // Trigger the noise watchdog (>100 pulses while noiseDetection=true).
    // Then queue a frame and fire all done callbacks. loop() should return early
    // due to noiseDetected and leave txBusy() == true.
    somfy.transceiver.config.noiseDetection = true;
    gpio_intr_enabled = true;

    for (int i = 0; i < 101; i++) {
        test_clock_us += 50;
        if (gpio_last_isr) gpio_last_isr(gpio_last_isr_arg);
        test_clock_ms = test_clock_us / 1000;
    }

    somfy_frame_t f;
    f.remoteAddress = 0x222222; f.rollingCode = 1;
    f.cmd = somfy_commands::Up; f.proto = radio_proto::RTS;
    f.bitLength = 56; f.encKey = 0xA0;

    rmt_stub_queue_count = 0;
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);

    if (rmt_stub_done_cb) rmt_stub_done_cb(nullptr, nullptr, rmt_stub_done_cb_ctx);

    somfy.transceiver.loop();

    EXPECT_TRUE(somfy.transceiver.txBusy())
        << "loop() must return early when noiseDetected, leaving txBusy() true";
}
