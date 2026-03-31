#include "TestableShade.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;

// ── helpers ────────────────────────────────────────────────────────────────

static somfy_frame_t make_frame(somfy_commands cmd, uint32_t addr = 0xABCDEF, uint16_t rc = 1) {
    somfy_frame_t f{};
    f.cmd           = cmd;
    f.remoteAddress = addr;
    f.rollingCode   = rc;
    f.valid         = true;
    return f;
}

// ── fixture ───────────────────────────────────────────────────────────────

class ProcessFrameTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType  = shade_types::roller;
        shade.tiltType   = tilt_types::none;
        shade.upTime     = 10000;
        shade.downTime   = 10000;
        shade.tiltTime   = 7000;
        shade.stepSize   = 100;
        shade.currentPos = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target     = 50.0f;
        shade.tiltTarget = 50.0f;

        // Silence unexpected calls to emitState — most tests only care about emitCommand.
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
    }
};

// ── guard: unrecognised remote is ignored ─────────────────────────────────

TEST_F(ProcessFrameTest, UnknownRemote_DoesNothing) {
    auto f = make_frame(somfy_commands::Down, /*addr=*/0x000001);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);  // unchanged
}

TEST_F(ProcessFrameTest, ShadeId255_DoesNothing) {
    shade.setShadeId(255);
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ── Up ────────────────────────────────────────────────────────────────────

TEST_F(ProcessFrameTest, Up_Roller_SetsTargetZero) {
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, Up_DryContact_DoesNotEmitCommand) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_TRUE(shade.lastFrame.processed);  // processFrame copies into lastFrame then marks it
}

// ── Down ──────────────────────────────────────────────────────────────────

TEST_F(ProcessFrameTest, Down_Roller_SetsTargetHundred) {
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, Down_DryContact_DoesNotEmitCommand) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, Down_DryContact2_TogglesPosition) {
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ── Stop ─────────────────────────────────────────────────────────────────

TEST_F(ProcessFrameTest, Stop_SetsTargetToCurrentPos) {
    shade.currentPos     = 42.0f;
    shade.currentTiltPos = 25.0f;
    auto f = make_frame(somfy_commands::Stop);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Stop, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(),     42.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 25.0f);
}

// ── My (internal path, idle shade) ───────────────────────────────────────

TEST_F(ProcessFrameTest, My_Internal_Idle_MovesToMyPos) {
    shade.myPos     = 30.0f;
    shade.myTiltPos = -1.0f;  // not set
    // Ensure shade is idle (target == currentPos)
    shade.currentPos = shade.target = 50.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/true);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(ProcessFrameTest, My_Internal_Idle_MyPoAndTiltPos_BothSet) {
    shade.myPos     = 30.0f;
    shade.myTiltPos = 10.0f;
    shade.currentPos = shade.target = 50.0f;
    shade.currentTiltPos = shade.tiltTarget = 50.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/true);
    EXPECT_FLOAT_EQ(shade.getTarget(),     30.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 10.0f);
}

// ── Toggle ────────────────────────────────────────────────────────────────

TEST_F(ProcessFrameTest, Toggle_WhenAtBottom_GoesUp) {
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, Toggle_WhenAtTop_GoesDown) {
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ── Prog/MyUp/MyDown — passthrough emit for non-drycontact ────────────────

TEST_F(ProcessFrameTest, Prog_EmitsCommand) {
    auto f = make_frame(somfy_commands::Prog);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Prog, _, _, _));
    shade.processFrame(f, /*internal=*/false);
}

TEST_F(ProcessFrameTest, Prog_DryContact_DoesNotEmit) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Prog);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ── Linked remote is accepted ─────────────────────────────────────────────

