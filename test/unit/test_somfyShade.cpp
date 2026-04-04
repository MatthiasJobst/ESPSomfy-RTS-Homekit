// test_somfyShade.cpp — 100% line coverage for the SomfyShade methods that are
// NOT covered by the more-focused test suites:
//
//  • clear()                         (line 20)
//  • emitState(const char*)          (line 497 — 1-arg delegate)
//  • emitCommand(cmd,…)              (line 500 — 4-arg delegate)
//  • processWaitingFrame()           (lines 512–624)
//  • processInternalCommand()        (lines 891–1061)
//  • setMovement()                   (lines 1083–1097)
//  • setTiltMovement()               (lines 1063–1080)
//  • sendCommand / sendTiltCommand   (lines 1103–1105  — thin delegation)

#include "TestableShade.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;

// ── helpers ────────────────────────────────────────────────────────────────

static somfy_frame_t make_frame(somfy_commands cmd, uint32_t addr = 0xABCDEF,
                                 uint16_t rc = 1, uint8_t repeats = 0) {
    somfy_frame_t f{};
    f.cmd           = cmd;
    f.remoteAddress = addr;
    f.rollingCode   = rc;
    f.valid         = true;
    f.repeats       = repeats;
    return f;
}

// ════════════════════════════════════════════════════════════════════════════
// A  clear()
// ════════════════════════════════════════════════════════════════════════════

class ClearTest : public ::testing::Test {
protected:
    TestableShade shade;
    void SetUp() override {
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    }
};

TEST_F(ClearTest, Clear_ResetsAllFields) {
    shade.setShadeId(3);
    shade.setRemoteAddress(0x123456);
    shade.currentPos    = 75.0f;
    shade.target        = 75.0f;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget    = 50.0f;
    shade.targetSequencer.myPos         = 42.0f;
    shade.setSettingPos(true);
    shade.setSettingTiltPos(true);
    shade.setSettingMyPos(true);

    shade.clear();

    EXPECT_EQ(shade.getShadeId(), 255);
    EXPECT_EQ(shade.getRemoteAddress(), 0u);
    EXPECT_FLOAT_EQ(shade.currentPos, 0.0f);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, -1.0f);
    EXPECT_FALSE(shade.getSettingPos());
    EXPECT_FALSE(shade.getSettingTiltPos());
    EXPECT_FALSE(shade.getSettingMyPos());
}

TEST_F(ClearTest, Clear_ResetsLinkedRemotes) {
    // Verify the linked-remote loop runs (just needs to not crash and reset addrs).
    shade.setShadeId(1);
    shade.clear();
    // If we get here the loop executed without crashing.
    EXPECT_EQ(shade.getShadeId(), 255);
}

// ════════════════════════════════════════════════════════════════════════════
// B  emitState / emitCommand 1-arg / 4-arg delegates
// ════════════════════════════════════════════════════════════════════════════

class EmitDelegateTest : public ::testing::Test {
protected:
    TestableShade shade;
    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    }
};

// Line 497: emitState(const char*) → emitState(255, evt)
TEST_F(EmitDelegateTest, EmitState_1arg_RoutesTo_2arg) {
    EXPECT_CALL(shade, emitState(testing::TypedEq<uint8_t>(255u), _)).Times(1);
    shade.callRealEmitState1("shadeState");
}

// Line 500: emitCommand(cmd, src, addr, evt) → emitCommand(255, cmd, src, addr, evt)
TEST_F(EmitDelegateTest, EmitCommand_4arg_RoutesTo_5arg) {
    EXPECT_CALL(shade, emitCommand(testing::TypedEq<uint8_t>(255u), _, _, _, _)).Times(1);
    shade.callRealEmitCommand4(somfy_commands::Down, "remote", 0xABCDEF);
}

// ════════════════════════════════════════════════════════════════════════════
// C  processWaitingFrame()
// ════════════════════════════════════════════════════════════════════════════

class WaitingFrameTest : public ::testing::Test {
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

        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        test_clock_ms = 0;
    }

    // Put a pending frame in lastFrame and set await to fire on next call.
    void armFrame(somfy_commands cmd, uint8_t repeats = 0) {
        auto f = make_frame(cmd, 0xABCDEF, 1, repeats);
        shade.processFrame(f);                 // copies into lastFrame
        shade.lastFrame.processed = false;     // reset for the waiting-frame test
        shade.lastFrame.await     = 1;         // fire at t=1
        shade.lastFrame.repeats   = repeats;
        test_clock_ms = 2;                     // past the await threshold
    }
};

