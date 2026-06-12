// test_transceiver_config.cpp — unit tests for transceiver_config_t:
//   load() / save()  — NVS round-trip; chip-model defaults; radioInit crash guard
//   fromJSON()       — all fields parsed from a JSON object
//   toJSON()         — execution coverage (JsonResponse is a no-op stub)
//   apply()          — CC1101 init path; radioInit guard; enableReceive side-effect

#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include "../../main/somfy/SomfyShadeController.h"
#include "../../main/somfy/SomfyTransceiver.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "Arduino.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include "../../main/compat/preferences.h"

extern SomfyShadeController somfy;
extern bool cc1101_init_ok;
extern bool gpio_intr_enabled;
extern void (*gpio_last_isr)(void *);
extern uint64_t test_clock_ms;

// ── Fixture ──────────────────────────────────────────────────────────────────

class TransceiverConfigTest : public ::testing::Test {
  protected:
    transceiver_config_t cfg;

    void SetUp() override
    {
        nvs_stub_reset_all();
        gpio_intr_enabled = true;
        gpio_last_isr = nullptr;
        cc1101_init_ok = true;
        test_clock_ms = 0;
        gpio_pin_levels.clear();

        // Start from a clean known state.
        cfg = transceiver_config_t{};
        somfy.transceiver.disableReceive();
    }

    void TearDown() override
    {
        somfy.transceiver.disableReceive();
        nvs_stub_reset_all();
    }
};

// ── save() / load() round-trip ────────────────────────────────────────────────

TEST_F(TransceiverConfigTest, SaveLoad_RoundTrip_AllFields)
{
    cfg.type = 80;
    cfg.TXPin = 4;
    cfg.RXPin = 5;
    cfg.SCKPin = 6;
    cfg.MOSIPin = 7;
    cfg.MISOPin = 8;
    cfg.CSNPin = 9;
    cfg.frequency = 433.92f;
    cfg.deviation = 20.0f;
    cfg.rxBandwidth = 200.0f;
    cfg.txPower = 0;
    cfg.enabled = true;
    cfg.noiseDetection = true;
    cfg.proto = radio_proto::RTW;

    cfg.save();

    transceiver_config_t loaded{};
    loaded.load();

    EXPECT_EQ(loaded.type, cfg.type);
    EXPECT_EQ(loaded.TXPin, cfg.TXPin);
    EXPECT_EQ(loaded.RXPin, cfg.RXPin);
    EXPECT_EQ(loaded.SCKPin, cfg.SCKPin);
    EXPECT_EQ(loaded.MOSIPin, cfg.MOSIPin);
    EXPECT_EQ(loaded.MISOPin, cfg.MISOPin);
    EXPECT_EQ(loaded.CSNPin, cfg.CSNPin);
    EXPECT_NEAR(loaded.frequency, cfg.frequency, 0.01f);
    EXPECT_NEAR(loaded.deviation, cfg.deviation, 0.01f);
    EXPECT_NEAR(loaded.rxBandwidth, cfg.rxBandwidth, 0.01f);
    EXPECT_EQ(loaded.txPower, cfg.txPower);
    EXPECT_EQ(loaded.enabled, cfg.enabled);
    EXPECT_EQ(loaded.noiseDetection, cfg.noiseDetection);
    EXPECT_EQ(loaded.proto, cfg.proto);
}

TEST_F(TransceiverConfigTest, Load_WithNoNvsData_UsesDefaults)
{
    // No prior save() — load() reads default values from the struct and NVS.
    // On the stub host, esp_chip_info() returns CHIP_ESP32; defaults are
    // TXPin=13, RXPin=12, SCKPin=18, MOSIPin=23, MISOPin=19, CSNPin=5.
    transceiver_config_t loaded{};
    loaded.load();

    EXPECT_EQ(loaded.TXPin, 13u);
    EXPECT_EQ(loaded.RXPin, 12u);
    EXPECT_EQ(loaded.SCKPin, 18u);
    EXPECT_FALSE(loaded.enabled);
}

TEST_F(TransceiverConfigTest, Save_AllFieldsPersistAcrossLoad)
{
    // Complementary check: a second save() with different values overwrites
    // the first, and load() picks up the new values.
    cfg.type = 56;
    cfg.TXPin = 13;
    cfg.enabled = false;
    cfg.save();

    cfg.type = 80;
    cfg.TXPin = 4;
    cfg.enabled = true;
    cfg.save();

    transceiver_config_t loaded{};
    loaded.load();
    EXPECT_EQ(loaded.type, 80u);
    EXPECT_EQ(loaded.TXPin, 4u);
    EXPECT_TRUE(loaded.enabled);
}

// ── fromJSON() ────────────────────────────────────────────────────────────────

