// test_commandProcessor.cpp — tests for SomfyCommandProcessor
//
// Covers: processFrame(), processInternalCommand(), processWaitingFrame()
//   and the motionState flags (settingPos, settingTiltPos, settingMyPos)
//   set/cleared by processFrame, moveToTarget, moveToTiltTarget, setMyPosition.
//
// Guards: unknown remote, shadeId==255, drycontact suppression
// Commands: Up, Down, Stop, My, Toggle, Prog, Sensor, SunFlag, Flag,
//           StepUp, StepDown, Favorite, linked remote
// MotionState: external frame clears flags; internal frame preserves them;
//              moveToTarget/moveToTiltTarget/setMyPosition set them correctly

#include "TestableShade.h"
#include "SomfyRepeatCounts.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using ::testing::_;
using ::testing::AnyNumber;

// ── helpers ────────────────────────────────────────────────────────────────

static somfy_frame_t make_frame(somfy_commands cmd, uint32_t addr = 0xABCDEF, uint16_t rc = 1)
{
    somfy_frame_t f{};
    f.cmd = cmd;
    f.remoteAddress = addr;
    f.rollingCode = rc;
    f.valid = true;
    return f;
}

// ── fixture ───────────────────────────────────────────────────────────────

class CommandProcessorTest : public ::testing::Test {
  protected:
    TestableShade shade;

    void SetUp() override
    {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType = shade_types::roller;
        shade.tiltType = tilt_types::none;
        shade.setUpTime(10000);
        shade.setDownTime(10000);
        shade.setTiltTime(7000);
        shade.setStepSize(100);
        shade.currentPos = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target = 50.0f;
        shade.tiltTarget = 50.0f;

        // Silence unexpected calls to emitState — most tests only care about emitCommand.
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());

        test_clock_ms = 0;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// Guards
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, UnknownRemote_DoesNothing)
{
    auto f = make_frame(somfy_commands::Down, /*addr=*/0x000001);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f); // unchanged
}

TEST_F(CommandProcessorTest, ShadeId255_DoesNothing)
{
    shade.setShadeId(255);
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Up_Roller_SetsTargetZero)
{
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, Up_DryContact_DoesNotEmitCommand)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_TRUE(shade.lastFrame.processed);
}

// ══════════════════════════════════════════════════════════════════════════════
// Down
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Down_Roller_SetsTargetHundred)
{
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, Down_DryContact_DoesNotEmitCommand)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

TEST_F(CommandProcessorTest, Down_DryContact2_TogglesPosition)
{
    shade.shadeType = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Stop
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Stop_SetsTargetToCurrentPos)
{
    shade.currentPos = 42.0f;
    shade.currentTiltPos = 25.0f;
    auto f = make_frame(somfy_commands::Stop);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Stop, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 42.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 25.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// My
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, My_Internal_Idle_MovesToMyPos)
{
    shade.targetSequencer.myPos = 30.0f;
    shade.targetSequencer.myTiltPos = -1.0f;
    shade.currentPos = shade.target = 50.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/true);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(CommandProcessorTest, My_Internal_Idle_MyPosAndTiltPos_BothSet)
{
    shade.targetSequencer.myPos = 30.0f;
    shade.targetSequencer.myTiltPos = 10.0f;
    shade.currentPos = shade.target = 50.0f;
    shade.currentTiltPos = shade.tiltTarget = 50.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/true);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 10.0f);
}

TEST_F(CommandProcessorTest, My_Remote_Idle_SetsAwait)
{
    shade.currentPos = shade.target = 50.0f;
    test_clock_ms = 1000;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(CommandProcessorTest, My_Remote_Moving_Stops)
{
    shade.currentPos = 30.0f;
    shade.target = 80.0f;
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(CommandProcessorTest, My_DryContact_Toggles_AtBottom)
{
    shade.shadeType = shade_types::drycontact;
    shade.currentPos = 100.0f;
    shade.target = 100.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, My_DryContact_Toggles_AtTop)
{
    shade.shadeType = shade_types::drycontact;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, My_DryContact2_Ignored)
{
    shade.shadeType = shade_types::drycontact2;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Toggle
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Toggle_WhenAtBottom_GoesUp)
{
    shade.currentPos = 100.0f;
    shade.target = 100.0f;
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, Toggle_WhenAtTop_GoesDown)
{
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Prog / passthrough
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Prog_EmitsCommand)
{
    auto f = make_frame(somfy_commands::Prog);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Prog, _, _, _));
    shade.processFrame(f, /*internal=*/false);
}