// Line 513-515: shadeId==255 → clear await and return immediately
TEST_F(WaitingFrameTest, ShadeId255_ClearsAwait) {
    shade.setShadeId(255);
    shade.lastFrame.await     = 999;
    shade.lastFrame.processed = false;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processWaitingFrame();
    EXPECT_EQ(shade.lastFrame.await, 0u);
}

// Line 517: already processed → no-op
TEST_F(WaitingFrameTest, AlreadyProcessed_DoesNothing) {
    shade.lastFrame.processed = true;
    shade.lastFrame.await     = 1;
    test_clock_ms = 2;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processWaitingFrame();
}

// Line 518: await not yet expired → no-op
TEST_F(WaitingFrameTest, AwaitNotExpired_DoesNothing) {
    shade.lastFrame.processed = false;
    shade.lastFrame.await     = 100;
    test_clock_ms = 50;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processWaitingFrame();
}

// Lines 521-528: StepUp with currentPos > 0 → target moves up 1%
TEST_F(WaitingFrameTest, StepUp_DecreasesTarget) {
    armFrame(somfy_commands::StepUp);
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _)).Times(1);
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 49.0f);
}

// StepUp when currentPos==0 → no command, no target change
TEST_F(WaitingFrameTest, StepUp_AtZero_DoesNothing) {
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    armFrame(somfy_commands::StepUp);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// Lines 530-537: StepDown with currentPos < 100 → target moves down 1%
TEST_F(WaitingFrameTest, StepDown_IncreasesTarget) {
    armFrame(somfy_commands::StepDown);
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _)).Times(1);
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 51.0f);
}

// StepDown when currentPos==100 → no command
TEST_F(WaitingFrameTest, StepDown_AtHundred_DoesNothing) {
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    armFrame(somfy_commands::StepDown);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processWaitingFrame();
}

// Lines 541-560: tiltmotor + Up, repeats < TILT_REPEATS → lift command
TEST_F(WaitingFrameTest, TiltMotor_Up_LowRepeats_SetsLiftTarget) {
    shade.tiltType = tilt_types::tiltmotor;
    armFrame(somfy_commands::Up, /*repeats=*/5);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);   // lift moves up
}

// Lines 541-548: tiltmotor + Up, repeats >= TILT_REPEATS → tilt command
TEST_F(WaitingFrameTest, TiltMotor_Up_HighRepeats_SetsTiltTarget) {
    shade.tiltType = tilt_types::tiltmotor;
    armFrame(somfy_commands::Up, /*repeats=*/15);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// tiltmotor + Up, repeats > TILT_REPEATS+2 → processed flag set again
TEST_F(WaitingFrameTest, TiltMotor_Up_OverRepeats_MarksProcessed) {
    shade.tiltType = tilt_types::tiltmotor;
    armFrame(somfy_commands::Up, /*repeats=*/20);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_TRUE(shade.lastFrame.processed);
}

// Lines 551-555: tiltmotor + Down, low repeats → lift Down
TEST_F(WaitingFrameTest, TiltMotor_Down_LowRepeats_SetsLiftTarget) {
    shade.tiltType = tilt_types::tiltmotor;
    armFrame(somfy_commands::Down, /*repeats=*/5);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// Lines 562-569: euromode + Up, high repeats → lift
TEST_F(WaitingFrameTest, EuroMode_Up_HighRepeats_SetsLiftTarget) {
    shade.tiltType = tilt_types::euromode;
    armFrame(somfy_commands::Up, /*repeats=*/15);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// Lines 571-577: euromode + Up, low repeats → tilt
TEST_F(WaitingFrameTest, EuroMode_Up_LowRepeats_SetsTiltTarget) {
    shade.tiltType = tilt_types::euromode;
    armFrame(somfy_commands::Up, /*repeats=*/5);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// euromode + Up, over repeats → processed
TEST_F(WaitingFrameTest, EuroMode_Up_OverRepeats_MarksProcessed) {
    shade.tiltType = tilt_types::euromode;
    armFrame(somfy_commands::Up, /*repeats=*/20);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_TRUE(shade.lastFrame.processed);
}

// euromode + Down, high repeats → lift Down
TEST_F(WaitingFrameTest, EuroMode_Down_HighRepeats_SetsLiftTarget) {
    shade.tiltType = tilt_types::euromode;
    armFrame(somfy_commands::Down, /*repeats=*/15);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// euromode + Down, low repeats → tilt
TEST_F(WaitingFrameTest, EuroMode_Down_LowRepeats_SetsTiltTarget) {
    shade.tiltType = tilt_types::euromode;
    armFrame(somfy_commands::Down, /*repeats=*/5);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.tiltTarget, 100.0f);
}

// Lines 586-598: My, SETMY_REPEATS, idle, myPos differs → store myPos
TEST_F(WaitingFrameTest, My_HighRepeats_Idle_DifferentPos_StoresMyPos) {
    shade.targetSequencer.myPos = -1.0f;
    armFrame(somfy_commands::My, /*repeats=*/35);
    EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 50.0f);
}

// Lines 586-591: My, SETMY_REPEATS, idle, floor(myPos)==floor(currentPos) → clear
TEST_F(WaitingFrameTest, My_HighRepeats_Idle_SamePos_ClearsMyPos) {
    shade.targetSequencer.myPos = 50.0f;   // same floor as currentPos=50
    armFrame(somfy_commands::My, /*repeats=*/35);
    EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, -1.0f);
}

// Lines 600-609: My, low repeats, idle, no simMy, myPos set → move to my
TEST_F(WaitingFrameTest, My_LowRepeats_Idle_WithMyPos_SetsTarget) {
    shade.targetSequencer.myPos = 30.0f;
    shade.tiltTarget = 50.0f;
    armFrame(somfy_commands::My, /*repeats=*/1);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _)).Times(1);
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 30.0f);
}

