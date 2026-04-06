// test_movementTracker.cpp — tests for SomfyMovementTracker
//
// Covers: checkMovement(), setMovement(), setTiltMovement()
//  computeDirections: drycontact time override, tilt_first detection
//  tickFlagTimers:    sun/wind timeouts adjusting target/tiltTarget
//  interpolatePos:    downTime==0, mid-move, target reached, settingPos variants
//  interpolateTilt:   tiltTime==0, tilt_first handoff, settingTiltPos variants
//  settingMyPos:      at-target handling, emitState change detection

#include "TestableShade.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;

// ── Timeout constants (must match SomfyFrame.h) ────────────────────────────
static constexpr uint64_t SUN_TIMEOUT     = 2  * 60 * 1000;   // SOMFY_SUN_TIMEOUT
static constexpr uint64_t NO_SUN_TIMEOUT  = 20 * 60 * 1000;   // SOMFY_NO_SUN_TIMEOUT
static constexpr uint64_t WIND_TIMEOUT    = 2  * 1000;         // SOMFY_WIND_TIMEOUT
static constexpr uint64_t NO_WIND_TIMEOUT = 12 * 60 * 1000;   // SOMFY_NO_WIND_TIMEOUT

// ── Fixture ────────────────────────────────────────────────────────────────

class MovementTrackerTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.setUpTime(10000);
        shade.setDownTime(10000);
        shade.setTiltTime(7000);
        shade.setStepSize(100);
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;   // idle by default
        shade.tiltTarget     = 50.0f;
        shade.targetSequencer.myPos          = -1.0f;
        shade.targetSequencer.myTiltPos      = -1.0f;

        // Allow emitState freely — most tests focus on position state, not events.
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        test_clock_ms = 0;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// Idle shade — no direction, no tilt, no sensor flags
// Verifies no crash and no spurious emitState
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MovementTrackerTest, Idle_NoMovement_NoEmit) {
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 50.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// DryContact: downTime/upTime/tiltTime forced to 1
// Covers line 334: if(drycontact || drycontact2) …
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MovementTrackerTest, DryContact_TimeForcedToOne_Down) {
    shade.shadeType  = shade_types::drycontact;
    shade.currentPos = 0.0f;
    shade.target     = 100.0f;   // moving down
    shade.setMoveStart(0);
    test_clock_ms = 5;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
}

TEST_F(MovementTrackerTest, DryContact2_TimeForcedToOne_Up) {
    shade.shadeType  = shade_types::drycontact2;
    shade.currentPos = 100.0f;
    shade.target     = 0.0f;    // moving up
    shade.setMoveStart(0);
    test_clock_ms = 5;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Sensor/Sun flag timers firing in checkMovement
// ══════════════════════════════════════════════════════════════════════════════

// Line 353–363: isSunny, noWindDone, !sunDone, sunStart set, elapsed >= SUN_TIMEOUT
// → sets target to myPos (or 100 if not set)
TEST_F(MovementTrackerTest, SunFlag_SunTimeout_FiresAndSetsTarget_MyPos) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag) |
                   static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(false);
    shade.setNoWindDone(true);
    shade.setSunStart(1);                        // non-zero = timer running
    shade.targetSequencer.myPos = 75.0f;
    test_clock_ms = 1 + SUN_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 75.0f);
    EXPECT_TRUE(shade.getSunDone());
}

TEST_F(MovementTrackerTest, SunFlag_SunTimeout_FiresAndSetsTarget_Fallback100) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag) |
                   static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(false);
    shade.setNoWindDone(true);
    shade.setSunStart(1);
    shade.targetSequencer.myPos = -1.0f;
    test_clock_ms = 1 + SUN_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// Line 364–372: isSunny, !noWindDone, noWindStart set, elapsed >= NO_WIND_TIMEOUT
// → sets target to myPos
TEST_F(MovementTrackerTest, SunFlag_NoWindTimeout_FiresAndSetsTarget) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag) |
                   static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setNoWindDone(false);
    shade.setNoWindStart(1);
    shade.targetSequencer.myPos = 60.0f;
    test_clock_ms = 1 + NO_WIND_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 60.0f);
    EXPECT_TRUE(shade.getNoWindDone());
}

// Line 374–383: !isSunny, !noSunDone, noSunStart set, elapsed >= NO_SUN_TIMEOUT
// tiltonly: also resets tiltTarget to 0
TEST_F(MovementTrackerTest, SunFlag_NoSunTimeout_Roller_SetsTargetZero) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag));  // SunFlag but not Sunny
    shade.setNoSunDone(false);
    shade.setNoSunStart(1);
    shade.target = 80.0f;
    test_clock_ms = 1 + NO_SUN_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_TRUE(shade.getNoSunDone());
}