TEST_F(ProcessFrameTest, LinkedRemote_AcceptsFrame) {
    constexpr uint32_t linked_addr = 0x111111;
    shade.linkRemote(linked_addr, 0);
    auto f = make_frame(somfy_commands::Down, linked_addr);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Sensor
// ══════════════════════════════════════════════════════════════════════════════

// The Sensor rollingCode encodes flags: bits are shifted left 4 to align with
// somfy_flags_t values.  Sunny = 0x20, Windy = 0x10.
// rollingCode << 4 must set the flag bit, so:
//   Sunny bit (0x20): rollingCode needs bit 1 set  → rollingCode = 0x02
//   Windy bit (0x10): rollingCode needs bit 0 set  → rollingCode = 0x01

TEST_F(ProcessFrameTest, Sensor_SetsSunnyFlag) {
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    shade.processFrame(f);
    EXPECT_TRUE(shade.isSunny());
    EXPECT_FALSE(shade.isWindy());
}

TEST_F(ProcessFrameTest, Sensor_ClearsSunnyFlag) {
    // Pre-set sunny
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::Sunny));
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_FALSE(shade.isSunny());
}

TEST_F(ProcessFrameTest, Sensor_SetsWindyFlag_UpdatesWindLast) {
    test_clock_ms = 5000;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x01);
    shade.processFrame(f);
    EXPECT_TRUE(shade.isWindy());
}

TEST_F(ProcessFrameTest, Sensor_SunTransition_StartsTimer) {
    test_clock_ms = 1000;
    // Not sunny → sunny: sunStart should be set, sunDone = false
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    shade.processFrame(f);
    EXPECT_EQ(shade.getSunStart(), 1000u);
    EXPECT_FALSE(shade.getSunDone());
}

TEST_F(ProcessFrameTest, Sensor_SunClearTransition_StartsNoSunTimer) {
    test_clock_ms = 2000;
    // Pre-set sunny, then clear it
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::Sunny));
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_EQ(shade.getNoSunStart(), 2000u);
    EXPECT_FALSE(shade.getNoSunDone());
}

TEST_F(ProcessFrameTest, Sensor_WindTransition_StartsTimer) {
    test_clock_ms = 3000;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x01);
    shade.processFrame(f);
    EXPECT_EQ(shade.getWindStart(), 3000u);
    EXPECT_FALSE(shade.getWindDone());
}

TEST_F(ProcessFrameTest, Sensor_WindClearTransition_StartsNoWindTimer) {
    test_clock_ms = 4000;
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::Windy));
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_EQ(shade.getNoWindStart(), 4000u);
    EXPECT_FALSE(shade.getNoWindDone());
}

TEST_F(ProcessFrameTest, Sensor_DryContact_DoesNothing) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    // emitState must NOT be called (drycontact returns early)
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FALSE(shade.isSunny());
}

TEST_F(ProcessFrameTest, Sensor_DemoMode_SetsFlag) {
    // DemoMode bit = 0x04 in rollingCode (not shifted)
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x04);
    shade.processFrame(f);
    EXPECT_TRUE(shade.getFlags() & static_cast<uint8_t>(somfy_flags_t::DemoMode));
}

// ══════════════════════════════════════════════════════════════════════════════
// SunFlag
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, SunFlag_SetsSunFlagBit) {
    auto f = make_frame(somfy_commands::SunFlag);
    shade.processFrame(f);
    EXPECT_TRUE(shade.hasSunFlag());
}

TEST_F(ProcessFrameTest, SunFlag_BogusRollingCode_Ignored) {
    // rollingCode >= 0x8000 → bogus, should return without setting flag
    auto f = make_frame(somfy_commands::SunFlag, 0xABCDEF, /*rc=*/0x8000);
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(ProcessFrameTest, SunFlag_DryContact_Ignored) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::SunFlag);
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(ProcessFrameTest, SunFlag_SunnyAndNoWind_SunDone_MovesToMyPos) {
    // Sunny + no wind + sunDone → should move to myPos
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(true);
    shade.myPos = 75.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 75.0f);
}

TEST_F(ProcessFrameTest, SunFlag_SunnyAndNoWind_SunDone_NoMyPos_MovesToHundred) {
    // myPos unset (< 0) → fall back to 100
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(true);
    shade.myPos = -1.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, SunFlag_NotSunny_NoSunDone_MovesToZero) {
    // No sun + noSunDone → retract to 0
    shade.setFlags(0);  // clear Sunny
    shade.setNoSunDone(true);
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, SunFlag_WindyActive_DoesNotMoveTarget) {
    // Windy → skip target changes entirely
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Sunny) |
                   static_cast<uint8_t>(somfy_flags_t::Windy));
    shade.setSunDone(true);
    shade.myPos = 75.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);  // unchanged
}