TEST_F(CommandProcessorTest, Prog_DryContact_DoesNotEmit)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Prog);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Linked remote
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, LinkedRemote_AcceptsFrame)
{
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

TEST_F(CommandProcessorTest, Sensor_SetsSunnyFlag)
{
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    shade.processFrame(f);
    EXPECT_TRUE(shade.isSunny());
    EXPECT_FALSE(shade.isWindy());
}

TEST_F(CommandProcessorTest, Sensor_ClearsSunnyFlag)
{
    shade.flags.setSunny(true);
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_FALSE(shade.isSunny());
}

TEST_F(CommandProcessorTest, Sensor_SetsWindyFlag)
{
    test_clock_ms = 5000;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x01);
    shade.processFrame(f);
    EXPECT_TRUE(shade.isWindy());
}

TEST_F(CommandProcessorTest, Sensor_SunTransition_StartsTimer)
{
    test_clock_ms = 1000;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    shade.processFrame(f);
    EXPECT_EQ(shade.getSunStart(), 1000u);
    EXPECT_FALSE(shade.getSunDone());
}

TEST_F(CommandProcessorTest, Sensor_SunClearTransition_StartsNoSunTimer)
{
    test_clock_ms = 2000;
    shade.flags.setSunny(true);
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_EQ(shade.getNoSunStart(), 2000u);
    EXPECT_FALSE(shade.getNoSunDone());
}

TEST_F(CommandProcessorTest, Sensor_WindTransition_StartsTimer)
{
    test_clock_ms = 3000;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x01);
    shade.processFrame(f);
    EXPECT_EQ(shade.getWindStart(), 3000u);
    EXPECT_FALSE(shade.getWindDone());
}

TEST_F(CommandProcessorTest, Sensor_WindClearTransition_StartsNoWindTimer)
{
    test_clock_ms = 4000;
    shade.flags.setWindy(true);
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x00);
    shade.processFrame(f);
    EXPECT_EQ(shade.getNoWindStart(), 4000u);
    EXPECT_FALSE(shade.getNoWindDone());
}

TEST_F(CommandProcessorTest, Sensor_DryContact_DoesNothing)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x02);
    EXPECT_CALL(shade, emitState(_)).Times(0);
    EXPECT_CALL(shade, emitState(_, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FALSE(shade.isSunny());
}

TEST_F(CommandProcessorTest, Sensor_DemoMode_SetsFlag)
{
    auto f = make_frame(somfy_commands::Sensor, 0xABCDEF, /*rc=*/0x04);
    shade.processFrame(f);
    EXPECT_TRUE(shade.getFlags().isDemoMode());
}

// ══════════════════════════════════════════════════════════════════════════════
// SunFlag / Flag
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, SunFlag_SetsSunFlagBit)
{
    auto f = make_frame(somfy_commands::SunFlag);
    shade.processFrame(f);
    EXPECT_TRUE(shade.hasSunFlag());
}

TEST_F(CommandProcessorTest, SunFlag_BogusRollingCode_Ignored)
{
    auto f = make_frame(somfy_commands::SunFlag, 0xABCDEF, /*rc=*/0x8000);
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(CommandProcessorTest, SunFlag_DryContact_Ignored)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::SunFlag);
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(CommandProcessorTest, SunFlag_SunnyAndNoWind_SunDone_MovesToMyPos)
{
    shade.flags.setSunny(true);
    shade.setSunDone(true);
    shade.targetSequencer.myPos = 75.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 75.0f);
}