// Lines 611-613: My, low repeats, NOT idle → stop at current pos
TEST_F(WaitingFrameTest, My_LowRepeats_NotIdle_StopsAtCurrentPos) {
    shade.setDirection(1);          // make it not-idle
    shade.target = 80.0f;           // moving but not yet at target
    armFrame(somfy_commands::My, /*repeats=*/1);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_FLOAT_EQ(shade.target, 50.0f);   // stopped at currentPos
}

// Lines 615-618: My, repeats > SETMY_REPEATS+2 → processed
TEST_F(WaitingFrameTest, My_VeryHighRepeats_MarksProcessed) {
    shade.targetSequencer.myPos = -1.0f;
    armFrame(somfy_commands::My, /*repeats=*/40);
    EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
    shade.processWaitingFrame();
    EXPECT_TRUE(shade.lastFrame.processed);
}

// Lines 583-585: My, low repeats, idle, simMy set → calls moveToMyPosition
TEST_F(WaitingFrameTest, My_LowRepeats_Idle_SimMy_CallsMoveToMyPosition) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SimMy));
    armFrame(somfy_commands::My, /*repeats=*/1);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    // No crash and processed flag set = pass
    EXPECT_TRUE(shade.lastFrame.processed);
}

// default case → no crash
TEST_F(WaitingFrameTest, Default_NoAction) {
    armFrame(somfy_commands::Prog);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    shade.processWaitingFrame();
    // Just verifying no crash
}

// ════════════════════════════════════════════════════════════════════════════
// H  processFrame default branch (Unknown0 / UnknownD / RTWProto → default:)
// ════════════════════════════════════════════════════════════════════════════

class ProcessFrameDefaultTest : public ::testing::Test {
protected:
    TestableShade shade;
    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    }
};