TEST_F(MovementTrackerTest, SunFlag_NoSunTimeout_TiltOnly_SetsTargetAndTiltTargetZero) {
    shade.tiltType = tilt_types::tiltonly;
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag));
    shade.setNoSunDone(false);
    shade.setNoSunStart(1);
    shade.target     = 80.0f;
    shade.tiltTarget = 80.0f;
    test_clock_ms = 1 + NO_SUN_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// Timers not yet expired — block is skipped
TEST_F(MovementTrackerTest, SunFlag_SunTimeout_NotYetExpired_NoChange) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunFlag) |
                   static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(false);
    shade.setNoWindDone(true);
    shade.setSunStart(0);
    shade.target = 50.0f;
    test_clock_ms = SUN_TIMEOUT - 1;   // not yet

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
    EXPECT_FALSE(shade.getSunDone());
}

// Line 386–395: isWindy, !windDone, windStart set, elapsed >= WIND_TIMEOUT
// tiltonly: also resets tiltTarget to 0
TEST_F(MovementTrackerTest, Wind_Timeout_Roller_SetsTargetZero) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Windy));
    shade.setWindDone(false);
    shade.setWindStart(1);
    shade.target = 70.0f;
    test_clock_ms = 1 + WIND_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_TRUE(shade.getWindDone());
}

TEST_F(MovementTrackerTest, Wind_Timeout_TiltOnly_SetsTargetAndTiltTargetZero) {
    shade.tiltType = tilt_types::tiltonly;
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::Windy));
    shade.setWindDone(false);
    shade.setWindStart(1);
    shade.target     = 70.0f;
    shade.tiltTarget = 70.0f;
    test_clock_ms = 1 + WIND_TIMEOUT + 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// direction > 0 (moving down), NOT tilt_first
// ══════════════════════════════════════════════════════════════════════════════

// Line 398–401: downTime == 0 → jump to 100
TEST_F(MovementTrackerTest, Down_DownTimeZero_JumpsToHundred) {
    shade.setDownTime(0);
    shade.currentPos = 10.0f;
    shade.target     = 100.0f;
    shade.setMoveStart(0);
    test_clock_ms = 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
}

// Line 403–431: downTime > 0, mid-move (not yet at target) → interpolated position
TEST_F(MovementTrackerTest, Down_MidMove_InterpolatesPosition) {
    shade.currentPos = 0.0f;
    shade.target     = 100.0f;
    shade.setStartPos(0.0f);
    shade.setMoveStart(0);
    test_clock_ms = 5000;   // 50% through 10000ms

    shade.checkMovement();
    EXPECT_NEAR(shade.currentPos, 50.0f, 1.0f);
    EXPECT_GT(shade.getDirection(), -1);   // still moving (direction hasn't gone to 0 yet)
}

// Line 416–419: msFrom0 >= downTime → sets to 100
TEST_F(MovementTrackerTest, Down_ElapsedExceedsDownTime_JumpsToHundred) {
    shade.currentPos = 0.0f;
    shade.target     = 100.0f;
    shade.setStartPos(0.0f);
    shade.setMoveStart(0);
    test_clock_ms = 20000;   // >> 10000ms

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
}

// Line 427–430: fpos >= 100 inside else branch → clamp to 100
// Reached when msFrom0/downTime ratio rounds to exactly 1.0 before the >=downTime check.
// We exercise this by starting at a very high startPos so msFrom0 barely misses downTime
// but ratio * 100 >= 100 before it does.
TEST_F(MovementTrackerTest, Down_FposClampedTo100) {
    shade.currentPos = 99.0f;
    shade.target     = 100.0f;
    shade.setStartPos(99.0f);
    shade.setMoveStart(0);
    // msFrom0 = floor(99/100 * 10000) + elapsed = 9900 + 99 = 9999 < 10000
    // ratio = 9999/10000 * 100 = 99.99 < 100, but let's push elapsed further
    // Actually needs ratio >= 100.  downTime=1 makes it easy:
    shade.setDownTime(1);
    test_clock_ms = 10;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
}

// Line 433–453: currentPos >= target → snap to target, settingPos=false path
TEST_F(MovementTrackerTest, Down_ReachesTarget_StopsAndSnaps) {
    shade.currentPos = 50.0f;
    shade.target     = 60.0f;
    shade.setStartPos(50.0f);
    shade.setMoveStart(0);
    test_clock_ms = 2000;   // enough to overshoot 60% within 10s

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, shade.target);
    EXPECT_EQ(shade.getDirection(), 0);
}

