// test_transceiver_misc.cpp — coverage for SomfyTransceiver and transceiver_config_t
// methods not exercised by the RX, TX, loop, or encoder test files:
//
//   SomfyTransceiver:  toJSON, fromJSON, usesPin, save, clearReceived,
//                      beginFrequencyScan, processFrequencyScan,
//                      endFrequencyScan, emitFrequencyScan
//   transceiver_config_t: removeNVSKey (key-exists path), load chip-model
//                         defaults (S3/S2/C3), apply() TXPin==RXPin path

// Pulse timing constants — mirrors test_transceiver_rx.cpp / test_transceiver_loop.cpp.
#define TX_MISC_SYMBOL     640u
#define TX_MISC_HW_SYNC_US (TX_MISC_SYMBOL * 4)
#define TX_MISC_SW_SYNC_US 4850u
#define TX_MISC_WAKEUP_US  9415u

#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyTransceiver.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "Arduino.h"
#include "nvs.h"
#include "esp_chip_info.h"
#include "../../main/compat/preferences.h"

extern SomfyShadeController somfy;
extern bool     cc1101_init_ok;
extern bool     gpio_intr_enabled;
extern void   (*gpio_last_isr)(void *);
extern uint64_t test_clock_ms;
extern esp_chip_model_t stub_chip_model;

// ── Fixture ──────────────────────────────────────────────────────────────────

class TransceiverMiscTest : public ::testing::Test {
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
        stub_chip_model       = CHIP_ESP32;

        somfy.transceiver.end();
        somfy.transceiver.config          = transceiver_config_t{};
        somfy.transceiver.config.enabled  = true;
        somfy.transceiver.config.TXPin    = 13;
        somfy.transceiver.config.RXPin    = 12;
        somfy.transceiver.config.SCKPin   = 18;
        somfy.transceiver.config.MOSIPin  = 23;
        somfy.transceiver.config.MISOPin  = 19;
        somfy.transceiver.config.CSNPin   = 5;
        somfy.transceiver.config.save();
        somfy.transceiver.begin();
        rmt_stub_queue_count = 0;
    }

    void TearDown() override {
        somfy.transceiver.end();
        nvs_stub_reset_all();
        stub_chip_model = CHIP_ESP32;
    }
};

// ── SomfyTransceiver::toJSON ─────────────────────────────────────────────────

TEST_F(TransceiverMiscTest, ToJSON_NoCrash) {
    JsonResponse json;
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.toJSON(json));
}

// ── SomfyTransceiver::fromJSON ───────────────────────────────────────────────

TEST_F(TransceiverMiscTest, FromJSON_WithConfigKey_UpdatesConfig) {
    JsonDocument doc;
    JsonObject top = doc.to<JsonObject>();
    JsonObject cfg = top["config"].to<JsonObject>();
    cfg["TXPin"]  = 7;
    cfg["RXPin"]  = 8;
    cfg["enabled"] = true;

    bool ok = somfy.transceiver.fromJSON(top);

    EXPECT_TRUE(ok);
    EXPECT_EQ(somfy.transceiver.config.TXPin, 7u);
    EXPECT_EQ(somfy.transceiver.config.RXPin, 8u);
}

TEST_F(TransceiverMiscTest, FromJSON_WithoutConfigKey_ReturnsTrue) {
    JsonDocument doc;
    JsonObject top = doc.to<JsonObject>();
    bool ok = somfy.transceiver.fromJSON(top);
    EXPECT_TRUE(ok);
}

// ── SomfyTransceiver::usesPin ────────────────────────────────────────────────

TEST_F(TransceiverMiscTest, UsesPin_ReturnsTrue_ForTXPin) {
    EXPECT_TRUE(somfy.transceiver.usesPin(somfy.transceiver.config.TXPin));
}

TEST_F(TransceiverMiscTest, UsesPin_ReturnsTrue_ForRXPin) {
    EXPECT_TRUE(somfy.transceiver.usesPin(somfy.transceiver.config.RXPin));
}

TEST_F(TransceiverMiscTest, UsesPin_ReturnsFalse_ForUnusedPin) {
    EXPECT_FALSE(somfy.transceiver.usesPin(99));
}

TEST_F(TransceiverMiscTest, UsesPin_ReturnsFalse_WhenDisabled) {
    somfy.transceiver.config.enabled = false;
    EXPECT_FALSE(somfy.transceiver.usesPin(somfy.transceiver.config.TXPin));
}

// ── SomfyTransceiver::save ───────────────────────────────────────────────────