TEST_F(CommandProcessorTest, SunFlag_SunnyAndNoWind_SunDone_NoMyPos_MovesToHundred)
{
    shade.flags.setWindy(false);
    shade.flags.setSunny(true);
    shade.setSunDone(true);
    shade.targetSequencer.myPos = -1.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, SunFlag_NotSunny_NoSunDone_MovesToZero)
{
    shade.setFlags(0);
    shade.setNoSunDone(true);
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, SunFlag_WindyActive_DoesNotMoveTarget)
{
    shade.flags.setWindy(true);
    shade.flags.setSunny(true);
    shade.setSunDone(true);
    shade.targetSequencer.myPos = 75.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, Flag_ClearsSunFlagBit)
{
    shade.flags.setSunFlag(false);
    auto f = make_frame(somfy_commands::Flag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Flag, _, _, _));
    shade.processFrame(f);
    EXPECT_FALSE(shade.hasSunFlag());
}

TEST_F(CommandProcessorTest, Flag_BogusRollingCode_Ignored)
{
    shade.flags.setSunFlag(true);
    auto f = make_frame(somfy_commands::Flag, 0xABCDEF, /*rc=*/0x8001);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_TRUE(shade.hasSunFlag());
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down with tilt variants and wind suppression
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Up_TiltMotor_Remote_SetsAwait)
{
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(CommandProcessorTest, Down_TiltMotor_Remote_SetsAwait)
{
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getLastFrameAwait(), 0u);
}

TEST_F(CommandProcessorTest, Down_RecentWind_Suppressed)
{
    test_clock_ms = 10000;
    shade.setWindLast(9000);
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, Down_OldWind_NotSuppressed)
{
    test_clock_ms = 100000;
    shade.setWindLast(1000);
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, Up_TiltOnly_Remote_SetsTiltTargetZero)
{
    shade.tiltType = tilt_types::tiltonly;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, Down_TiltOnly_Remote_SetsTiltTargetHundred)
{
    shade.tiltType = tilt_types::tiltonly;
    shade.tiltTarget = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// StepUp / StepDown
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, StepUp_Roller_DecreasesTarget)
{
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_LT(shade.getTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, StepDown_Roller_IncreasesTarget)
{
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, StepUp_Roller_AtTop_DoesNothing)
{
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, StepDown_Roller_AtBottom_DoesNothing)
{
    shade.currentPos = 100.0f;
    shade.target = 100.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, StepUp_DryContact_NoEmit)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::StepUp);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

TEST_F(CommandProcessorTest, StepUp_Repeat_SecondFrameIgnored)
{
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _)).Times(1);
    shade.processFrame(f, /*internal=*/false);
    shade.processFrame(f, /*internal=*/false);
}

TEST_F(CommandProcessorTest, StepUp_TiltOnly_DecreasesTiltTarget)
{
    shade.tiltType = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_LT(shade.getTiltTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, StepDown_TiltOnly_IncreasesTiltTarget)
{
    shade.tiltType = tilt_types::tiltonly;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_GT(shade.getTiltTarget(), 50.0f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Favorite
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Favorite_MovesToMyPos)
{
    shade.targetSequencer.myPos = 40.0f;
    shade.targetSequencer.myTiltPos = -1.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 40.0f);
}

TEST_F(CommandProcessorTest, Favorite_MyPosAndTiltPos_BothSet)
{
    shade.targetSequencer.myPos = 40.0f;
    shade.targetSequencer.myTiltPos = 20.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 40.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 20.0f);
}

TEST_F(CommandProcessorTest, Favorite_Repeat_Ignored)
{
    shade.targetSequencer.myPos = 40.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Favorite, _, _, _)).Times(1);
    shade.processFrame(f);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// MotionState flags — set/cleared by processFrame and move* methods
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, ExternalFrame_ClearsAllMotionStateFlags)
{
    shade.setSettingPos(true);
    shade.setSettingTiltPos(true);
    shade.setSettingMyPos(true);
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FALSE(shade.getSettingPos());
    EXPECT_FALSE(shade.getSettingTiltPos());
    EXPECT_FALSE(shade.getSettingMyPos());
}

TEST_F(CommandProcessorTest, InternalFrame_PreservesAllMotionStateFlags)
{
    shade.setSettingPos(true);
    shade.setSettingTiltPos(true);
    shade.setSettingMyPos(true);
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.getSettingPos());
    EXPECT_TRUE(shade.getSettingTiltPos());
    EXPECT_TRUE(shade.getSettingMyPos());
}

