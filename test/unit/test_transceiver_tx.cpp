// test_transceiver_tx.cpp — unit tests for SomfyTransceiver RMT TX encoder.
//
// Strategy:
//   rmt_transmit() stub captures (data pointer, size) for each queued frame in
//   rmt_stub_queue[]. Tests cast each captured pointer to tx_frame_mirror_t —
//   a layout-identical copy of the module-private somfy_tx_frame_t — to inspect
//   wakeup, sync_count, tail, and bit_length fields without touching private state.
//
//   The done callback (rmt_stub_done_cb) is captured by begin() via
//   rmt_tx_register_event_callbacks(). Tests fire it manually to simulate the
//   RMT ISR, then call loop() to trigger endTransmit().

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
extern bool cc1101_init_ok;
extern bool gpio_intr_enabled;
extern void (*gpio_last_isr)(void *);
extern uint64_t test_clock_ms;

// Mirror of module-private somfy_tx_frame_t (must stay in sync with SomfyTransceiver.cpp).
struct tx_frame_mirror_t {
    uint8_t bytes[10];
    uint8_t bit_length;
    uint8_t sync_count;
    bool wakeup;
    bool tail;
};

// Helper: cast captured stub entry to mirror struct.
static const tx_frame_mirror_t *captured(int i)
{
    return reinterpret_cast<const tx_frame_mirror_t *>(rmt_stub_queue[i].data);
}

// Helper: fire the done callback once (simulates one RMT ISR).
static void fireDoneCb()
{
    if (rmt_stub_done_cb) rmt_stub_done_cb(nullptr, nullptr, rmt_stub_done_cb_ctx);
}

// Helper: build a minimal valid Somfy frame.
static somfy_frame_t makeFrame(uint8_t bitLength = 56)
{
    somfy_frame_t f;
    f.remoteAddress = 0x123456;
    f.rollingCode = 1;
    f.cmd = somfy_commands::Up;
    f.proto = radio_proto::RTS;
    f.bitLength = bitLength;
    f.encKey = 0xA0;
    return f;
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class TransceiverTxTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        // Reset noiseDetected (module-static) via the cooldown path so this
        // fixture is order-independent when the binary runs all tests in one process.
        test_clock_ms = 35000;
        somfy.transceiver.loop();
        test_clock_ms = 70000;
        somfy.transceiver.loop();

        nvs_stub_reset_all();
        rmt_stub_queue_count = 0;
        rmt_stub_done_cb = nullptr;
        rmt_stub_done_cb_ctx = nullptr;
        gpio_intr_enabled = true;
        gpio_last_isr = nullptr;
        cc1101_init_ok = true;
        test_clock_ms = 0;
        gpio_pin_levels.clear();

        somfy.transceiver.end(); // tears down any live RMT channel

        somfy.transceiver.config = transceiver_config_t{};
        somfy.transceiver.config.enabled = true;
        somfy.transceiver.config.TXPin = 13;
        somfy.transceiver.config.RXPin = 12;
        somfy.transceiver.config.save(); // writes radioInit=true

        somfy.transceiver.begin(); // allocates encoder, captures done callback
        rmt_stub_queue_count = 0;  // reset after begin()'s own rmt_transmit calls
    }

    void TearDown() override
    {
        // Drain any outstanding TX so s_rmtBusy and s_txDone are clear
        // before the next fixture's SetUp runs.
        rmt_stub_fail_after = 0;
        if (somfy.transceiver.txBusy()) {
            if (rmt_stub_done_cb) rmt_stub_done_cb(nullptr, nullptr, rmt_stub_done_cb_ctx);
            somfy.transceiver.loop();
        }
        somfy.transceiver.end();
        nvs_stub_reset_all();
    }
};

// ── txBusy() lifecycle ────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, TxBusy_FalseBeforeTransmit)
{
    EXPECT_FALSE(somfy.transceiver.txBusy());
}

TEST_F(TransceiverTxTest, TxBusy_TrueAfterBeginFrameTx)
{
    somfy_frame_t f = makeFrame();
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);
    EXPECT_TRUE(somfy.transceiver.txBusy());
}