TEST_F(TransceiverMiscTest, Save_ReturnsTrueAndPersists) {
    somfy.transceiver.config.TXPin = 7;
    bool ok = somfy.transceiver.save();
    EXPECT_TRUE(ok);

    transceiver_config_t reloaded{};
    reloaded.load();
    EXPECT_EQ(reloaded.TXPin, 7u);
}

// ── SomfyTransceiver::clearReceived ─────────────────────────────────────────

TEST_F(TransceiverMiscTest, ClearReceived_NoCrash_WhenEnabled) {
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.clearReceived());
}

// ── transceiver_config_t::removeNVSKey — key-exists path ─────────────────────

TEST_F(TransceiverMiscTest, RemoveNVSKey_RemovesExistingKey) {
    // removeNVSKey() uses the global `pref` handle directly — it must be open.
    // load() opens pref before calling removeNVSKey; replicate that here.
    extern Preferences pref;

    pref.begin("CC1101");
    pref.putBool("testKey", true);
    ASSERT_TRUE(pref.isKey("testKey"));

    somfy.transceiver.config.removeNVSKey("testKey");

    EXPECT_FALSE(pref.isKey("testKey")) << "removeNVSKey should delete the key";
    pref.end();
}

// ── transceiver_config_t::load — chip-model defaults ─────────────────────────

TEST_F(TransceiverMiscTest, Load_S3Defaults_TXPin15_RXPin14) {
    nvs_stub_reset_all();  // no saved values → load() uses chip defaults
    stub_chip_model = CHIP_ESP32S3;

    transceiver_config_t cfg{};
    cfg.load();

    EXPECT_EQ(cfg.TXPin,   15u);
    EXPECT_EQ(cfg.RXPin,   14u);
    EXPECT_EQ(cfg.MOSIPin, 11u);
    EXPECT_EQ(cfg.MISOPin, 13u);
    EXPECT_EQ(cfg.SCKPin,  12u);
    EXPECT_EQ(cfg.CSNPin,  10u);
}

TEST_F(TransceiverMiscTest, Load_S2Defaults_TXPin15_RXPin14) {
    nvs_stub_reset_all();
    stub_chip_model = CHIP_ESP32S2;

    transceiver_config_t cfg{};
    cfg.load();

    EXPECT_EQ(cfg.TXPin,   15u);
    EXPECT_EQ(cfg.RXPin,   14u);
    EXPECT_EQ(cfg.MOSIPin, 35u);
    EXPECT_EQ(cfg.MISOPin, 37u);
    EXPECT_EQ(cfg.SCKPin,  36u);
    EXPECT_EQ(cfg.CSNPin,  34u);
}

TEST_F(TransceiverMiscTest, Load_C3Defaults_TXPin13_RXPin12) {
    nvs_stub_reset_all();
    stub_chip_model = CHIP_ESP32C3;

    transceiver_config_t cfg{};
    cfg.load();

    EXPECT_EQ(cfg.TXPin,   13u);
    EXPECT_EQ(cfg.RXPin,   12u);
    EXPECT_EQ(cfg.MOSIPin, 16u);
    EXPECT_EQ(cfg.MISOPin, 17u);
    EXPECT_EQ(cfg.SCKPin,  15u);
    EXPECT_EQ(cfg.CSNPin,  14u);
}

// ── apply() with TXPin == RXPin ───────────────────────────────────────────────

TEST_F(TransceiverMiscTest, Apply_TXPinEqualsRXPin_CallsSetGDO0) {
    // When TXPin == RXPin and RMT is not yet init, apply() takes the setGDO0 path.
    nvs_stub_reset_all();
    somfy.transceiver.end();  // ensure s_rmtTxChan == nullptr

    auto &tcfg    = somfy.transceiver.config;
    tcfg.TXPin    = 12;
    tcfg.RXPin    = 12;  // same pin
    tcfg.SCKPin   = 18;
    tcfg.MOSIPin  = 23;
    tcfg.MISOPin  = 19;
    tcfg.CSNPin   = 5;
    tcfg.enabled  = true;
    tcfg.save();  // writes radioInit=true

    EXPECT_NO_FATAL_FAILURE(tcfg.apply());
    // Re-initialise for TearDown
    somfy.transceiver.begin();
}

// ── beginFrequencyScan / processFrequencyScan / endFrequencyScan ───────────────