TEST_F(CommandProcessorTest, MoveToTarget_DifferentPos_SetsSettingPos)
{
    shade.currentPos = 50.0f;
    shade.moveToTarget(80.0f);
    EXPECT_TRUE(shade.getSettingPos());
}

TEST_F(CommandProcessorTest, MoveToTarget_SamePos_NoTilt_DoesNotSetSettingPos)
{
    shade.currentPos = 50.0f;
    shade.moveToTarget(50.0f);
    EXPECT_FALSE(shade.getSettingPos());
}

TEST_F(CommandProcessorTest, MoveToTarget_WithTilt_SetsBothFlags)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentPos = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(80.0f, 30.0f);
    EXPECT_TRUE(shade.getSettingPos());
    EXPECT_TRUE(shade.getSettingTiltPos());
}

TEST_F(CommandProcessorTest, MoveToTarget_SamePosNewTilt_SetsSettingTiltPos)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentPos = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.moveToTarget(50.0f, 20.0f);
    EXPECT_TRUE(shade.getSettingPos());
    EXPECT_TRUE(shade.getSettingTiltPos());
}

TEST_F(CommandProcessorTest, MoveToTiltTarget_DifferentTilt_SetsSettingTiltPos)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.currentPos = shade.target;
    shade.moveToTiltTarget(20.0f);
    EXPECT_TRUE(shade.getSettingTiltPos());
}

TEST_F(CommandProcessorTest, MoveToTiltTarget_SameTilt_DoesNotSetSettingTiltPos)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.currentPos = shade.target;
    shade.moveToTiltTarget(50.0f);
    EXPECT_FALSE(shade.getSettingTiltPos());
}

TEST_F(CommandProcessorTest, SetMyPosition_NoTilt_DifferentPos_SetsSettingMyPos)
{
    shade.tiltType = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setMyPosition(80, -1);
    EXPECT_TRUE(shade.getSettingMyPos());
}

TEST_F(CommandProcessorTest, SetMyPosition_NoTilt_AtCurrentAndMyPos_SetsSettingMyPos)
{
    shade.tiltType = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.targetSequencer.myPos = 50.0f;
    shade.setMyPosition(50, -1);
    EXPECT_TRUE(shade.getSettingMyPos());
}

TEST_F(CommandProcessorTest, SetMyPosition_NoTilt_AtCurrentNotMyPos_DoesNotSetSettingMyPos)
{
    shade.tiltType = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.targetSequencer.myPos = 30.0f;
    shade.setMyPosition(50, -1);
    EXPECT_FALSE(shade.getSettingMyPos());
    EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 50.0f);
}

TEST_F(CommandProcessorTest, SetMyPosition_WhileMoving_DoesNothing)
{
    shade.tiltType = tilt_types::none;
    shade.currentPos = 50.0f;
    shade.target = 80.0f;
    shade.setDirection(1);
    shade.setMyPosition(80, -1);
    EXPECT_FALSE(shade.getSettingMyPos());
}

// ══════════════════════════════════════════════════════════════════════════════
// MyUp / MyDown / UpDown / MyUpDown passthrough
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Combo_Commands_EmitPassthrough)
{
    const somfy_commands cmds[] = {
        somfy_commands::MyUp,
        somfy_commands::MyDown,
        somfy_commands::UpDown,
        somfy_commands::MyUpDown,
    };
    for (auto cmd : cmds) {
        auto f = make_frame(cmd);
        EXPECT_CALL(shade, emitCommand(cmd, _, _, _));
        shade.processFrame(f);
    }
}