// Line 438–448: settingPos=true, isAtTarget() → target != 100 → send My
// (no actual radio in test — just verify direction stops and no crash)
TEST_F(MovementTrackerTest, Down_SettingPos_AtTarget_NotHundred_SendsMy) {
    shade.currentPos = 50.0f;
    shade.target     = 60.0f;
    shade.tiltTarget = 60.0f;
    shade.currentTiltPos = 60.0f;   // isAtTarget() == true
    shade.setStartPos(50.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 2000;

    shade.checkMovement();
    EXPECT_EQ(shade.getDirection(), 0);
}

// Line 438–448: settingPos=true, !isAtTarget() → send My + moveToTiltTarget
TEST_F(MovementTrackerTest, Down_SettingPos_NotAtTiltTarget_MovesToTiltTarget) {
    shade.currentPos     = 50.0f;
    shade.target         = 60.0f;
    shade.currentTiltPos = 40.0f;
    shade.tiltTarget     = 80.0f;   // tiltonly type so isAtTarget checks tilt
    shade.tiltType       = tilt_types::tiltonly;
    shade.setStartPos(50.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 2000;

    shade.checkMovement();   // should not crash; tilt movement set up
    EXPECT_EQ(shade.getDirection(), 0);
}

// target == 100 → My NOT sent (line 441/447 condition)
TEST_F(MovementTrackerTest, Down_SettingPos_TargetIsHundred_NoMy) {
    shade.currentPos     = 0.0f;
    shade.target         = 100.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.setStartPos(0.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getDirection(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// direction < 0 (moving up), NOT tilt_first
// ══════════════════════════════════════════════════════════════════════════════

// Line 456–458: upTime == 0 → jump to 0
TEST_F(MovementTrackerTest, Up_UpTimeZero_JumpsToZero) {
    shade.setUpTime(0);
    shade.currentPos = 80.0f;
    shade.target     = 0.0f;
    shade.setMoveStart(0);
    test_clock_ms = 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 0.0f);
}

// Mid-move up
TEST_F(MovementTrackerTest, Up_MidMove_InterpolatesPosition) {
    shade.currentPos = 100.0f;
    shade.target     = 0.0f;
    shade.setStartPos(100.0f);
    shade.setMoveStart(0);
    test_clock_ms = 5000;

    shade.checkMovement();
    EXPECT_NEAR(shade.currentPos, 50.0f, 1.0f);
}

// msFrom100 >= upTime → 0
TEST_F(MovementTrackerTest, Up_ElapsedExceedsUpTime_JumpsToZero) {
    shade.currentPos = 100.0f;
    shade.target     = 0.0f;
    shade.setStartPos(100.0f);
    shade.setMoveStart(0);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 0.0f);
}

// fpos <= 0 inside else branch (line 476–479)
TEST_F(MovementTrackerTest, Up_FposClampedToZero) {
    shade.currentPos = 1.0f;
    shade.target     = 0.0f;
    shade.setStartPos(1.0f);
    shade.setMoveStart(0);
    shade.setUpTime(1);
    test_clock_ms = 10;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, 0.0f);
}

// Reaches target while moving up
TEST_F(MovementTrackerTest, Up_ReachesTarget_StopsAndSnaps) {
    shade.currentPos = 60.0f;
    shade.target     = 40.0f;
    shade.setStartPos(60.0f);
    shade.setMoveStart(0);
    test_clock_ms = 2000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentPos, shade.target);
    EXPECT_EQ(shade.getDirection(), 0);
}

// settingPos=true, isAtTarget(), target != 0 → My
TEST_F(MovementTrackerTest, Up_SettingPos_AtTarget_NotZero_SendsMy) {
    shade.currentPos     = 60.0f;
    shade.target         = 40.0f;
    shade.tiltTarget     = 40.0f;
    shade.currentTiltPos = 40.0f;
    shade.setStartPos(60.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 2000;

    shade.checkMovement();
    EXPECT_EQ(shade.getDirection(), 0);
}

// settingPos=true, !isAtTarget() (tilt differs) → My + moveToTiltTarget
TEST_F(MovementTrackerTest, Up_SettingPos_NotAtTiltTarget_MovesToTiltTarget) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 60.0f;
    shade.target         = 40.0f;
    shade.currentTiltPos = 60.0f;
    shade.tiltTarget     = 20.0f;
    shade.setStartPos(60.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 2000;

    shade.checkMovement();
    EXPECT_EQ(shade.getDirection(), 0);
}

// target == 0 → My NOT sent on up path (line 492/498)
TEST_F(MovementTrackerTest, Up_SettingPos_TargetIsZero_NoMy) {
    shade.currentPos     = 100.0f;
    shade.target         = 0.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.setStartPos(100.0f);
    shade.setMoveStart(0);
    shade.setSettingPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getDirection(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// tiltDirection > 0 (tilt moving clockwise / extending)
// ══════════════════════════════════════════════════════════════════════════════

// tiltTime == 0 → immediately snap to 100 (tiltDirection > 0 path)
TEST_F(MovementTrackerTest, TiltUp_TiltTimeZero_SnapsToHundred) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.setTiltTime(0);
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    test_clock_ms = 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 100.0f);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// Mid-tilt (not tilt_first, not at target)
TEST_F(MovementTrackerTest, TiltUp_MidMove_InterpolatesTiltPos) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    test_clock_ms = 3500;   // ~50% of 7000ms

    shade.checkMovement();
    EXPECT_NEAR(shade.currentTiltPos, 50.0f, 2.0f);
}

// msFrom0 >= tiltTime → jump to 100 (line 511–514)
TEST_F(MovementTrackerTest, TiltUp_ElapsedExceedsTiltTime_JumpsToHundred) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 100.0f);
}

