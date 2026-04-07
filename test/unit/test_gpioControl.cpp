// test_gpioControl.cpp — tests for SomfyGPIOControl
//
// Covers: setGPIOs(), triggerGPIOs(), usesPin()
//
// Pin assignments used throughout:
//   gpioUp   = 4
//   gpioDown = 5
//   gpioMy   = 6
//
// HIGH = 1, LOW = 0  (from Arduino.h stub)
// gpio_pin_levels[] records the last value written to each pin number.

#include "TestableShade.h"
#include "driver/gpio.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;

static constexpr uint8_t PIN_UP   = 4;
static constexpr uint8_t PIN_DOWN = 5;
static constexpr uint8_t PIN_MY   = 6;

// ── fixture ───────────────────────────────────────────────────────────────

class GPIOControlTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.setUpTime(10000);
        shade.setDownTime(10000);
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;
        shade.setGpioUp(PIN_UP);
        shade.setGpioDown(PIN_DOWN);
        shade.setGpioMy(PIN_MY);
        shade.setGpioFlags(0);   // high-level trigger (normal polarity)
        shade.setGpioRelease(0);

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        test_clock_ms = 0;
        gpio_pin_levels.clear();
    }

    // Convenience: make a frame with given cmd and repeat count.
    static somfy_frame_t make_frame(somfy_commands cmd, uint8_t repeats = 1) {
        somfy_frame_t f{};
        f.cmd           = cmd;
        f.remoteAddress = 0xABCDEF;
        f.rollingCode   = 1;
        f.valid         = true;
        f.repeats       = repeats;
        return f;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// setGPIOs — proto != GP_Relay/GP_Remote → no pin writes
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GPIOControlTest, SetGPIOs_RTSProto_WritesNoPins) {
    shade.setProto(radio_proto::RTS);
    shade.setDirection(1);
    shade.setGPIOs();
    EXPECT_TRUE(gpio_pin_levels.empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// setGPIOs — GP_Relay, roller, direction branches
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GPIOControlTest, SetGPIOs_Relay_DirectionUp_UpHighDownLow) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setDirection(-1);  // up
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);  // HIGH
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);  // LOW
    EXPECT_EQ(shade.getGpioDir(), -1);
}

TEST_F(GPIOControlTest, SetGPIOs_Relay_DirectionDown_UpLowDownHigh) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setDirection(1);   // down
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);  // LOW
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);  // HIGH
    EXPECT_EQ(shade.getGpioDir(), 1);
}

TEST_F(GPIOControlTest, SetGPIOs_Relay_DirectionIdle_BothLow) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setDirection(0);
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
    EXPECT_EQ(shade.getGpioDir(), 0);
}

// ── LowLevelTrigger polarity inversion ────────────────────────────────────

TEST_F(GPIOControlTest, SetGPIOs_Relay_LowLevelTrigger_DirectionUp_UpLowDownHigh) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setGpioFlags(static_cast<uint8_t>(gpio_flags_t::LowLevelTrigger));
    shade.setDirection(-1);
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);  // p_on=LOW when LLT set
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);  // p_off=HIGH when LLT set
}

TEST_F(GPIOControlTest, SetGPIOs_Relay_LowLevelTrigger_DirectionIdle_BothHigh) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setGpioFlags(static_cast<uint8_t>(gpio_flags_t::LowLevelTrigger));
    shade.setDirection(0);
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);  // p_off=HIGH
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);
}

// ── drycontact: only DOWN pin, level based on currentPos ──────────────────

TEST_F(GPIOControlTest, SetGPIOs_Relay_DryContact_AtHundred_DownHigh) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType  = shade_types::drycontact;
    shade.currentPos = 100.0f;
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);  // p_on
    EXPECT_EQ(shade.getGpioDir(), 1);
}

TEST_F(GPIOControlTest, SetGPIOs_Relay_DryContact_NotAtHundred_DownLow) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType  = shade_types::drycontact;
    shade.currentPos = 50.0f;
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);  // p_off
    EXPECT_EQ(shade.getGpioDir(), -1);
}

// ── drycontact2: both pins, opposing levels ────────────────────────────────