TEST_F(CommandProcessorTest, MyUp_DryContact_DoesNotEmit)
{
    shade.shadeType = shade_types::drycontact;
    auto f = make_frame(somfy_commands::MyUp);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Toggle — moving shade stops; mid-position uses lastMovement
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Toggle_WhenMoving_StopsAtCurrentPos)
{
    shade.currentPos = 30.0f;
    shade.target = 80.0f;
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(CommandProcessorTest, Toggle_MidPos_LastMovementUp_GoesDown)
{
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setLastMovement(-1);
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, Toggle_MidPos_LastMovementDown_GoesUp)
{
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setLastMovement(1);
    auto f = make_frame(somfy_commands::Toggle);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Toggle, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down DryContact2 — repeat suppression
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Up_DryContact2_AlreadyAtTop_NoTargetChange)
{
    shade.shadeType = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::Up);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Up, _, _, _));
    shade.processFrame(f, /*internal=*/false);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, Down_DryContact2_Repeat_Suppressed)
{
    shade.shadeType = shade_types::drycontact2;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::Down);
    EXPECT_CALL(shade, emitCommand(somfy_commands::Down, _, _, _)).Times(1);
    shade.processFrame(f, /*internal=*/false);
    shade.processFrame(f, /*internal=*/false);
}

// ══════════════════════════════════════════════════════════════════════════════
// SunFlag + tiltonly
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, SunFlag_TiltOnly_Sunny_SunDone_SetsTiltTarget)
{
    shade.tiltType = tilt_types::tiltonly;
    shade.flags.setSunny(true);
    shade.setSunDone(true);
    shade.targetSequencer.myTiltPos = 60.0f;
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 60.0f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
}

TEST_F(CommandProcessorTest, SunFlag_TiltOnly_NotSunny_NoSunDone_SetsTiltTargetZero)
{
    shade.tiltType = tilt_types::tiltonly;
    shade.setFlags(0);
    shade.setNoSunDone(true);
    auto f = make_frame(somfy_commands::SunFlag);
    EXPECT_CALL(shade, emitCommand(somfy_commands::SunFlag, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Up/Down tiltmotor — internal=true path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Up_TiltMotor_Internal_MarksProcessed_NoAwait)
{
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Up);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.lastFrame.processed);
    EXPECT_EQ(shade.getLastFrameAwait(), 0u);
}

TEST_F(CommandProcessorTest, Down_TiltMotor_Internal_MarksProcessed_NoAwait)
{
    shade.tiltType = tilt_types::tiltmotor;
    auto f = make_frame(somfy_commands::Down);
    shade.processFrame(f, /*internal=*/true);
    EXPECT_TRUE(shade.lastFrame.processed);
    EXPECT_EQ(shade.getLastFrameAwait(), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// My — toggle shade types (garage1/lgate1/cgate1/rgate1)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, My_Toggle_Moving_StopsAtCurrentPos)
{
    shade.shadeType = shade_types::garage1;
    shade.currentPos = 40.0f;
    shade.target = 100.0f;
    shade.setDirection(1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 40.0f);
}

TEST_F(CommandProcessorTest, My_Toggle_AtBottom_GoesUp)
{
    shade.shadeType = shade_types::garage1;
    shade.currentPos = 100.0f;
    shade.target = 100.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, My_Toggle_AtTop_GoesDown)
{
    shade.shadeType = shade_types::garage1;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, My_Toggle_MidPosition_UsesLastMovement)
{
    shade.shadeType = shade_types::garage1;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setLastMovement(-1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// My — drycontact mid-position
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, My_DryContact_MidPos_LastMovementNeg1_GoesTo100)
{
    shade.shadeType = shade_types::drycontact;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setLastMovement(-1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, My_DryContact_MidPos_LastMovementPos1_GoesTo0)
{
    shade.shadeType = shade_types::drycontact;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.setLastMovement(1);
    auto f = make_frame(somfy_commands::My);
    EXPECT_CALL(shade, emitCommand(somfy_commands::My, _, _, _));
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// StepUp / StepDown — integrated tilt
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, StepUp_Integrated_BothAtTop_DoesNothing)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.currentPos = 0.0f;
    shade.target = 0.0f;
    shade.tiltTarget = 0.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, StepUp_Integrated_TiltNotAtTop_MovesTilt)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;
    shade.currentPos = 30.0f;
    shade.target = 30.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f);
    EXPECT_LT(shade.getTiltTarget(), 50.0f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(CommandProcessorTest, StepUp_Integrated_TiltAtTop_MovesLift)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 0.0f;
    shade.tiltTarget = 0.0f;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepUp);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepUp, _, _, _));
    shade.processFrame(f);
    EXPECT_LT(shade.getTarget(), 50.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandProcessorTest, StepDown_Integrated_BothAtBottom_DoesNothing)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.currentPos = 100.0f;
    shade.target = 100.0f;
    shade.tiltTarget = 100.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

