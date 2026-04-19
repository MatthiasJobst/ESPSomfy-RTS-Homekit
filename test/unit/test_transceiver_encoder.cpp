// test_transceiver_encoder.cpp — unit tests for the somfy_encode() state machine
// and somfy_encoder_reset().
//
// Strategy:
//   After begin(), rmt_stub_last_encoder == &s_enc->base.  SetUp triggers one
//   beginFrameTx() call to populate rmt_stub_last_encoder, then calls reset()
//   to put the encoder back at ST_WAKEUP so tests start from a clean state.
//
//   Because the stub copy encoder is a no-op that always returns 1 / COMPLETE,
//   SOMFY_WRITE_SYM never triggers an early MEM_FULL return.  Each state
//   therefore runs to completion in a single encode() call, and the return
//   value equals the exact symbol count:
//
//     n = (wakeup ? 1 : 0) + sync_count + 1(swsync) + bit_length
//           + (bit_length == 56 && tail ? 1 : 0)

#include <gtest/gtest.h>
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyTransceiver.h"
#include "../../main/somfy/SomfyFrame.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "Arduino.h"
#include "nvs.h"

extern SomfyShadeController somfy;
extern bool     cc1101_init_ok;
extern bool     gpio_intr_enabled;
extern void   (*gpio_last_isr)(void *);
extern uint64_t test_clock_ms;

// Layout-identical mirror of the module-private somfy_tx_frame_t.
struct enc_frame_t {
    uint8_t bytes[10];
    uint8_t bit_length;
    uint8_t sync_count;
    bool    wakeup;
    bool    tail;
};

// ── Fixture ──────────────────────────────────────────────────────────────────

class TransceiverEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_clock_ms = 35000; somfy.transceiver.loop();
        test_clock_ms = 70000; somfy.transceiver.loop();

        nvs_stub_reset_all();
        rmt_stub_queue_count  = 0;
        rmt_stub_done_cb      = nullptr;
        rmt_stub_done_cb_ctx  = nullptr;
        rmt_stub_last_encoder = nullptr;
        rmt_stub_fail_after   = 0;
        gpio_intr_enabled     = true;
        gpio_last_isr         = nullptr;
        cc1101_init_ok        = true;
        test_clock_ms         = 0;
        gpio_pin_levels.clear();

        somfy.transceiver.end();
        somfy.transceiver.config          = transceiver_config_t{};
        somfy.transceiver.config.enabled  = true;
        somfy.transceiver.config.TXPin    = 13;
        somfy.transceiver.config.RXPin    = 12;
        somfy.transceiver.config.save();
        somfy.transceiver.begin();

        // One beginFrameTx() call puts rmt_stub_last_encoder = &s_enc->base.
        somfy_frame_t f{};
        f.remoteAddress = 0x111111;
        f.rollingCode   = 1;
        f.cmd           = somfy_commands::Up;
        f.proto         = radio_proto::RTS;
        f.bitLength     = 56;
        f.encKey        = 0xA0;
        somfy.transceiver.beginTransmit();
        somfy.transceiver.beginFrameTx(f, 0);

        // Reset encoder so tests start at ST_WAKEUP with idx=0.
        if (rmt_stub_last_encoder)
            rmt_stub_last_encoder->reset(rmt_stub_last_encoder);
        rmt_stub_queue_count = 0;
    }

    void TearDown() override {
        somfy.transceiver.end();
        nvs_stub_reset_all();
    }

    // Run one full encode pass and return symbol count.
    size_t encode(const enc_frame_t &frame, rmt_encode_state_t *out = nullptr) {
        rmt_encode_state_t st = RMT_ENCODING_RESET;
        size_t n = rmt_stub_last_encoder->encode(
            rmt_stub_last_encoder, nullptr,
            &frame, sizeof(frame), &st);
        if (out) *out = st;
        return n;
    }

    // Reset encoder to ST_WAKEUP between encode calls.
    void resetEnc() {
        rmt_stub_last_encoder->reset(rmt_stub_last_encoder);
    }
};

// ── Encoder handle captured ────────────────────────────────────────────────

TEST_F(TransceiverEncoderTest, Begin_CapturesEncoderHandle) {
    ASSERT_NE(rmt_stub_last_encoder, nullptr);
    ASSERT_NE(rmt_stub_last_encoder->encode, nullptr);
    ASSERT_NE(rmt_stub_last_encoder->reset,  nullptr);
    ASSERT_NE(rmt_stub_last_encoder->del,    nullptr);
}

// ── Symbol counts verify full state traversal ──────────────────────────────