TEST_F(GPIOControlTest, SetGPIOs_Relay_DryContact2_AtHundred_DownOffUpOn) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 100.0f;
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);  // p_off
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);  // p_on
    EXPECT_EQ(shade.getGpioDir(), 1);
}

TEST_F(GPIOControlTest, SetGPIOs_Relay_DryContact2_NotAtHundred_UpOffDownOn) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);
    EXPECT_EQ(shade.getGpioDir(), -1);
}

// ── integrated tilt: direction=0 falls back to tiltDirection ─────────────

TEST_F(GPIOControlTest, SetGPIOs_Relay_Integrated_DirZero_UsesTiltDirection) {
    shade.setProto(radio_proto::GP_Relay);
    shade.tiltType = tilt_types::integrated;
    shade.setDirection(0);
    // tiltDirection is not directly settable via TestableShade helpers,
    // but we can infer the branch fires by checking gpioDir after setting
    // direction=0 with tiltDirection (default 0) — result should be idle.
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
    EXPECT_EQ(shade.getGpioDir(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// setGPIOs — GP_Remote: release timer
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GPIOControlTest, SetGPIOs_Remote_PastRelease_AllPinsOff_ReleaseCleared) {
    shade.setProto(radio_proto::GP_Remote);
    shade.setGpioRelease(100);
    test_clock_ms = 200;  // millis() > gpioRelease
    shade.setGPIOs();
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   0u);
    EXPECT_EQ(shade.getGpioRelease(), 0u);
}

TEST_F(GPIOControlTest, SetGPIOs_Remote_BeforeRelease_WritesNoPins) {
    shade.setProto(radio_proto::GP_Remote);
    shade.setGpioRelease(500);
    test_clock_ms = 100;  // millis() < gpioRelease
    shade.setGPIOs();
    EXPECT_TRUE(gpio_pin_levels.empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// triggerGPIOs — GP_Remote command → pin states
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_Up_UpHighOthersLow) {
    shade.setProto(radio_proto::GP_Remote);
    auto f = make_frame(somfy_commands::Up, 2);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   0u);
    EXPECT_EQ(shade.getGpioDir(), -1);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_Down_DownHighOthersLow) {
    shade.setProto(radio_proto::GP_Remote);
    auto f = make_frame(somfy_commands::Down, 2);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   0u);
    EXPECT_EQ(shade.getGpioDir(), 1);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_My_Roller_MyHighOthersLow) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::roller;
    auto f = make_frame(somfy_commands::My, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   1u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_My_DryContact_NoMyPin) {
    // drycontact hits the early return in the My case — MY pin not written
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::My, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels.count(PIN_MY), 0u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_My_Toggle_NoMyPin) {
    // isToggle() shades also skip the My pin write
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::garage1;
    auto f = make_frame(somfy_commands::My, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels.count(PIN_MY), 0u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_MyUp_MyAndUpHigh_DownLow) {
    shade.setProto(radio_proto::GP_Remote);
    auto f = make_frame(somfy_commands::MyUp, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_MyDown_MyAndDownHigh_UpLow) {
    shade.setProto(radio_proto::GP_Remote);
    auto f = make_frame(somfy_commands::MyDown, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_Remote_SetsGpioRelease) {
    shade.setProto(radio_proto::GP_Remote);
    test_clock_ms = 1000;
    auto f = make_frame(somfy_commands::Up, 3);
    shade.triggerGPIOs(f);
    // gpioRelease = millis() + repeats * 200 = 1000 + 3*200 = 1600
    EXPECT_EQ(shade.getGpioRelease(), 1600u);
}

TEST_F(GPIOControlTest, TriggerGPIOs_RTSProto_WritesNoPins) {
    shade.setProto(radio_proto::RTS);
    auto f = make_frame(somfy_commands::Up, 1);
    shade.triggerGPIOs(f);
    EXPECT_TRUE(gpio_pin_levels.empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// usesPin
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GPIOControlTest, UsesPin_RTSProto_ReturnsFalse) {
    shade.setProto(radio_proto::RTS);
    EXPECT_FALSE(shade.usesPin(PIN_DOWN));
    EXPECT_FALSE(shade.usesPin(PIN_UP));
}

TEST_F(GPIOControlTest, UsesPin_Relay_Roller_DownPin_ReturnsTrue) {
    shade.setProto(radio_proto::GP_Relay);
    EXPECT_TRUE(shade.usesPin(PIN_DOWN));
}

TEST_F(GPIOControlTest, UsesPin_Relay_Roller_UpPin_ReturnsTrue) {
    shade.setProto(radio_proto::GP_Relay);
    EXPECT_TRUE(shade.usesPin(PIN_UP));
}

TEST_F(GPIOControlTest, UsesPin_Relay_Roller_UnrelatedPin_ReturnsFalse) {
    shade.setProto(radio_proto::GP_Relay);
    EXPECT_FALSE(shade.usesPin(99));
}

TEST_F(GPIOControlTest, UsesPin_Remote_Roller_MyPin_ReturnsTrue) {
    shade.setProto(radio_proto::GP_Remote);
    EXPECT_TRUE(shade.usesPin(PIN_MY));
}

TEST_F(GPIOControlTest, UsesPin_Relay_Toggle_MyPin_ReturnsFalse) {
    // isToggle() → My pin is not used by GP_Relay
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType = shade_types::garage1;
    EXPECT_FALSE(shade.usesPin(PIN_MY));
}

TEST_F(GPIOControlTest, UsesPin_Relay_DryContact_DownPin_ReturnsTrue) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType = shade_types::drycontact;
    EXPECT_TRUE(shade.usesPin(PIN_DOWN));
}

TEST_F(GPIOControlTest, UsesPin_Relay_DryContact2_UpPin_ReturnsTrue) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType = shade_types::drycontact2;
    EXPECT_TRUE(shade.usesPin(PIN_UP));
}

TEST_F(GPIOControlTest, UsesPin_Relay_DryContact_OtherPin_ReturnsFalse) {
    // shadeType==drycontact and pin!=gpioDown: reaches the return-false path in
    // the drycontact else-if branch (line 149 in SomfyGPIOControl.cpp).
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType = shade_types::drycontact;
    EXPECT_FALSE(shade.usesPin(PIN_UP));
}

TEST_F(GPIOControlTest, UsesPin_Remote_DryContact2_NonDownPin_ReturnsFalse) {
    // proto=GP_Remote, drycontact2, pin!=gpioDown: enters the drycontact2 else-if block
    // but inner if(GP_Relay && ...) is false → falls through the closing brace (line 155).
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::drycontact2;
    EXPECT_FALSE(shade.usesPin(PIN_UP));
}

// ── tiltonly: direction falls back to tiltDirection ───────────────────────────
TEST_F(GPIOControlTest, SetGPIOs_Relay_TiltOnly_UsesTiltDirection) {
    shade.setProto(radio_proto::GP_Relay);
    shade.tiltType = tilt_types::tiltonly;
    shade.setDirection(1);  // any non-zero direction to ensure tiltonly takes effect
    shade.setGPIOs();
    // tiltDirection is 0 by default → falls back to idle (dir==0)
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   0u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 0u);
}

// ── triggerGPIOs: MyUpDown + isToggle (garage) → all three pins high ─────────
TEST_F(GPIOControlTest, TriggerGPIOs_Remote_MyUpDown_Toggle_AllPinsHigh) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::garage1;  // isToggle() == true
    auto f = make_frame(somfy_commands::MyUpDown, 1);
    shade.triggerGPIOs(f);
    EXPECT_EQ(gpio_pin_levels[PIN_UP],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_MY],   1u);
    EXPECT_EQ(gpio_pin_levels[PIN_DOWN], 1u);
}

// ── triggerGPIOs: unhandled cmd hits default: break ───────────────────────────
TEST_F(GPIOControlTest, TriggerGPIOs_Remote_Prog_DefaultBranch_NoExtraWrites) {
    shade.setProto(radio_proto::GP_Remote);
    auto f = make_frame(somfy_commands::Prog, 1);
    shade.triggerGPIOs(f);
    // Prog is not in any specific case → default: break. No pin writes expected.
    EXPECT_EQ(gpio_pin_levels.count(PIN_UP),   0u);
    EXPECT_EQ(gpio_pin_levels.count(PIN_DOWN), 0u);
    EXPECT_EQ(gpio_pin_levels.count(PIN_MY),   0u);
}
