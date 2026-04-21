// test_targetSequencer.cpp — tests for SomfyTargetSequencer
//
// Covers: moveToTarget(), moveToTiltTarget(), moveToMyPosition(), setMyPosition()
//  moveToTarget:      direction selection, target/tiltTarget set, guards (toggle, tiltonly, euromode)
//  moveToTiltTarget:  direction selection, tiltTarget set, send-gate conditions
//  moveToMyPosition:  idle guard, early-return guards, simMy and My paths
//  setMyPosition:     all three tiltType branches and their sub-cases
//
// motionState flag-setting is covered in test_commandProcessor.cpp.

#include "TestableShade.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::AnyNumber;
using ::testing::_;

// SETMY_REPEATS / TILT_REPEATS are defined in SomfyTransceiver.h
#include "SomfyTransceiver.h"

// ── fixture ───────────────────────────────────────────────────────────────────

class TargetSequencerTest : public ::testing::Test {
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
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;
        shade.targetSequencer.myPos          = -1.0f;
        shade.targetSequencer.myTiltPos      = -1.0f;
        shade.setDirection(0);

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    }

    somfy_commands lastCmd() { return shade.getLastFrame().cmd; }
    bool cmdWasSent()        { return shade.getLastFrame().valid; }
    void clearLastFrame()    { shade.getLastFrame(); /* frame is observable but not resettable —
                                 use getLastFrame().cmd to distinguish My (no-send) from Up/Down */ }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. moveToTarget
// ══════════════════════════════════════════════════════════════════════════════

// A1. pos > currentPos → Down command
TEST_F(TargetSequencerTest, MoveToTarget_PosHigher_SendsDown) {
    shade.currentPos = 30.0f;
    shade.moveToTarget(80.0f);
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// A2. pos < currentPos → Up command
TEST_F(TargetSequencerTest, MoveToTarget_PosLower_SendsUp) {
    shade.currentPos = 70.0f;
    shade.moveToTarget(20.0f);
    EXPECT_EQ(lastCmd(), somfy_commands::Up);
}

// A3. pos == currentPos, no tilt → no command sent (lastFrame.cmd still My from no-send)
TEST_F(TargetSequencerTest, MoveToTarget_SamePos_NoTilt_NoCommand) {
    shade.currentPos = 50.0f;
    // Reset rolling code so we can detect if sendCommand was called
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToTarget(50.0f);
    // No command sent → rolling code unchanged
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// A4. target set to requested pos
TEST_F(TargetSequencerTest, MoveToTarget_SetsTarget) {
    shade.currentPos = 20.0f;
    shade.moveToTarget(75.0f);
    EXPECT_FLOAT_EQ(shade.target, 75.0f);
}

// A5. tilt >= 0 → tiltTarget set
TEST_F(TargetSequencerTest, MoveToTarget_WithTilt_SetsTiltTarget) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 20.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(80.0f, 30.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 30.0f);
}

// A6. tilt not supplied (default -1) → tiltTarget NOT updated
TEST_F(TargetSequencerTest, MoveToTarget_NoTiltArg_TiltTargetUnchanged) {
    shade.tiltTarget     = 40.0f;
    shade.currentPos     = 20.0f;
    shade.moveToTarget(80.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 40.0f);
}

// A7. pos == currentPos but tilt differs → Up or Down for tilt, settingPos set
TEST_F(TargetSequencerTest, MoveToTarget_SamePosLowerTilt_SendsUp) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(50.0f, 20.0f);  // tilt < currentTiltPos → Up
    EXPECT_EQ(lastCmd(), somfy_commands::Up);
}

TEST_F(TargetSequencerTest, MoveToTarget_SamePosHigherTilt_SendsDown) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(50.0f, 80.0f);  // tilt > currentTiltPos → Down
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// A8. tiltType::tiltonly → forces pos=100, currentPos set to 100
TEST_F(TargetSequencerTest, MoveToTarget_TiltOnly_ForcesPos100) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;  // tiltonly: currentPos is always 100
    shade.currentTiltPos = 50.0f;
    shade.targetSequencer.myPos          = -1.0f;
    shade.moveToTarget(100.0f, 20.0f);  // tilt < currentTiltPos → Up
    EXPECT_EQ(lastCmd(), somfy_commands::Up);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// A8b. tiltType::tiltonly, tilt > currentTiltPos → Down command