TEST_F(CommandProcessorTest, StepDown_Integrated_TiltNotAtBottom_MovesTilt)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;
    shade.currentPos = 30.0f;
    shade.target = 30.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f);
    EXPECT_GT(shade.getTiltTarget(), 50.0f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f);
}

TEST_F(CommandProcessorTest, StepDown_Integrated_TiltAtBottom_MovesLift)
{
    shade.tiltType = tilt_types::integrated;
    shade.currentTiltPos = 100.0f;
    shade.tiltTarget = 100.0f;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    auto f = make_frame(somfy_commands::StepDown);
    f.stepSize = 1;
    EXPECT_CALL(shade, emitCommand(somfy_commands::StepDown, _, _, _));
    shade.processFrame(f);
    EXPECT_GT(shade.getTarget(), 50.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Favorite — simMy path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandProcessorTest, Favorite_SimMy_CallsMoveToMyPosition)
{
    shade.flags.setSimMy(true);
    shade.targetSequencer.myPos = 35.0f;
    auto f = make_frame(somfy_commands::Favorite);
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(0);
    shade.processFrame(f);
    EXPECT_FLOAT_EQ(shade.getTarget(), 35.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// processWaitingFrame() — repeat-count boundary conditions
//
// TILT_REPEATS == 15, SETMY_REPEATS == 35
//
// For tiltmotor Up/Down:
//   repeats <  TILT_REPEATS → lift move  (target changes)
//   repeats >= TILT_REPEATS → tilt move  (tiltTarget changes)
//   repeats >  TILT_REPEATS+2 → second emitCommand call
//
// For euromode Up/Down (reversed):
//   repeats <  TILT_REPEATS → tilt move  (tiltTarget changes)
//   repeats >= TILT_REPEATS → lift move  (target changes)
//
// For My (idle, myPos != currentPos):
//   repeats <  SETMY_REPEATS → execute My (target = myPos)
//   repeats >= SETMY_REPEATS → set My position (myPos = currentPos)
// ══════════════════════════════════════════════════════════════════════════════

// Helper: prime lastFrame for processWaitingFrame()
static void primeWaitingFrame(TestableShade &shade, somfy_commands cmd, uint8_t repeats)
{
    shade.lastFrame.cmd = cmd;
    shade.lastFrame.repeats = repeats;
    shade.lastFrame.await = 1;
    shade.lastFrame.processed = false;
    shade.lastFrame.remoteAddress = 0xABCDEF;
    test_clock_ms = 2; // > await so the guard passes
}

// ── A. Tiltmotor Up: lift vs tilt boundary ────────────────────────────────────

struct TiltRepeatCase {
    uint8_t repeats;
    bool expectTilt; // true → tiltTarget changes to 0; false → target changes to 0
};

class TiltmotorRepeatBoundaryTest : public CommandProcessorTest,
                                    public ::testing::WithParamInterface<TiltRepeatCase> {};

TEST_P(TiltmotorRepeatBoundaryTest, UpCommand_LiftVsTilt)
{
    shade.tiltType = tilt_types::tiltmotor;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;

    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    primeWaitingFrame(shade, somfy_commands::Up, GetParam().repeats);
    shade.processWaitingFrame();

    if (GetParam().expectTilt) {
        EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f) << "repeats=" << (int)GetParam().repeats;
        EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
    } else {
        EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f) << "repeats=" << (int)GetParam().repeats;
        EXPECT_FLOAT_EQ(shade.getTiltTarget(), 50.0f);
    }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, TiltmotorRepeatBoundaryTest,
                         ::testing::Values(TiltRepeatCase{TILT_REPEATS - 1, false}, // 14 → lift
                                           TiltRepeatCase{TILT_REPEATS,
                                                          true}, // 15 → tilt (first value to cross threshold)
                                           TiltRepeatCase{TILT_REPEATS + 1, true} // 16 → tilt
                                           ));

// ── B. Euromode Up: reversed lift vs tilt boundary ───────────────────────────

class EuromodeRepeatBoundaryTest : public CommandProcessorTest, public ::testing::WithParamInterface<TiltRepeatCase> {};

TEST_P(EuromodeRepeatBoundaryTest, UpCommand_LiftVsTilt)
{
    shade.tiltType = tilt_types::euromode;
    shade.currentPos = 50.0f;
    shade.target = 50.0f;
    shade.currentTiltPos = 50.0f;
    shade.tiltTarget = 50.0f;

    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    primeWaitingFrame(shade, somfy_commands::Up, GetParam().repeats);
    shade.processWaitingFrame();

    if (GetParam().expectTilt) {
        EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f) << "repeats=" << (int)GetParam().repeats;
        EXPECT_FLOAT_EQ(shade.getTarget(), 50.0f);
    } else {
        EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f) << "repeats=" << (int)GetParam().repeats;
        EXPECT_FLOAT_EQ(shade.getTiltTarget(), 50.0f);
    }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, EuromodeRepeatBoundaryTest,
                         ::testing::Values(TiltRepeatCase{TILT_REPEATS - 1, true}, // 14 → tilt (euromode is reversed)
                                           TiltRepeatCase{TILT_REPEATS, false},    // 15 → lift
                                           TiltRepeatCase{TILT_REPEATS + 1, false} // 16 → lift
                                           ));