// ══════════════════════════════════════════════════════════════════════════════
// Flag (clear sun flag)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Flag_ClearsSunFlagBit) {
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::SunFlag));
    auto f = make_frame(somfy_commands::Flag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Flag, _, _, _));
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(ProcessFrameTest, Flag_BogusRollingCode_Ignored) {
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::SunFlag));
    auto f = make_frame(somfy_commands::Flag, 0xABCDEF, /*rc=*/0x8001);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_TRUE(shade.hasSunFlag());  // not cleared
}

// ══════════════════════════════════════════════════════════════════════════════
// My — remote paths
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, My_Remote_Idle_SetsAwait) {
    // From a remote on idle shade → sets await (deferred decision)
    shade.currentPos = shade.target = 50.0f;
    test_clock_ms = 1000;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(ProcessFrameTest, My_Remote_Moving_Stops) {
    // From a remote while moving → stop at current position
    shade.currentPos = 30.0f;
    shade.target     = 80.0f;   // not at target → not idle
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(ProcessFrameTest, My_DryContact_Toggles_AtBottom) {
    shade.shadeType  = shade_types::drycontact;
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, My_DryContact_Toggles_AtTop) {
    shade.shadeType  = shade_types::drycontact;
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, My_DryContact2_Ignored) {
    shade.shadeType = shade_types::drycontact2;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down with tiltmotor — await path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Up_TiltMotor_Remote_SetsAwait) {
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(ProcessFrameTest, Down_TiltMotor_Remote_SetsAwait) {
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Down);
    // emitCommand is outside the tiltmotor branch, so it fires even when await is set.
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(ProcessFrameTest, Down_RecentWind_Suppressed) {
    // If windLast is within SOMFY_NO_WIND_REMOTE_TIMEOUT (30s), Down is ignored
    test_clock_ms = 10000;
    shade.setWindLast(9000);  // 1 second ago — within 30s timeout
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);  // unchanged
}

TEST_F(ProcessFrameTest, Down_OldWind_NotSuppressed) {
    test_clock_ms = 100000;
    shade.setWindLast(1000);  // 99 seconds ago — outside 30s timeout
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down with tiltonly
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Up_TiltOnly_Remote_SetsTiltTargetZero) {
    shade.tiltType = tilt_types::tiltonly;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, Down_TiltOnly_Remote_SetsTiltTargetHundred) {
    shade.tiltType   = tilt_types::tiltonly;
    shade.tiltTarget = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// StepUp / StepDown — roller shade
// ══════════════════════════════════════════════════════════════════════════════
// stepSize=100ms, upTime=10000ms → one step = 100/10000 * 100% = 1%

TEST_F(ProcessFrameTest, StepUp_Roller_DecreasesTarget) {
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_LT(shade.getTarget(), 50.0f);
}

TEST_F(ProcessFrameTest, StepDown_Roller_IncreasesTarget) {
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getTarget(), 50.0f);
}

TEST_F(ProcessFrameTest, StepUp_Roller_AtTop_DoesNothing) {
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, StepDown_Roller_AtBottom_DoesNothing) {
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, StepUp_DryContact_NoEmit) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::StepUp);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, StepUp_Repeat_SecondFrameIgnored) {
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _)).Times(1);
    shade.processFrame(f, /*internal=*/false);
    // Second identical frame — lastFrame.processed is now true
    shade.processFrame(f, /*internal=*/false);
}

// ══════════════════════════════════════════════════════════════════════════════
// StepUp / StepDown — tiltonly
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, StepUp_TiltOnly_DecreasesTiltTarget) {
    shade.tiltType    = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_LT(shade.getTiltTarget(), 50.0f);
}