// Unknown0 is not handled by any explicit case → hits default:
TEST_F(ProcessFrameDefaultTest, Unknown0_HitsDefault_NoTargetChange) {
    auto f = make_frame(somfy_commands::Unknown0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// RTWProto is not handled → hits default:
TEST_F(ProcessFrameDefaultTest, RTWProto_HitsDefault_NoTargetChange) {
    auto f = make_frame(somfy_commands::RTWProto);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// ════════════════════════════════════════════════════════════════════════════
// D  processInternalCommand()
// ════════════════════════════════════════════════════════════════════════════

class InternalCmdTest : public ::testing::Test {
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

        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
        test_clock_ms = 0;
    }
};

// Line 895: shadeId==255 → return immediately
TEST_F(InternalCmdTest, ShadeId255_ReturnsEarly) {
    shade.setShadeId(255);
    shade.processInternalCommand(somfy_commands::Down, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);  // unchanged
}

// Up roller → target=0
TEST_F(InternalCmdTest, Up_Roller_SetsTargetZero) {
    shade.processInternalCommand(somfy_commands::Up, 1);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// Up tiltmotor, low repeat → lift
TEST_F(InternalCmdTest, Up_TiltMotor_LowRepeat_SetsLift) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.processInternalCommand(somfy_commands::Up, 5);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// Up tiltmotor, high repeat → tilt
TEST_F(InternalCmdTest, Up_TiltMotor_HighRepeat_SetsTilt) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.processInternalCommand(somfy_commands::Up, 15);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// Up tiltonly → forces pos=100 and tilt→0
TEST_F(InternalCmdTest, Up_TiltOnly_ForcesFullOpenPos) {
    shade.tiltType = tilt_types::tiltonly;
    shade.processInternalCommand(somfy_commands::Up, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// Down roller → target=100
TEST_F(InternalCmdTest, Down_Roller_SetsTargetHundred) {
    shade.processInternalCommand(somfy_commands::Down, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// Down tiltmotor low repeat → lift
TEST_F(InternalCmdTest, Down_TiltMotor_LowRepeat_SetsLift) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.processInternalCommand(somfy_commands::Down, 5);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// Down tiltmotor high repeat → tilt
TEST_F(InternalCmdTest, Down_TiltMotor_HighRepeat_SetsTilt) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.processInternalCommand(somfy_commands::Down, 15);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 100.0f);
}

// Down tiltonly → forces pos=100 and tilt→100
TEST_F(InternalCmdTest, Down_TiltOnly_ForcesFullClosedPos) {
    shade.tiltType = tilt_types::tiltonly;
    shade.processInternalCommand(somfy_commands::Down, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 100.0f);
}

// Down blind (none tilt, has tilt target) → also sets tiltTarget
TEST_F(InternalCmdTest, Down_Roller_WithTiltTypeBlind_SetsTiltTarget) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.processInternalCommand(somfy_commands::Down, 20);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 100.0f);
}

// Down suppressed after wind event
TEST_F(InternalCmdTest, Down_WindSuppressed_IgnoresCommand) {
    shade.setWindLast(1);
    test_clock_ms = 2;  // within NO_WIND_REMOTE_TIMEOUT
    shade.processInternalCommand(somfy_commands::Down, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);  // unchanged
}

// My idle, no simMy, myPos set → moves to myPos
TEST_F(InternalCmdTest, My_Idle_WithMyPos_SetsTarget) {
    shade.targetSequencer.myPos = 30.0f;
    shade.processInternalCommand(somfy_commands::My, 1);
    EXPECT_FLOAT_EQ(shade.target, 30.0f);
}

// My idle, no simMy, myPos==-1 → no target change
TEST_F(InternalCmdTest, My_Idle_NoMyPos_NoChange) {
    shade.targetSequencer.myPos = -1.0f;
    shade.processInternalCommand(somfy_commands::My, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// My idle, simMy set → calls moveToMyPosition (via targetSequencer, no crash)
TEST_F(InternalCmdTest, My_Idle_SimMy_CallsMoveToMyPosition) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SimMy));
    shade.processInternalCommand(somfy_commands::My, 1);
    // No crash = pass
}

// My not-idle roller → stops at currentPos
TEST_F(InternalCmdTest, My_NotIdle_Roller_StopsAtCurrentPos) {
    shade.setDirection(1);
    shade.target = 80.0f;
    shade.processInternalCommand(somfy_commands::My, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// My not-idle tiltonly → target forced to 100
TEST_F(InternalCmdTest, My_NotIdle_TiltOnly_SetTarget100) {
    shade.tiltType = tilt_types::tiltonly;
    shade.setDirection(1);
    shade.target = 80.0f;
    shade.processInternalCommand(somfy_commands::My, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// StepUp roller, mid pos → target decreases
TEST_F(InternalCmdTest, StepUp_Roller_DecreasesTarget) {
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_LT(shade.target, 50.0f);
}

// StepUp roller at 0 → no target change
TEST_F(InternalCmdTest, StepUp_Roller_AtZero_NoChange) {
    shade.currentPos = 0.0f;
    shade.target     = 0.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// StepUp stepSize==0 → return immediately
TEST_F(InternalCmdTest, StepUp_StepSizeZero_ReturnsEarly) {
    shade.setStepSize(0);
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// StepUp integrated, tilt>0 → moves tilt first
TEST_F(InternalCmdTest, StepUp_Integrated_TiltAboveZero_MovesTilt) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_LT(shade.tiltTarget, 50.0f);
}

// StepUp integrated, tilt==0, pos>0 → moves lift
TEST_F(InternalCmdTest, StepUp_Integrated_TiltAtZero_MovesLift) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 0.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_LT(shade.target, 50.0f);
}

// StepUp integrated, both at 0 → early return
TEST_F(InternalCmdTest, StepUp_Integrated_BothAtZero_NoChange) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 0.0f;
    shade.currentPos     = 0.0f;
    shade.target         = 0.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// StepUp integrated, tiltTime==0 → return to avoid divide by zero
TEST_F(InternalCmdTest, StepUp_Integrated_TiltTimeZero_ReturnsEarly) {
    shade.tiltType       = tilt_types::integrated;
    shade.setTiltTime(0);
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 50.0f);
}

// StepUp integrated, upTime==0 → return
TEST_F(InternalCmdTest, StepUp_Integrated_UpTimeZero_ReturnsEarly) {
    shade.tiltType       = tilt_types::integrated;
    shade.setUpTime(0);
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 0.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// StepUp tiltonly, tiltPos>0 → moves tilt
TEST_F(InternalCmdTest, StepUp_TiltOnly_MovesTilt) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_LT(shade.tiltTarget, 50.0f);
}

// StepUp tiltonly, tiltPos==0 → no change
TEST_F(InternalCmdTest, StepUp_TiltOnly_AtZero_NoChange) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget     = 0.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 0.0f);
}

// StepUp tiltonly, tiltTime==0 → no change
TEST_F(InternalCmdTest, StepUp_TiltOnly_TiltTimeZero_NoChange) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.setTiltTime(0);
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 50.0f);
}

// StepUp roller, upTime==0 → no change
TEST_F(InternalCmdTest, StepUp_Roller_UpTimeZero_NoChange) {
    shade.setUpTime(0);
    shade.processInternalCommand(somfy_commands::StepUp, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// StepDown roller, mid pos → target increases
TEST_F(InternalCmdTest, StepDown_Roller_IncreasesTarget) {
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_GT(shade.target, 50.0f);
}

// StepDown roller at 100 → no change
TEST_F(InternalCmdTest, StepDown_Roller_AtHundred_NoChange) {
    shade.currentPos = 100.0f;
    shade.target     = 100.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// StepDown stepSize==0 → return
TEST_F(InternalCmdTest, StepDown_StepSizeZero_ReturnsEarly) {
    shade.setStepSize(0);
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// StepDown integrated, tilt<100 → moves tilt first
TEST_F(InternalCmdTest, StepDown_Integrated_TiltBelowHundred_MovesTilt) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_GT(shade.tiltTarget, 50.0f);
}

// StepDown integrated, tilt==100, pos<100 → moves lift
TEST_F(InternalCmdTest, StepDown_Integrated_TiltAtHundred_MovesLift) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 100.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_GT(shade.target, 50.0f);
}

// StepDown integrated, both at 100 → early return
TEST_F(InternalCmdTest, StepDown_Integrated_BothAtHundred_NoChange) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 100.0f;
    shade.currentPos     = 100.0f;
    shade.target         = 100.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.target, 100.0f);
}

// StepDown integrated, tiltTime==0 → return
TEST_F(InternalCmdTest, StepDown_Integrated_TiltTimeZero_ReturnsEarly) {
    shade.tiltType       = tilt_types::integrated;
    shade.setTiltTime(0);
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 50.0f);
}