// ── C. Tiltmotor double-emit at TILT_REPEATS+2 boundary ──────────────────────
//
// When repeats > TILT_REPEATS+2 (> 17), a second emitCommand is fired.
// repeats=17 is exactly at the boundary (NOT > 17) → single emit.
// repeats=18 crosses it (IS > 17) → double emit.

TEST_F(CommandProcessorTest, ProcessWaitingFrame_Tiltmotor_SingleEmit_AtBoundary)
{
    shade.tiltType = tilt_types::tiltmotor;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(1);
    primeWaitingFrame(shade, somfy_commands::Up, TILT_REPEATS + 2); // 17
    shade.processWaitingFrame();
}

TEST_F(CommandProcessorTest, ProcessWaitingFrame_Tiltmotor_DoubleEmit_BeyondBoundary)
{
    shade.tiltType = tilt_types::tiltmotor;
    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(2);
    primeWaitingFrame(shade, somfy_commands::Up, TILT_REPEATS + 3); // 18
    shade.processWaitingFrame();
}

// ── D. My: execute vs set-My-position boundary ───────────────────────────────

struct MyRepeatCase {
    uint8_t repeats;
    bool expectSetMyPos; // true → myPos updated; false → target moves to myPos
};

class MyRepeatBoundaryTest : public CommandProcessorTest, public ::testing::WithParamInterface<MyRepeatCase> {};

TEST_P(MyRepeatBoundaryTest, IdleShade)
{
    shade.currentPos = 60.0f;
    shade.target = 60.0f; // idle (at target, direction==0)
    shade.targetSequencer.myPos = 30.0f;

    EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
    primeWaitingFrame(shade, somfy_commands::My, GetParam().repeats);
    shade.processWaitingFrame();

    if (GetParam().expectSetMyPos) {
        EXPECT_FLOAT_EQ(shade.targetSequencer.myPos, 60.0f)
            << "repeats=" << (int)GetParam().repeats << " should set myPos to currentPos";
    } else {
        EXPECT_FLOAT_EQ(shade.getTarget(), 30.0f) << "repeats=" << (int)GetParam().repeats << " should move to myPos";
    }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, MyRepeatBoundaryTest,
                         ::testing::Values(MyRepeatCase{SETMY_REPEATS - 1, false}, // 34 → execute My (move to myPos)
                                           MyRepeatCase{SETMY_REPEATS, true}, // 35 → set My position (first crossing)
                                           MyRepeatCase{SETMY_REPEATS + 1, true} // 36 → set My position
                                           ));