TEST_F(TransceiverTxTest, TxBusy_StillTrueAfterAllCallbacksFired_BeforeLoop)
{
    // txBusy() == s_rmtBusy. It stays true until loop() calls endTransmit().
    somfy_frame_t f = makeFrame();
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 1); // total = 2 frames

    fireDoneCb();
    fireDoneCb();

    EXPECT_TRUE(somfy.transceiver.txBusy()) << "s_rmtBusy must not be cleared by the ISR callback";
}

TEST_F(TransceiverTxTest, Loop_ClearsTxBusy_AfterAllCallbacksFired)
{
    somfy_frame_t f = makeFrame();
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 2); // total = 3 frames

    fireDoneCb();
    fireDoneCb();
    fireDoneCb();

    somfy.transceiver.loop();

    EXPECT_FALSE(somfy.transceiver.txBusy()) << "loop() should call endTransmit() and clear s_rmtBusy";
}

// ── 56-bit burst setup ────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, BurstSetup_56bit_SingleFrame_Wakeup_NoTail)
{
    somfy_frame_t f = makeFrame(56);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0); // 0 repeats → 1 frame

    ASSERT_EQ(rmt_stub_queue_count, 1);
    const tx_frame_mirror_t *fr = captured(0);
    EXPECT_TRUE(fr->wakeup) << "Initial frame must have wakeup=true";
    EXPECT_EQ(fr->sync_count, 2u) << "Initial 56-bit frame: sync_count=2";
    EXPECT_FALSE(fr->tail) << "Single frame: tail=false";
    EXPECT_EQ(fr->bit_length, 56u);
}

TEST_F(TransceiverTxTest, BurstSetup_56bit_InitialFrame_TailSetWhenRepeats)
{
    somfy_frame_t f = makeFrame(56);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 3); // 3 repeats → 4 frames total

    ASSERT_GE(rmt_stub_queue_count, 1);
    const tx_frame_mirror_t *fr0 = captured(0);
    EXPECT_TRUE(fr0->wakeup);
    EXPECT_EQ(fr0->sync_count, 2u);
    EXPECT_TRUE(fr0->tail) << "Initial frame has tail=true when more frames follow";
}

TEST_F(TransceiverTxTest, BurstSetup_56bit_RepeatFrames_NoWakeup_Sync7)
{
    somfy_frame_t f = makeFrame(56);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 2); // total = 3 frames

    ASSERT_EQ(rmt_stub_queue_count, 3);
    for (int i = 1; i <= 2; i++) {
        const tx_frame_mirror_t *fr = captured(i);
        EXPECT_FALSE(fr->wakeup) << "Repeat frame " << i << ": wakeup=false";
        EXPECT_EQ(fr->sync_count, 7u) << "Repeat 56-bit frame: sync_count=7";
        EXPECT_EQ(fr->bit_length, 56u);
    }
}

TEST_F(TransceiverTxTest, BurstSetup_56bit_LastFrame_NoTail)
{
    somfy_frame_t f = makeFrame(56);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 2); // total = 3

    ASSERT_EQ(rmt_stub_queue_count, 3);
    EXPECT_TRUE(captured(1)->tail) << "Middle repeat: tail=true";
    EXPECT_FALSE(captured(2)->tail) << "Last frame: tail=false";
}

// ── 80-bit burst setup ────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, BurstSetup_80bit_InitialSync12)
{
    somfy_frame_t f = makeFrame(80);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);

    ASSERT_EQ(rmt_stub_queue_count, 1);
    const tx_frame_mirror_t *fr = captured(0);
    EXPECT_TRUE(fr->wakeup);
    EXPECT_EQ(fr->sync_count, 12u) << "Initial 80-bit frame: sync_count=12";
    EXPECT_EQ(fr->bit_length, 80u);
}

TEST_F(TransceiverTxTest, BurstSetup_80bit_RepeatSync6)
{
    somfy_frame_t f = makeFrame(80);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 1); // total = 2

    ASSERT_EQ(rmt_stub_queue_count, 2);
    const tx_frame_mirror_t *rep = captured(1);
    EXPECT_FALSE(rep->wakeup);
    EXPECT_EQ(rep->sync_count, 6u) << "Repeat 80-bit frame: sync_count=6";
}