TEST_F(TransceiverConfigTest, FromJSON_ParsesAllFields)
{
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = 80;
    obj["TXPin"] = 3;
    obj["RXPin"] = 4;
    obj["SCKPin"] = 5;
    obj["MOSIPin"] = 6;
    obj["MISOPin"] = 7;
    obj["CSNPin"] = 8;
    obj["frequency"] = 433.42f;
    obj["deviation"] = 47.6f;
    obj["rxBandwidth"] = 99.97f;
    obj["txPower"] = 10;
    obj["enabled"] = true;
    obj["noiseDetection"] = true;
    obj["proto"] = static_cast<uint8_t>(radio_proto::RTW);

    cfg.fromJSON(obj);

    EXPECT_EQ(cfg.type, 80u);
    EXPECT_EQ(cfg.TXPin, 3u);
    EXPECT_EQ(cfg.RXPin, 4u);
    EXPECT_EQ(cfg.SCKPin, 5u);
    EXPECT_EQ(cfg.MOSIPin, 6u);
    EXPECT_EQ(cfg.MISOPin, 7u);
    EXPECT_EQ(cfg.CSNPin, 8u);
    EXPECT_NEAR(cfg.frequency, 433.42f, 0.01f);
    EXPECT_NEAR(cfg.deviation, 47.6f, 0.01f);
    EXPECT_NEAR(cfg.rxBandwidth, 99.97f, 0.01f);
    EXPECT_EQ(cfg.txPower, 10);
    EXPECT_TRUE(cfg.enabled);
    EXPECT_TRUE(cfg.noiseDetection);
    EXPECT_EQ(cfg.proto, radio_proto::RTW);
}

TEST_F(TransceiverConfigTest, FromJSON_IgnoresMissingKeys)
{
    // Only a subset of keys present — other fields keep their struct defaults.
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["enabled"] = true;

    cfg.fromJSON(obj);

    EXPECT_TRUE(cfg.enabled);
    EXPECT_EQ(cfg.type, 56u); // default unchanged
}

// ── toJSON() ─────────────────────────────────────────────────────────────────

TEST_F(TransceiverConfigTest, ToJSON_ExecutesWithoutCrash)
{
    // JsonResponse is a no-op stub; this test verifies no crash or UB.
    JsonResponse json;
    cfg.toJSON(json);
}

// ── apply() ──────────────────────────────────────────────────────────────────

// ── apply() tests use somfy.transceiver.config directly because apply() calls
// somfy.transceiver.enableReceive(), which reads this->config.enabled from the
// SomfyTransceiver instance — not from a separate local transceiver_config_t.

TEST_F(TransceiverConfigTest, Apply_Disabled_DoesNotEnableReceive)
{
    auto &tcfg = somfy.transceiver.config;
    tcfg.enabled = false;
    tcfg.save(); // write radioInit=true so apply() proceeds past the guard
    tcfg.apply();

    EXPECT_EQ(gpio_last_isr, nullptr);
}

TEST_F(TransceiverConfigTest, Apply_Enabled_CC1101OK_EnablesReceive)
{
    auto &tcfg = somfy.transceiver.config;
    tcfg.enabled = true;
    tcfg.TXPin = 13;
    tcfg.RXPin = 12;
    tcfg.SCKPin = 18;
    tcfg.MOSIPin = 23;
    tcfg.MISOPin = 19;
    tcfg.CSNPin = 5;
    cc1101_init_ok = true;
    tcfg.save(); // write radioInit=true

    tcfg.apply();

    EXPECT_NE(gpio_last_isr, nullptr) << "enableReceive() should be called when enabled and CC1101 init succeeds";
}

TEST_F(TransceiverConfigTest, Apply_Enabled_CC1101Fail_DoesNotEnableReceive)
{
    auto &tcfg = somfy.transceiver.config;
    tcfg.enabled = true;
    tcfg.TXPin = 13;
    tcfg.RXPin = 12;
    cc1101_init_ok = false;
    tcfg.save();

    tcfg.apply();

    EXPECT_EQ(gpio_last_isr, nullptr) << "enableReceive() must not be called when CC1101 init fails";
}

TEST_F(TransceiverConfigTest, Apply_RadioInitGuard_AbortsEarlyWhenFalse)
{
    auto &tcfg = somfy.transceiver.config;
    tcfg.enabled = true;
    tcfg.save(); // writes radioInit=true

    // Simulate a previous crash: overwrite radioInit=false in NVS.
    Preferences p;
    p.begin("CC1101");
    p.putBool("radioInit", false);
    p.end();

    tcfg.apply();

    EXPECT_EQ(gpio_last_isr, nullptr) << "apply() should abort early when radioInit=false in NVS";
}

TEST_F(TransceiverConfigTest, Apply_SetsCSNPinHigh)
{
    auto &tcfg = somfy.transceiver.config;
    tcfg.enabled = true;
    tcfg.CSNPin = 5;
    tcfg.save();

    tcfg.apply();

    auto it = gpio_pin_levels.find(static_cast<int>(tcfg.CSNPin));
    ASSERT_NE(it, gpio_pin_levels.end());
    EXPECT_EQ(it->second, 1u) << "CSN pin should be driven HIGH in apply()";
}