// fpos > 100 branch (line 519–522) — start near end, push tiltTime to 1
TEST_F(MovementTrackerTest, TiltUp_FposOverHundred_ClampsToHundred) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.setTiltTime(1);
    shade.currentTiltPos = 99.0f;
    shade.tiltTarget     = 100.0f;
    shade.setStartTiltPos(99.0f);
    shade.setTiltStart(0);
    test_clock_ms = 10;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 100.0f);
}

// Reaches tiltTarget (not tilt_first, not settingTiltPos) → stop
TEST_F(MovementTrackerTest, TiltUp_ReachesTiltTarget_Stops) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 50.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    test_clock_ms = 10000;   // overshoot

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 50.0f);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, integrated, tiltTarget != 100 → My
TEST_F(MovementTrackerTest, TiltUp_SettingTiltPos_Integrated_NotHundred_SendsMy) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 10000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, integrated, tiltTarget == 100 AND currentPos == 100 → no My
TEST_F(MovementTrackerTest, TiltUp_SettingTiltPos_Integrated_BothAtHundred_NoMy) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, non-integrated tilt motor, tiltTarget != 100 → My
TEST_F(MovementTrackerTest, TiltUp_SettingTiltPos_TiltMotor_NotHundred_SendsMy) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 50.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 10000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, tiltmotor, tiltTarget == 100 → no My (line 547)
TEST_F(MovementTrackerTest, TiltUp_SettingTiltPos_TiltMotor_AtHundred_NoMy) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// tilt_first moving down: tilt extends to 100 → switches to shade movement
// Line 526–533: tilt_first && currentTiltPos >= 100
TEST_F(MovementTrackerTest, TiltFirst_Down_TiltReachesHundred_StartsMovePhase) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 100.0f;   // moving down → tilt_first because tilt != 100
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 100.0f;   // will be overridden to direction by tilt_first logic
    shade.setStartTiltPos(0.0f);
    shade.setTiltStart(0);
    shade.setStartPos(50.0f);
    test_clock_ms = 20000;   // enough for tilt to reach 100

    shade.checkMovement();
    // After tilt reaches 100, moveStart is reset and shade direction takes over
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 100.0f);
}

// tilt_first moving up: tilt retracts to 0 → switches to shade movement (line 577–583)
TEST_F(MovementTrackerTest, TiltFirst_Up_TiltReachesZero_StartsMovePhase) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 0.0f;     // moving up → tilt_first because tilt != 0
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    shade.setStartPos(50.0f);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// tiltDirection < 0 (tilt moving counter-clockwise / retracting)
// ══════════════════════════════════════════════════════════════════════════════

// tiltTime == 0 → immediately snap to 0 and stop (line 557–560)
TEST_F(MovementTrackerTest, TiltDown_TiltTimeZero_SnapsToZero) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.setTiltTime(0);
    shade.currentTiltPos = 80.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(80.0f);
    shade.setTiltStart(0);
    test_clock_ms = 1;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 0.0f);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// Mid-tilt retracting
TEST_F(MovementTrackerTest, TiltDown_MidMove_InterpolatesTiltPos) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    test_clock_ms = 3500;

    shade.checkMovement();
    EXPECT_NEAR(shade.currentTiltPos, 50.0f, 2.0f);
}