TEST_F(ProcessFrameTest, StepDown_TiltOnly_IncreasesTiltTarget) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.target         = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getTiltTarget(), 50.0f);   // p_tiltTarget is called (bug fixed in refactor)
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f); // lift target unchanged
}

// ══════════════════════════════════════════════════════════════════════════════
// Favorite
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Favorite_MovesToMyPos) {
    shade.myPos     = 40.0f;
    shade.myTiltPos = -1.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 40.0f);
}

TEST_F(ProcessFrameTest, Favorite_MyPosAndTiltPos_BothSet) {
    shade.myPos     = 40.0f;
    shade.myTiltPos = 20.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(),     40.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 20.0f);
}

TEST_F(ProcessFrameTest, Favorite_Repeat_Ignored) {
    shade.myPos = 40.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _)).Times(1);
    shade.processFrame(f);
    shade.processFrame(f);  // repeat — lastFrame.processed is true
}

// ══════════════════════════════════════════════════════════════════════════════
// MyUp / MyDown / UpDown / MyUpDown passthrough
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, MyUp_EmitsCommand) {
    auto f = make_frame(somfy_commands::MyUp);
    EXPECT_CALL(shade, emitCommand(somfy_commands::MyUp, _, _, _));
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, MyDown_EmitsCommand) {
    auto f = make_frame(somfy_commands::MyDown);
    EXPECT_CALL(shade, emitCommand(somfy_commands::MyDown, _, _, _));
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, UpDown_EmitsCommand) {
    auto f = make_frame(somfy_commands::UpDown);
    EXPECT_CALL(shade, emitCommand(somfy_commands::UpDown, _, _, _));
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, MyUpDown_EmitsCommand) {
    auto f = make_frame(somfy_commands::MyUpDown);
    EXPECT_CALL(shade, emitCommand(somfy_commands::MyUpDown, _, _, _));
    shade.processFrame(f);
}

TEST_F(ProcessFrameTest, MyUp_DryContact_DoesNotEmit) {
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::MyUp);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Toggle (moving shade stops at current position)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Toggle_WhenMoving_StopsAtCurrentPos) {
    shade.currentPos = 30.0f;
    shade.target     = 80.0f;  // moving
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down DryContact2 — repeat suppression
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Up_DryContact2_AlreadyAtTop_NoTargetChange) {
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, Down_DryContact2_Repeat_Suppressed) {
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _)).Times(1);
    shade.processFrame(f, /*internal=*/false);
    shade.processFrame(f, /*internal=*/false);  // repeat — suppressed
}

// ══════════════════════════════════════════════════════════════════════════════
// SunFlag + tiltonly
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, SunFlag_TiltOnly_Sunny_SunDone_SetsTiltTarget) {
    shade.tiltType = tilt_types::tiltonly;
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(true);
    shade.myTiltPos = 60.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 60.0f);  // p_tiltTarget not p_target
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);       // target unchanged
}

TEST_F(ProcessFrameTest, SunFlag_TiltOnly_NotSunny_NoSunDone_SetsTiltTargetZero) {
    shade.tiltType = tilt_types::tiltonly;
    shade.setFlags(0);  // not sunny
    shade.setNoSunDone(true);
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);  // p_tiltTarget(0) not p_target(0)
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);      // target unchanged
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down tiltmotor — internal=true path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Up_TiltMotor_Internal_MarksProcessed_NoAwait) {
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Up);
    // internal=true takes the `else lastFrame.processed = true` branch
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.lastFrame.processed);
    EXPECT_EQ(shade.getLastFrameAwait(), 0u);
}

TEST_F(ProcessFrameTest, Down_TiltMotor_Internal_MarksProcessed_NoAwait) {
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Down);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.lastFrame.processed);
    EXPECT_EQ(shade.getLastFrameAwait(), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// My — toggle shade types (garage1/lgate1/cgate1/rgate1)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, My_Toggle_Moving_StopsAtCurrentPos) {
    shade.shadeType  = shade_types::garage1;
    shade.currentPos = 40.0f;
    shade.target     = 100.0f;  // moving → not idle
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 40.0f);
}