TEST_F(TargetSequencerTest, MoveToTarget_TiltOnly_TiltHigher_SendsDown) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.currentTiltPos = 30.0f;
    shade.targetSequencer.myPos          = -1.0f;
    shade.moveToTarget(100.0f, 80.0f);  // tilt(80) > currentTiltPos(30) → Down
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// A9. Toggle shade (garage1) → sets target+currentPos directly, no RF command
TEST_F(TargetSequencerTest, MoveToTarget_ToggleShade_SetsPositionDirectly) {
    shade.shadeType  = shade_types::garage1;
    shade.currentPos = 0.0f;
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToTarget(100.0f);
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
    EXPECT_FLOAT_EQ(shade.target,     100.0f);
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);  // no sendCommand
}

// A10. euromode tiltType → uses TILT_REPEATS repeats
TEST_F(TargetSequencerTest, MoveToTarget_Euromode_UsesTiltRepeats) {
    shade.tiltType   = tilt_types::euromode;
    shade.currentPos = 20.0f;
    shade.moveToTarget(80.0f);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
}

// A11. Non-euromode roller → uses MOVE_REPEATS so the motor registers a
// sustained press (movement), not a tap (tilt nudge). After the ~1 s burst
// the transmitter is freed and the motor runs on its own to the endpoint.
TEST_F(TargetSequencerTest, MoveToTarget_Roller_UsesMoveRepeats) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 20.0f;
    shade.moveToTarget(80.0f);
    EXPECT_EQ(shade.getLastFrame().repeats, MOVE_REPEATS);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. moveToTiltTarget
// ══════════════════════════════════════════════════════════════════════════════

// B1. target < currentTiltPos → Up command
TEST_F(TargetSequencerTest, MoveToTiltTarget_LowerTarget_SendsUp) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 60.0f;
    shade.currentPos     = shade.target;  // lift at rest → send allowed
    shade.moveToTiltTarget(20.0f);
    EXPECT_EQ(lastCmd(), somfy_commands::Up);
}

// B2. target > currentTiltPos → Down command
TEST_F(TargetSequencerTest, MoveToTiltTarget_HigherTarget_SendsDown) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 30.0f;
    shade.currentPos     = shade.target;
    shade.moveToTiltTarget(80.0f);
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// B3. target == currentTiltPos → no command (My is a no-send)
TEST_F(TargetSequencerTest, MoveToTiltTarget_SameTarget_NoCommand) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.currentPos     = shade.target;
    uint16_t rcBefore    = shade.getLastFrame().rollingCode;
    shade.moveToTiltTarget(50.0f);
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// B4. tiltTarget set to requested value
TEST_F(TargetSequencerTest, MoveToTiltTarget_SetsTiltTarget) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 30.0f;
    shade.currentPos     = shade.target;
    shade.moveToTiltTarget(70.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 70.0f);
}

// B5. Lift not yet at target AND not tiltmotor → command suppressed, tiltTarget still set
TEST_F(TargetSequencerTest, MoveToTiltTarget_LiftMoving_SuppressesCommand) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 30.0f;
    shade.currentPos     = 40.0f;
    shade.target         = 80.0f;  // lift still moving
    uint16_t rcBefore    = shade.getLastFrame().rollingCode;
    shade.moveToTiltTarget(70.0f);
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);  // no send
    EXPECT_FLOAT_EQ(shade.tiltTarget, 70.0f);               // but target set
}

// B6. tiltmotor → sends even when lift is not at rest
TEST_F(TargetSequencerTest, MoveToTiltTarget_TiltMotor_SendsEvenWhileLiftMoving) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 30.0f;
    shade.currentPos     = 40.0f;
    shade.target         = 80.0f;  // lift still moving
    shade.moveToTiltTarget(70.0f);
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// B7. tiltmotor → uses TILT_REPEATS
TEST_F(TargetSequencerTest, MoveToTiltTarget_TiltMotor_UsesTiltRepeats) {
    shade.tiltType       = tilt_types::tiltmotor;
    shade.currentTiltPos = 30.0f;
    shade.currentPos     = shade.target;
    shade.moveToTiltTarget(70.0f);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
}

// B8. target out of valid range (> 100) → tiltTarget not updated, no command
TEST_F(TargetSequencerTest, MoveToTiltTarget_OutOfRange_NoUpdate) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.currentPos     = shade.target;
    uint16_t rcBefore    = shade.getLastFrame().rollingCode;
    shade.moveToTiltTarget(150.0f);  // out of range
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 50.0f);  // unchanged
}