// ── Burst size cap ────────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, RepeatsCapAt_BurstMax_Minus1)
{
    // SOMFY_BURST_MAX == 16; requesting 20 repeats should be capped to 15.
    somfy_frame_t f = makeFrame(56);
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 20);

    // Should queue exactly SOMFY_BURST_MAX (16) frames.
    EXPECT_EQ(rmt_stub_queue_count, 16) << "Repeats should be capped so total frames == SOMFY_BURST_MAX";
}

// ── beginRawFrameTx ───────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, RawFrameTx_Sync2_SetsWakeup)
{
    uint8_t payload[10] = {};
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginRawFrameTx(payload, 2, 56);

    ASSERT_EQ(rmt_stub_queue_count, 1);
    EXPECT_TRUE(captured(0)->wakeup) << "sync=2 marks initial frame: wakeup=true";
    EXPECT_EQ(captured(0)->sync_count, 2u);
}

TEST_F(TransceiverTxTest, RawFrameTx_Sync12_SetsWakeup)
{
    uint8_t payload[10] = {};
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginRawFrameTx(payload, 12, 80);

    ASSERT_EQ(rmt_stub_queue_count, 1);
    EXPECT_TRUE(captured(0)->wakeup) << "sync=12 marks initial 80-bit frame: wakeup=true";
    EXPECT_EQ(captured(0)->sync_count, 12u);
}

TEST_F(TransceiverTxTest, RawFrameTx_Sync7_NoWakeup)
{
    uint8_t payload[10] = {};
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginRawFrameTx(payload, 7, 56);

    ASSERT_EQ(rmt_stub_queue_count, 1);
    EXPECT_FALSE(captured(0)->wakeup) << "sync=7 marks repeat frame: wakeup=false";
    EXPECT_EQ(captured(0)->sync_count, 7u);
}

TEST_F(TransceiverTxTest, RawFrameTx_AlwaysNoTail)
{
    uint8_t payload[10] = {};
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginRawFrameTx(payload, 2, 56);

    EXPECT_FALSE(captured(0)->tail) << "beginRawFrameTx always sets tail=false";
}

// ── Payload bytes carried through ────────────────────────────────────────────

TEST_F(TransceiverTxTest, BurstSetup_56bit_PayloadBytesMatchEncoded)
{
    somfy_frame_t f = makeFrame(56);
    uint8_t expected[10] = {};
    f.encodeFrame(expected);

    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);

    ASSERT_EQ(rmt_stub_queue_count, 1);
    EXPECT_EQ(memcmp(captured(0)->bytes, expected, 10), 0)
        << "Frame bytes in s_burst[0] must match encodeFrame() output";
}

// ── RMT error paths ───────────────────────────────────────────────────────────

TEST_F(TransceiverTxTest, BeginFrameTx_FirstTransmitFails_TxBusyClearedImmediately)
{
    // rmt_stub_fail_after=1 → first rmt_transmit returns ESP_FAIL (queued=0).
    // beginFrameTx should clear s_rmtBusy since nothing was queued.
    rmt_stub_fail_after = 1;
    somfy_frame_t f = makeFrame();
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);

    EXPECT_FALSE(somfy.transceiver.txBusy()) << "All transmits failed → s_rmtBusy must be cleared immediately";
}

TEST_F(TransceiverTxTest, BeginFrameTx_PartialFailure_AdjustsPendingFrames)
{
    // rmt_stub_fail_after=2 → first transmit succeeds, second fails (queued=1 of 3).
    // s_rmtBusy stays true (at least one frame queued); pendingFrames adjusted.
    rmt_stub_fail_after = 2;
    somfy_frame_t f = makeFrame();
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 2); // total = 3

    EXPECT_TRUE(somfy.transceiver.txBusy()) << "At least 1 frame queued → s_rmtBusy must stay true";
}

TEST_F(TransceiverTxTest, BeginRawFrameTx_TransmitFails_TxBusyCleared)
{
    rmt_stub_fail_after = 1;
    uint8_t payload[10] = {};
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginRawFrameTx(payload, 2, 56);

    EXPECT_FALSE(somfy.transceiver.txBusy()) << "rmt_transmit raw failed → s_rmtBusy must be cleared";
}