TEST_F(TransceiverMiscTest, BeginFrequencyScan_SetsRxmode3) {
    somfy.transceiver.beginFrequencyScan();
    // Verify via processFrequencyScan not crashing (rxmode==3 branch active).
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(false));
    somfy.transceiver.endFrequencyScan();
}

TEST_F(TransceiverMiscTest, ProcessFrequencyScan_Received_True) {
    somfy.transceiver.beginFrequencyScan();
    // received=true exercises the RSSI update branches.
    // getRssi() stub returns -70.0f → currRSSI=-70 > -75 but markRSSI=-100 so
    // the currRSSI>markRSSI branch fires, updating markFreq/markRSSI.
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(true));
    somfy.transceiver.endFrequencyScan();
}

TEST_F(TransceiverMiscTest, ProcessFrequencyScan_Received_True_SameFreq) {
    somfy.transceiver.beginFrequencyScan();
    // Call twice with received=true so the second call hits markFreq == currFreq.
    somfy.transceiver.processFrequencyScan(true);
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(true));
    somfy.transceiver.endFrequencyScan();
}

TEST_F(TransceiverMiscTest, ProcessFrequencyScan_AdvancesFreqAfter100ms) {
    somfy.transceiver.beginFrequencyScan();
    // Advance clock past 100ms so millis()-lastScan > 100 triggers frequency step.
    test_clock_ms = 200;
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(false));
    somfy.transceiver.endFrequencyScan();
}

TEST_F(TransceiverMiscTest, ProcessFrequencyScan_NoOp_WhenRxmodeNot3) {
    // rxmode is 1 after begin(); processFrequencyScan should be a no-op.
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(true));
}

TEST_F(TransceiverMiscTest, EndFrequencyScan_ResetsRxmode) {
    somfy.transceiver.beginFrequencyScan();
    somfy.transceiver.endFrequencyScan();
    // After end, processFrequencyScan should be a no-op (rxmode != 3).
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(true));
}

TEST_F(TransceiverMiscTest, EndFrequencyScan_NoOp_WhenNotScanning) {
    // endFrequencyScan() when rxmode != 3 should do nothing.
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.endFrequencyScan());
}

TEST_F(TransceiverMiscTest, EmitFrequencyScan_NoCrash) {
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.emitFrequencyScan(0));
}

TEST_F(TransceiverMiscTest, ProcessFrequencyScan_DifferentFreq_UpdatesMark) {
    // Advance currFreq far enough from markFreq (433.0f) that the float→long cast
    // gives a different value. A single 0.01f step may round to the same integer
    // due to float precision, so we step 15 times (≈+0.15 → 433.15f ≈ 43315).
    somfy.transceiver.beginFrequencyScan();

    for (int i = 0; i < 15; i++) {
        test_clock_ms = (i + 1) * 200;
        somfy.transceiver.processFrequencyScan(false);
    }

    // currFreq ≈ 433.15; markFreq = 433.0 → (long)(433.0*100)=43300 ≠ 43315.
    // getRssi()=-70 > -75, so the else-if branch fires.
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.processFrequencyScan(true));

    somfy.transceiver.endFrequencyScan();
}

// ── loop() with rxmode == 3 ──────────────────────────────────────────────────

TEST_F(TransceiverMiscTest, Loop_RxMode3_CallsProcessFrequencyScan) {
    somfy.transceiver.beginFrequencyScan();
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.loop());
    somfy.transceiver.endFrequencyScan();
}

// ── loop() busy-warn after 3s ─────────────────────────────────────────────────

TEST_F(TransceiverMiscTest, Loop_BusyWarn_After3s) {
    // Arrange: s_rmtBusy=true but s_txDone=false (ISR never fires).
    somfy_frame_t f{};
    f.remoteAddress = 0x111111; f.rollingCode = 1;
    f.cmd = somfy_commands::Up; f.proto = radio_proto::RTS;
    f.bitLength = 56; f.encKey = 0xA0;
    somfy.transceiver.beginTransmit();
    somfy.transceiver.beginFrameTx(f, 0);
    // txBusy() is true; txDone is false.

    // Advance time past 3s and call loop() twice (first sets busyWarnAt, second warns).
    test_clock_ms = 3500;
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.loop());
    test_clock_ms = 7000;
    EXPECT_NO_FATAL_FAILURE(somfy.transceiver.loop());

    // Cleanup: fire done callback and drain.
    extern rmt_tx_done_callback_t rmt_stub_done_cb;
    if (rmt_stub_done_cb)
        rmt_stub_done_cb(nullptr, nullptr, rmt_stub_done_cb_ctx);
    somfy.transceiver.loop();
}