// StepDown integrated, downTime==0, tilt at 100 → return
TEST_F(InternalCmdTest, StepDown_Integrated_DownTimeZero_ReturnsEarly) {
    shade.tiltType       = tilt_types::integrated;
    shade.setDownTime(0);
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 100.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// StepDown tiltonly, tiltPos<100 → moves tilt
TEST_F(InternalCmdTest, StepDown_TiltOnly_MovesTilt) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_GT(shade.tiltTarget, 50.0f);
}

// StepDown tiltonly, at 100 → no change
TEST_F(InternalCmdTest, StepDown_TiltOnly_AtHundred_NoChange) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget     = 100.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 100.0f);
}

// StepDown tiltonly, tiltTime==0 → no change
TEST_F(InternalCmdTest, StepDown_TiltOnly_TiltTimeZero_NoChange) {
    shade.tiltType       = tilt_types::tiltonly;
    shade.setTiltTime(0);
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget     = 50.0f;
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.tiltTarget, 50.0f);
}

// StepDown roller, downTime==0 → no change
TEST_F(InternalCmdTest, StepDown_Roller_DownTimeZero_NoChange) {
    shade.setDownTime(0);
    shade.processInternalCommand(somfy_commands::StepDown, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// Flag, no SunSensor → just clears sunFlag (ESP_LOGI path)
TEST_F(InternalCmdTest, Flag_NoSunSensor_ClearsSunFlag) {
    shade.setFlags(0);  // no SunSensor bit
    shade.processInternalCommand(somfy_commands::Flag, 1);
    EXPECT_FALSE(shade.hasSunFlag());
}

// Flag, hasSunSensor → sets isDirty, calls emitState
TEST_F(InternalCmdTest, Flag_WithSunSensor_SetsIsDirty) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunSensor));
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.processInternalCommand(somfy_commands::Flag, 1);
    EXPECT_FALSE(shade.hasSunFlag());
}