// ══════════════════════════════════════════════════════════════════════════════
// C. moveToMyPosition
// ══════════════════════════════════════════════════════════════════════════════

// C1. Not idle → returns immediately, no command
TEST_F(TargetSequencerTest, MoveToMyPos_NotIdle_DoesNothing) {
    shade.targetSequencer.myPos      = 80.0f;
    shade.currentPos = 50.0f;
    shade.target     = 80.0f;
    shade.setDirection(1);  // moving
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToMyPosition();
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// C2. tiltType::none, currentPos == myPos → returns early, no command
TEST_F(TargetSequencerTest, MoveToMyPos_AlreadyAtMyPos_NoTilt_EarlyReturn) {
    shade.tiltType   = tilt_types::none;
    shade.targetSequencer.myPos      = 50.0f;
    shade.currentPos = 50.0f;
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToMyPosition();
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// C3. tiltType != none, currentPos == myPos AND currentTiltPos == myTiltPos → early return
TEST_F(TargetSequencerTest, MoveToMyPos_AlreadyAtMyPos_WithTilt_EarlyReturn) {
    shade.tiltType       = tilt_types::integrated;
    shade.targetSequencer.myPos          = 50.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;  // must match currentTiltPos for isAtTarget()/isIdle()
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToMyPosition();
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// C4. myPos == -1 AND tiltType == none → early return
TEST_F(TargetSequencerTest, MoveToMyPos_MyPosNotSet_NoTilt_EarlyReturn) {
    shade.tiltType   = tilt_types::none;
    shade.targetSequencer.myPos      = -1.0f;
    shade.currentPos = 50.0f;
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToMyPosition();
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// C5. myPos == -1 AND tiltType != none AND myTiltPos == -1 → early return
TEST_F(TargetSequencerTest, MoveToMyPos_NeitherPosSet_WithTilt_EarlyReturn) {
    shade.tiltType   = tilt_types::integrated;
    shade.targetSequencer.myPos      = -1.0f;
    shade.targetSequencer.myTiltPos  = -1.0f;
    shade.currentPos = 50.0f;
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.moveToMyPosition();
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
}

// C6. simMy = false, valid myPos → sends My command
TEST_F(TargetSequencerTest, MoveToMyPos_NoSimMy_SendsMy) {
    shade.tiltType   = tilt_types::none;
    shade.targetSequencer.myPos      = 80.0f;
    shade.currentPos = 50.0f;
    shade.moveToMyPosition();
    EXPECT_EQ(lastCmd(), somfy_commands::My);
}

// C7. simMy = true → calls moveToTarget instead (settingPos=true, Up/Down sent)
TEST_F(TargetSequencerTest, MoveToMyPos_SimMy_CallsMoveToTarget) {
    shade.setSimMy(true);
    shade.tiltType   = tilt_types::none;
    shade.targetSequencer.myPos      = 80.0f;
    shade.currentPos = 50.0f;
    shade.moveToMyPosition();
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
    EXPECT_TRUE(shade.getSettingPos());
}

// C8. target set to myPos before sending
TEST_F(TargetSequencerTest, MoveToMyPos_SetsTargetToMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.targetSequencer.myPos      = 80.0f;
    shade.currentPos = 50.0f;
    shade.moveToMyPosition();
    EXPECT_FLOAT_EQ(shade.target, 80.0f);
}

// C9. tiltTarget set to myTiltPos when valid
TEST_F(TargetSequencerTest, MoveToMyPos_SetsTiltTargetToMyTiltPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.targetSequencer.myPos          = 80.0f;
    shade.targetSequencer.myTiltPos      = 40.0f;
    shade.currentPos     = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToMyPosition();
    EXPECT_FLOAT_EQ(shade.tiltTarget, 40.0f);
}

// C10. tiltType::tiltonly → currentPos forced to 100 and myPos cleared to -1
TEST_F(TargetSequencerTest, MoveToMyPos_TiltOnly_ForcesCurrentPos100) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.currentTiltPos = 60.0f;
    shade.moveToMyPosition();
    EXPECT_FLOAT_EQ(shade.currentPos, 100.0f);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, -1.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// D. setMyPosition — tiltType::none
// ══════════════════════════════════════════════════════════════════════════════

// D1. pos != currentPos, pos != myPos → moveToTarget called (Down sent)
TEST_F(TargetSequencerTest, SetMyPos_None_PosNotCurrent_NotMyPos_MovesToTarget) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    shade.targetSequencer.myPos      = 30.0f;
    shade.setMyPosition(80, -1);
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// D2. pos != currentPos, pos == myPos → moveToMyPosition called (My sent when !simMy)
TEST_F(TargetSequencerTest, SetMyPos_None_PosNotCurrent_IsMyPos_MovesToMyPosition) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    shade.targetSequencer.myPos      = 80.0f;
    shade.setMyPosition(80, -1);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
}

// D3. pos == currentPos == myPos → sends My (clear-my-pos branch), settingMyPos=true
TEST_F(TargetSequencerTest, SetMyPos_None_AtCurrentAndMyPos_SendsMy) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    shade.targetSequencer.myPos      = 50.0f;
    shade.setMyPosition(50, -1);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// D4. pos == currentPos, pos != myPos → sends My (SETMY_REPEATS) to physically program
//     the motor's favorite position, then stores new myPos
TEST_F(TargetSequencerTest, SetMyPos_None_AtCurrentNotMyPos_SendsMyAndStoresMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 50.0f;
    shade.targetSequencer.myPos      = 30.0f;
    shade.setMyPosition(50, -1);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().repeats, SETMY_REPEATS);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 50.0f);
}

// D5. Not idle → returns immediately
TEST_F(TargetSequencerTest, SetMyPos_None_NotIdle_DoesNothing) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target     = 80.0f;
    shade.setDirection(1);
    uint16_t rcBefore = shade.getLastFrame().rollingCode;
    shade.setMyPosition(80, -1);
    EXPECT_EQ(shade.getLastFrame().rollingCode, rcBefore);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// E. setMyPosition — tiltType::tiltonly
// ══════════════════════════════════════════════════════════════════════════════

// E1. tilt != currentTiltPos → settingMyPos=true + moveToTarget(100, tilt)
TEST_F(TargetSequencerTest, SetMyPos_TiltOnly_TiltDiffersFromCurrent_Moves) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.currentTiltPos = 50.0f;
    shade.targetSequencer.myTiltPos      = -1.0f;
    shade.setMyPosition(100, 20);  // tilt(20) != currentTiltPos(50)
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::Up);  // 20 < 50 → Up
}