TEST_F(ProcessFrameTest, My_Toggle_AtBottom_GoesUp) {
    shade.shadeType  = shade_types::garage1;
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, My_Toggle_AtTop_GoesDown) {
    shade.shadeType  = shade_types::garage1;
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, My_Toggle_MidPosition_UsesLastMovement) {
    shade.shadeType   = shade_types::garage1;
    shade.currentPos  = 50.0f;
    shade.target      = 50.0f;
    shade.lastMovement = -1;  // was going up → should now go to 100
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// My — drycontact mid-position
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, My_DryContact_MidPos_LastMovementNeg1_GoesTo100) {
    // Code: p_target(lastMovement == -1 ? 100 : 0)
    // lastMovement = -1  →  target = 100
    shade.shadeType    = shade_types::drycontact;
    shade.currentPos   = 50.0f;
    shade.target       = 50.0f;
    shade.lastMovement = -1;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, My_DryContact_MidPos_LastMovementPos1_GoesTo0) {
    // Code: p_target(lastMovement == -1 ? 100 : 0)
    // lastMovement = 1  →  target = 0
    shade.shadeType    = shade_types::drycontact;
    shade.currentPos   = 50.0f;
    shade.target       = 50.0f;
    shade.lastMovement = 1;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// StepUp / StepDown — integrated tilt
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, StepUp_Integrated_BothAtTop_DoesNothing) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.currentPos     = 0.0f;
    shade.target         = 0.0f;
    shade.tiltTarget     = 0.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(),     0.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(ProcessFrameTest, StepUp_Integrated_TiltNotAtTop_MovesTilt) {
    // tiltPos > 0 → tilt moves up, lift stays
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentPos     = 30.0f;
    shade.target         = 30.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f);
    EXPECT_LT(shade.getTiltTarget(), 50.0f);   // tilt moved up
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f); // lift locked at currentPos
}

TEST_F(ProcessFrameTest, StepUp_Integrated_TiltAtTop_MovesLift) {
    // tiltPos == 0 → only lift moves
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 0.0f;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f);
    EXPECT_LT(shade.getTarget(), 50.0f);          // lift moved up
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f); // tilt stays
}

TEST_F(ProcessFrameTest, StepDown_Integrated_BothAtBottom_DoesNothing) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.tiltTarget     = 100.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(),     100.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, StepDown_Integrated_TiltNotAtBottom_MovesTilt) {
    // tiltPos < 100 → tilt moves down, lift stays
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentPos     = 30.0f;
    shade.target         = 30.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f);
    EXPECT_GT(shade.getTiltTarget(), 50.0f);   // tilt moved down
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f); // lift locked at currentPos
}

TEST_F(ProcessFrameTest, StepDown_Integrated_TiltAtBottom_MovesLift) {
    // tiltPos == 100 → only lift moves
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 100.0f;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f);
    EXPECT_GT(shade.getTarget(), 50.0f);             // lift moved down
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);  // tilt stays
}

// ══════════════════════════════════════════════════════════════════════════════
// Toggle — mid-position
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Toggle_MidPos_LastMovementUp_GoesDown) {
    shade.currentPos   = 50.0f;
    shade.target       = 50.0f;   // idle
    shade.lastMovement = -1;      // was going up → go to 100
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(ProcessFrameTest, Toggle_MidPos_LastMovementDown_GoesUp) {
    shade.currentPos   = 50.0f;
    shade.target       = 50.0f;
    shade.lastMovement = 1;  // was going down → go to 0
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Favorite — simMy path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ProcessFrameTest, Favorite_SimMy_CallsMoveToMyPosition) {
    // simMy() returns true when the SimMy flag is set
    shade.setFlags(shade.getFlags() | static_cast<uint8_t>(somfy_flags_t::SimMy));
    shade.myPos = 35.0f;
    // moveToMyPosition() calls p_target(myPos) internally when idle
    auto f = make_frame(somfy_commands::Favorite);
    // emitCommand is NOT called — moveToMyPosition sends its own RF frame
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 35.0f);
}