TEST_F(TransceiverEncoderTest, Encode_56bit_Wakeup_NoTail_SymbolCount) {
    // n = 1(wakeup) + 2(sync) + 1(swsync) + 56(data) = 60
    enc_frame_t f{};
    f.bit_length = 56; f.sync_count = 2; f.wakeup = true; f.tail = false;
    EXPECT_EQ(encode(f), 60u);
}

TEST_F(TransceiverEncoderTest, Encode_56bit_NoWakeup_NoTail_SymbolCount) {
    // n = 0 + 7(sync) + 1(swsync) + 56(data) = 64
    enc_frame_t f{};
    f.bit_length = 56; f.sync_count = 7; f.wakeup = false; f.tail = false;
    EXPECT_EQ(encode(f), 64u);
}

TEST_F(TransceiverEncoderTest, Encode_WakeupAddsOneSymbol) {
    enc_frame_t base{};
    base.bit_length = 56; base.sync_count = 2; base.tail = false;

    base.wakeup = false;
    size_t without = encode(base);
    resetEnc();

    base.wakeup = true;
    size_t with = encode(base);

    EXPECT_EQ(with, without + 1u) << "wakeup=true should emit exactly one extra symbol";
}

TEST_F(TransceiverEncoderTest, Encode_56bit_WithTail_SymbolCount) {
    // n = 0 + 7 + 1 + 56 + 1(tail) = 65
    enc_frame_t f{};
    f.bit_length = 56; f.sync_count = 7; f.wakeup = false; f.tail = true;
    EXPECT_EQ(encode(f), 65u);
}

TEST_F(TransceiverEncoderTest, Encode_80bit_NoTail_EvenWithTailFlag) {
    // 80-bit frames never emit a tail regardless of tail flag.
    // n = 1 + 12 + 1 + 80 = 94
    enc_frame_t f{};
    f.bit_length = 80; f.sync_count = 12; f.wakeup = true; f.tail = true;
    EXPECT_EQ(encode(f), 94u);
}

TEST_F(TransceiverEncoderTest, Encode_80bit_RepeatSync6_SymbolCount) {
    // n = 0 + 6 + 1 + 80 = 87
    enc_frame_t f{};
    f.bit_length = 80; f.sync_count = 6; f.wakeup = false; f.tail = false;
    EXPECT_EQ(encode(f), 87u);
}

// ── Return state ───────────────────────────────────────────────────────────

TEST_F(TransceiverEncoderTest, Encode_ReturnsCompleteFlag) {
    enc_frame_t f{};
    f.bit_length = 56; f.sync_count = 2; f.wakeup = true; f.tail = false;
    rmt_encode_state_t st = RMT_ENCODING_RESET;
    encode(f, &st);
    EXPECT_NE(st & RMT_ENCODING_COMPLETE, 0) << "encode() must set COMPLETE when done";
}

// ── Data bit paths ─────────────────────────────────────────────────────────

TEST_F(TransceiverEncoderTest, Encode_AllZeroBits_CorrectCount) {
    // Each data bit emits exactly 1 symbol regardless of value.
    enc_frame_t f{};
    memset(f.bytes, 0x00, 10);
    f.bit_length = 56; f.sync_count = 2; f.wakeup = false; f.tail = false;
    EXPECT_EQ(encode(f), 59u);  // 0+2+1+56 = 59
}

TEST_F(TransceiverEncoderTest, Encode_AllOneBits_CorrectCount) {
    enc_frame_t f{};
    memset(f.bytes, 0xFF, 10);
    f.bit_length = 56; f.sync_count = 2; f.wakeup = false; f.tail = false;
    EXPECT_EQ(encode(f), 59u);  // 0+2+1+56 = 59
}

// ── Reset ──────────────────────────────────────────────────────────────────

TEST_F(TransceiverEncoderTest, Reset_AllowsReencode) {
    enc_frame_t f{};
    f.bit_length = 56; f.sync_count = 2; f.wakeup = true; f.tail = false;

    size_t first  = encode(f);
    resetEnc();
    size_t second = encode(f);

    EXPECT_EQ(first, second) << "reset() must restore encoder to initial state";
}

TEST_F(TransceiverEncoderTest, Reset_ReturnsOK) {
    EXPECT_EQ(rmt_stub_last_encoder->reset(rmt_stub_last_encoder), ESP_OK);
}

// ── Del (via end()) ────────────────────────────────────────────────────────

TEST_F(TransceiverEncoderTest, End_FreesEncoder_NoCrash) {
    // end() calls rmt_del_encoder(&s_enc->base) → somfy_encoder_del() → free(e).
    // TearDown also calls end(); double-end must not crash.
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.end());
    // Null out to prevent TearDown double-free.
    rmt_stub_last_encoder = nullptr;
}