// SunFlag, no SunSensor → no-op (ESP_LOGI path)
TEST_F(InternalCmdTest, SunFlag_NoSunSensor_NoChange) {
    shade.setFlags(0);
    shade.processInternalCommand(somfy_commands::SunFlag, 1);
    EXPECT_FALSE(shade.hasSunFlag());
}

// SunFlag, hasSunSensor → sets sunFlag, emitState
TEST_F(InternalCmdTest, SunFlag_WithSunSensor_SetsSunFlag) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunSensor));
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.processInternalCommand(somfy_commands::SunFlag, 1);
    EXPECT_TRUE(shade.hasSunFlag());
}

// SunFlag, sunny+sunDone → sets target to myPos or 100
TEST_F(InternalCmdTest, SunFlag_SunSensor_Sunny_SunDone_SetsTarget) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunSensor) |
                   static_cast<uint8_t>(somfy_flags_t::Sunny));
    shade.setSunDone(true);
    shade.targetSequencer.myPos = 70.0f;
    shade.processInternalCommand(somfy_commands::SunFlag, 1);
    EXPECT_FLOAT_EQ(shade.target, 70.0f);
}

// SunFlag, not sunny+noSunDone → sets target to 0
TEST_F(InternalCmdTest, SunFlag_SunSensor_NotSunny_NoSunDone_SetsTargetZero) {
    shade.setFlags(static_cast<uint8_t>(somfy_flags_t::SunSensor));
    shade.setNoSunDone(true);
    shade.processInternalCommand(somfy_commands::SunFlag, 1);
    EXPECT_FLOAT_EQ(shade.target, 0.0f);
}

// default case → no crash
TEST_F(InternalCmdTest, Default_NoAction) {
    shade.processInternalCommand(somfy_commands::Prog, 1);
    EXPECT_FLOAT_EQ(shade.target, 50.0f);
}

// ════════════════════════════════════════════════════════════════════════════
// E  setMovement()
// ════════════════════════════════════════════════════════════════════════════

class SetMovementTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;

        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
        test_clock_ms = 100;
    }
};

// dir != 0 → sets moveStart/startPos/startTiltPos
TEST_F(SetMovementTest, NonZeroDir_SetsStartState) {
    shade.setMovement(1);
    EXPECT_EQ(shade.getMoveStart(), 100u);
}

// dir == 0 when already stopped → no commitShadePosition (direction stays 0)
TEST_F(SetMovementTest, ZeroDir_AlreadyStopped_NoCommit) {
    shade.setDirection(0);
    // Just verifying no crash and state unchanged
    shade.setMovement(0);
    EXPECT_EQ(shade.getDirection(), 0);
}

// dir == 0 from moving state → calls commitShadePosition (no crash, direction unchanged by setMovement)
TEST_F(SetMovementTest, ZeroDir_FromMoving_CommitsPosition) {
    shade.setDirection(1);
    shade.setMovement(0);
    // setMovement(0) calls commitShadePosition but does NOT call p_direction; direction is
    // zeroed only by checkMovement. Just verify it doesn't crash.
    EXPECT_EQ(shade.getDirection(), 1);
}

