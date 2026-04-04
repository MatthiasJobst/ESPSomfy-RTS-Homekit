// test_motionIntent.cpp — coverage for the three cross-layer MotionIntent flags:
//   settingPos, settingTiltPos, settingMyPos
//
// Three groups:
//  A. processFrame cancels all flags on an external (non-internal) command
//  B. processFrame preserves all flags on an internal command
//  C. moveToTarget / moveToTiltTarget / setMyPosition set the flags correctly

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

class MotionIntentTest : public ::testing::Test {
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
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;
        shade.targetSequencer.myPos          = -1.0f;
        shade.targetSequencer.myTiltPos      = -1.0f;

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        test_clock_ms = 0;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. processFrame(internal=false) clears all three flags (line 1436)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MotionIntentTest, ExternalFrame_ClearsSettingPos) {
    shade.setSettingPos(true);
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FALSE(shade.getSettingPos());
}

TEST_F(MotionIntentTest, ExternalFrame_ClearsSettingTiltPos) {
    shade.setSettingTiltPos(true);
    auto f = make_frame(somfy_commands::Down);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FALSE(shade.getSettingTiltPos());
}

TEST_F(MotionIntentTest, ExternalFrame_ClearsSettingMyPos) {
    shade.setSettingMyPos(true);
    auto f = make_frame(somfy_commands::My);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FALSE(shade.getSettingMyPos());
}

TEST_F(MotionIntentTest, ExternalFrame_ClearsAllThreeFlagsAtOnce) {
    shade.setSettingPos(true);
    shade.setSettingTiltPos(true);
    shade.setSettingMyPos(true);
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FALSE(shade.getSettingPos());
    EXPECT_FALSE(shade.getSettingTiltPos());
    EXPECT_FALSE(shade.getSettingMyPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// B. processFrame(internal=true) preserves all three flags (line 1436 branch)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MotionIntentTest, InternalFrame_PreservesSettingPos) {
    shade.setSettingPos(true);
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.getSettingPos());
}

TEST_F(MotionIntentTest, InternalFrame_PreservesSettingTiltPos) {
    shade.setSettingTiltPos(true);
    auto f = make_frame(somfy_commands::Down);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.getSettingTiltPos());
}

TEST_F(MotionIntentTest, InternalFrame_PreservesSettingMyPos) {
    shade.setSettingMyPos(true);
    auto f = make_frame(somfy_commands::My);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// C. moveToTarget sets settingPos (and optionally settingTiltPos)
// ══════════════════════════════════════════════════════════════════════════════

// Moving to a different position → settingPos=true
TEST_F(MotionIntentTest, MoveToTarget_DifferentPos_SetsSettingPos) {
    shade.currentPos = 50.0f;
    shade.moveToTarget(80.0f);
    EXPECT_TRUE(shade.getSettingPos());
}

// Moving to the same position → no command sent → settingPos stays false
TEST_F(MotionIntentTest, MoveToTarget_SamePos_NoTilt_DoesNotSetSettingPos) {
    shade.currentPos = 50.0f;
    shade.moveToTarget(50.0f);
    EXPECT_FALSE(shade.getSettingPos());
}

// Moving with a tilt argument → settingPos AND settingTiltPos both set
TEST_F(MotionIntentTest, MoveToTarget_WithTilt_SetsBothFlags) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(80.0f, 30.0f);
    EXPECT_TRUE(shade.getSettingPos());
    EXPECT_TRUE(shade.getSettingTiltPos());
}

// Moving to same pos but different tilt → only settingTiltPos set via tilt branch
TEST_F(MotionIntentTest, MoveToTarget_SamePosNewTilt_SetsSettingTiltPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 50.0f;
    // pos matches currentPos but tilt differs → cmd = Up/Down for tilt
    shade.moveToTarget(50.0f, 20.0f);
    EXPECT_TRUE(shade.getSettingPos());
    EXPECT_TRUE(shade.getSettingTiltPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// D. moveToTiltTarget sets settingTiltPos (line 1947)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MotionIntentTest, MoveToTiltTarget_DifferentTilt_SetsSettingTiltPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.currentPos     = shade.target;  // lift at target so tilt send is allowed
    shade.moveToTiltTarget(20.0f);
    EXPECT_TRUE(shade.getSettingTiltPos());
}

TEST_F(MotionIntentTest, MoveToTiltTarget_SameTilt_DoesNotSetSettingTiltPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.currentPos     = shade.target;
    shade.moveToTiltTarget(50.0f);  // cmd == My → condition on line 1947 is false
    EXPECT_FALSE(shade.getSettingTiltPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// E. setMyPosition sets settingMyPos when position differs from current
// ══════════════════════════════════════════════════════════════════════════════

// tiltType::none, requested pos != currentPos → settingMyPos=true
TEST_F(MotionIntentTest, SetMyPosition_NoTilt_DifferentPos_SetsSettingMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;  // idle
    shade.setMyPosition(80, -1);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// tiltType::none, requested pos == currentPos and myPos also matches → settingMyPos=true
// (the "clear my pos" path: sends My then sets settingMyPos=true)
TEST_F(MotionIntentTest, SetMyPosition_NoTilt_AtCurrentAndMyPos_SetsSettingMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;   // idle
    shade.targetSequencer.myPos      = 50.0f;   // same as currentPos → "clear my" branch
    shade.setMyPosition(50, -1);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// tiltType::none, pos == currentPos and myPos differs → stores new myPos, no movement intent
TEST_F(MotionIntentTest, SetMyPosition_NoTilt_AtCurrentNotMyPos_DoesNotSetSettingMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;   // idle
    shade.targetSequencer.myPos      = 30.0f;   // different from current
    shade.setMyPosition(50, -1);
    EXPECT_FALSE(shade.getSettingMyPos());
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 50.0f);  // stored
}

// Not idle → setMyPosition returns immediately without touching any flag
TEST_F(MotionIntentTest, SetMyPosition_WhileMoving_DoesNothing) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 80.0f;  // not idle (target != currentPos)
    shade.setDirection(1);
    shade.setMyPosition(80, -1);
    EXPECT_FALSE(shade.getSettingMyPos());
}