// E2. tilt == myTiltPos, currentTiltPos != myTiltPos → moveToMyPosition (My sent)
TEST_F(TargetSequencerTest, SetMyPos_TiltOnly_AtMyTiltPos_NotCurrentTilt_MovesToMyPosition) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.currentTiltPos = 50.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.setMyPosition(100, 30);  // tilt == myTiltPos but != currentTiltPos
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::My);
}

// E3. tilt == currentTiltPos, tilt != myTiltPos → sends My (SETMY_REPEATS) to program
//     motor's favorite position, then stores new myTiltPos
TEST_F(TargetSequencerTest, SetMyPos_TiltOnly_AtCurrentNotMyTilt_SendsMyAndStoresToMyTiltPos) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.setMyPosition(100, 50);  // tilt == currentTiltPos, tilt != myTiltPos
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().repeats, SETMY_REPEATS);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myTiltPos, 50.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// F. setMyPosition — tiltType has-tilt (integrated / tiltmotor, not tiltonly)
// ══════════════════════════════════════════════════════════════════════════════

// F1. pos != currentPos → settingMyPos=true + move initiated
TEST_F(TargetSequencerTest, SetMyPos_HasTilt_PosDiffers_InitiatesMove) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.setMyPosition(80, 50);
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::Down);
}

// F2. pos == myPos AND tilt == myTiltPos → moveToMyPosition (My sent when !simMy)
TEST_F(TargetSequencerTest, SetMyPos_HasTilt_AtMyPos_MovesToMyPosition) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.targetSequencer.myPos          = 80.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.setMyPosition(80, 30);  // == myPos, myTiltPos → moveToMyPosition
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::My);
}

// F3. pos == currentPos AND tilt == currentTiltPos AND both == myPos/myTiltPos →
//     sends My (the exact-match clear path), settingMyPos=true
TEST_F(TargetSequencerTest, SetMyPos_HasTilt_AtCurrentAndMyPos_SendsMy) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;  // must match currentTiltPos so isAtTarget() passes
    shade.targetSequencer.myPos          = 50.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.setMyPosition(50, 30);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// F4. pos == currentPos AND tilt == currentTiltPos AND not at myPos/myTiltPos →