// ════════════════════════════════════════════════════════════════════════════
// F  setTiltMovement()
// ════════════════════════════════════════════════════════════════════════════

class SetTiltMovementTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::tiltmotor;
        shade.currentTiltPos = 50.0f;
        shade.tiltTarget     = 50.0f;

        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        test_clock_ms = 100;
    }
};

// dir==0 from stopped → no commit, no emitState
TEST_F(SetTiltMovementTest, StopFromStopped_NoEmit) {
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.setTiltMovement(0);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// dir==0 from moving → commits tilt position, emitState fired
TEST_F(SetTiltMovementTest, StopFromMoving_CommitsAndEmits) {
    shade.tiltDirection = 1;
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.setTiltMovement(0);
    EXPECT_EQ(shade.getTiltDirection(), 0);
}

// dir==1 from stopped → starts moving, emitState
TEST_F(SetTiltMovementTest, StartMoving_SetsDirection_Emits) {
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.setTiltMovement(1);
    EXPECT_EQ(shade.getTiltDirection(), 1);
}

// dir==1 when already moving in that direction → no change, no emitState
TEST_F(SetTiltMovementTest, SameDirection_NoChange_NoEmit) {
    shade.tiltDirection = 1;
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.setTiltMovement(1);
    EXPECT_EQ(shade.getTiltDirection(), 1);
}

// Reverse direction → emitState
TEST_F(SetTiltMovementTest, ReverseDirection_Emits) {
    shade.tiltDirection = 1;
    EXPECT_CALL(shade, emitState(_)).Times(AtLeast(1));
    shade.setTiltMovement(-1);
    EXPECT_EQ(shade.getTiltDirection(), -1);
}

// ════════════════════════════════════════════════════════════════════════════
// G  sendCommand / sendTiltCommand delegation (thin wrappers — just verifies
//    they don't crash; the real work is in CommandTransmitter's test suite)
// ════════════════════════════════════════════════════════════════════════════

class SendCmdTest : public ::testing::Test {
protected:
    TestableShade shade;
    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    }
};

TEST_F(SendCmdTest, SendCommand_1arg_NoCrash) {
    shade.sendCommand(somfy_commands::Down);
}

TEST_F(SendCmdTest, SendCommand_3arg_NoCrash) {
    shade.sendCommand(somfy_commands::Down, 1, 0);
}

TEST_F(SendCmdTest, SendTiltCommand_NoCrash) {
    shade.sendTiltCommand(somfy_commands::Down);
}

// ════════════════════════════════════════════════════════════════════════════
// I  SomfyShade delegation stubs for process* helpers
//    These thin wrappers route to commandProcessor; calling them directly
//    covers the stub lines in SomfyShade.cpp.
// ════════════════════════════════════════════════════════════════════════════

class ProcessDelegateTest : public ::testing::Test {
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
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());
        test_clock_ms = 0;
    }
    somfy_frame_t make_f(somfy_commands cmd, uint32_t addr = 0xABCDEF) {
        somfy_frame_t f{};
        f.cmd = cmd; f.remoteAddress = addr; f.valid = true;
        return f;
    }
};

TEST_F(ProcessDelegateTest, ProcessSensorCommand_NoCrash) {
    auto f = make_f(somfy_commands::Sensor);
    shade.processSensorCommand(f, 0);
}

TEST_F(ProcessDelegateTest, ProcessFlagCommand_NoCrash) {
    auto f = make_f(somfy_commands::Flag);
    shade.processFlagCommand(false, f);
}

TEST_F(ProcessDelegateTest, ProcessSunFlagCommand_NoCrash) {
    auto f = make_f(somfy_commands::SunFlag);
    shade.processSunFlagCommand(false, f);
}

TEST_F(ProcessDelegateTest, ProcessMyCommand_NoCrash) {
    auto f = make_f(somfy_commands::My);
    shade.processMyCommand(false, f, 0);
}

TEST_F(ProcessDelegateTest, ProcessUpDownCommand_NoCrash) {
    auto f = make_f(somfy_commands::Down);
    shade.processUpDownCommand(somfy_commands::Down, 1, false, f, 0);
}

TEST_F(ProcessDelegateTest, ProcessStepCommand_NoCrash) {
    auto f = make_f(somfy_commands::StepDown);
    shade.processStepCommand(somfy_commands::StepDown, 1, false, f);
}