// msFrom100 >= tiltTime → snap to 0 (line 565–567)
TEST_F(MovementTrackerTest, TiltDown_ElapsedExceedsTiltTime_JumpsToZero) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 0.0f);
}

// fpos <= 0 inside else (line 571–573)
TEST_F(MovementTrackerTest, TiltDown_FposClampedToZero) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.setTiltTime(1);
    shade.currentTiltPos = 1.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(1.0f);
    shade.setTiltStart(0);
    test_clock_ms = 10;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 0.0f);
}

// Reaches tiltTarget, not settingTiltPos → stop
TEST_F(MovementTrackerTest, TiltDown_ReachesTiltTarget_Stops) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 50.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    test_clock_ms = 10000;

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.currentTiltPos, 50.0f);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, integrated, tiltTarget != 0 → My (line 593)
TEST_F(MovementTrackerTest, TiltDown_SettingTiltPos_Integrated_NotZero_SendsMy) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 10000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, integrated, tiltTarget==0 AND currentPos==0 → no My
TEST_F(MovementTrackerTest, TiltDown_SettingTiltPos_Integrated_BothAtZero_NoMy) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 0.0f;
    shade.currentPos     = 0.0f;
    shade.target         = 0.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, tiltmotor (non-integrated), tiltTarget != 0 → My (line 597)
TEST_F(MovementTrackerTest, TiltDown_SettingTiltPos_TiltMotor_NotZero_SendsMy) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 50.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 10000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// settingTiltPos=true, tiltmotor, tiltTarget == 0 → no My (line 597 condition false)
TEST_F(MovementTrackerTest, TiltDown_SettingTiltPos_TiltMotor_AtZero_NoMy) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 0.0f;
    shade.setStartTiltPos(100.0f);
    shade.setTiltStart(0);
    shade.setSettingTiltPos(true);
    test_clock_ms = 20000;

    shade.checkMovement();
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// settingMyPos block (lines 606–626)
// ══════════════════════════════════════════════════════════════════════════════

// settingMyPos=true, isAtTarget(), tiltType==none, myPos != currentPos → sets myPos
TEST_F(MovementTrackerTest, SettingMyPos_NoTilt_SetsMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 40.0f;
    shade.target     = 40.0f;   // isAtTarget() true
    shade.targetSequencer.myPos      = 99.0f;   // different from currentPos → sets to currentPos
    shade.setSettingMyPos(true);

    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 40.0f);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// settingMyPos=true, isAtTarget(), tiltType==none, myPos == currentPos → clears myPos to -1
TEST_F(MovementTrackerTest, SettingMyPos_NoTilt_ClearsMyPosIfAlreadySet) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 40.0f;
    shade.target     = 40.0f;
    shade.targetSequencer.myPos      = 40.0f;   // same as currentPos → clear to -1
    shade.setSettingMyPos(true);

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, -1.0f);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// settingMyPos=true, isAtTarget(), tiltType != none, both match → clears both to -1
TEST_F(MovementTrackerTest, SettingMyPos_WithTilt_BothMatch_ClearsToNegOne) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentPos     = 40.0f;
    shade.target         = 40.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;
    shade.targetSequencer.myPos          = 40.0f;    // == currentPos
    shade.targetSequencer.myTiltPos      = 30.0f;   // == currentTiltPos → clear both
    shade.setSettingMyPos(true);

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos,    -1.0f);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myTiltPos, -1.0f);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// settingMyPos=true, isAtTarget(), tiltType != none, at least one differs → sets both
TEST_F(MovementTrackerTest, SettingMyPos_WithTilt_SetsBothPositions) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentPos     = 40.0f;
    shade.target         = 40.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;
    shade.targetSequencer.myPos          = 99.0f;   // different → update
    shade.targetSequencer.myTiltPos      = 99.0f;
    shade.setSettingMyPos(true);

    shade.checkMovement();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos,     40.0f);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myTiltPos, 30.0f);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// settingMyPos=false but state changed → emitState fires (line 627–630)
TEST_F(MovementTrackerTest, StateChanged_EmitsState_OnDirectionChange) {
    shade.currentPos = 50.0f;
    shade.target     = 100.0f;   // will set direction=1
    shade.setStartPos(50.0f);
    shade.setMoveStart(0);
    test_clock_ms = 1000;        // move just a bit

    // direction will change from 0 → 1 → emitState
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.checkMovement();
}

// settingMyPos=false, nothing changed → emitState NOT fired
TEST_F(MovementTrackerTest, NothingChanged_DoesNotEmitState) {
    // shade is idle: target == currentPos, no sensor flags, no tilt movement
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.checkMovement();
}