//     sends My (SETMY_REPEATS) to program the motor, stores new myPos/myTiltPos
TEST_F(TargetSequencerTest, SetMyPos_HasTilt_AtCurrentNotMyPos_SendsMyAndStoresMyPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.0f;
    shade.target         = 50.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;  // must match currentTiltPos so isAtTarget() passes
    shade.targetSequencer.myPos          = 20.0f;
    shade.targetSequencer.myTiltPos      = 10.0f;
    shade.setMyPosition(50, 30);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().repeats, SETMY_REPEATS);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos,     50.0f);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myTiltPos, 30.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// G. Float-precision edge cases (floor(x)==floor(y) but x != y)
// ══════════════════════════════════════════════════════════════════════════════

// E. tiltonly: tilt == currentTiltPos AND tilt == myTiltPos →
//    "already at the requested my-position" clear-path; sends My + settingMyPos=true
TEST_F(TargetSequencerTest, SetMyPos_TiltOnly_AtCurrentAndMyTilt_SendsMy) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;  // tilt == currentTiltPos == myTiltPos
    shade.setMyPosition(100, 30);
    EXPECT_EQ(lastCmd(), somfy_commands::My);
    EXPECT_TRUE(shade.getSettingMyPos());
}

// F5. has-tilt: floor(currentPos)==floor(myPos) but currentPos != myPos
//     → "clear my pos" branch with float sub-integer precision; calls moveToMyPosition
TEST_F(TargetSequencerTest, SetMyPos_HasTilt_FloorMatchButExactDiffers_CallsMoveToMyPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentPos     = 50.5f;  // floor = 50
    shade.target         = 50.5f;
    shade.currentTiltPos = 30.0f;
    shade.tiltTarget     = 30.0f;
    shade.targetSequencer.myPos          = 50.8f;  // floor = 50; exact differs from currentPos
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.setMyPosition(50, 30);   // pos=50 == floor(50.5) and == floor(50.8)
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::My);  // moveToMyPosition → My
}

// D6. tiltType::none: floor(currentPos)==floor(myPos) but exact differs
//     → "clear my pos" branch; calls moveToMyPosition
TEST_F(TargetSequencerTest, SetMyPos_None_FloorMatchButExactDiffers_CallsMoveToMyPos) {
    shade.tiltType   = tilt_types::none;
    shade.currentPos = 50.5f;  // floor = 50
    shade.target     = 50.5f;
    shade.targetSequencer.myPos      = 50.8f;  // floor = 50; exact differs
    shade.setMyPosition(50, -1);
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::My);  // moveToMyPosition → My
}

// C3b. moveToMyPosition: currentPos==myPos AND tiltType!=none AND currentTiltPos!=myTiltPos
//      → does NOT return early; continues to set tilt target and send My
TEST_F(TargetSequencerTest, MoveToMyPos_AtPosNotAtTilt_ContinuesToSetTilt) {
    shade.tiltType       = tilt_types::integrated;
    shade.targetSequencer.myPos          = 80.0f;
    shade.targetSequencer.myTiltPos      = 30.0f;
    shade.currentPos     = 80.0f;  // already at myPos
    shade.target         = 80.0f;
    shade.currentTiltPos = 50.0f;  // NOT at myTiltPos
    shade.tiltTarget     = 50.0f;
    shade.moveToMyPosition();
    EXPECT_FLOAT_EQ(shade.tiltTarget, 30.0f);  // tilt target updated
    EXPECT_EQ(lastCmd(), somfy_commands::My);
}

// E2b. tiltonly: floor(currentTiltPos)==floor(myTiltPos)==tilt but exact values differ
//      → "clear my pos" branch; currentTiltPos != myTiltPos → settingMyPos + moveToMyPosition
TEST_F(TargetSequencerTest, SetMyPos_TiltOnly_FloorTiltMatchButExactDiffers_CallsMoveToMyPos) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.currentTiltPos = 30.1f;  // floor = 30
    shade.tiltTarget     = 30.1f;
    shade.targetSequencer.myTiltPos      = 30.8f;  // floor = 30; exact differs
    shade.setMyPosition(100, 30);  // tilt(30)==floor(30.1) → first if false; tilt==floor(30.8) → else if
    EXPECT_TRUE(shade.getSettingMyPos());
    EXPECT_EQ(lastCmd(), somfy_commands::My);  // moveToMyPosition → My
}
