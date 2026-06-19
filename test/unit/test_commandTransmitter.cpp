// test_commandTransmitter.cpp — tests for the CommandTransmitter
// methods of SomfyShade:
//   sendCommand()  — all cmd branches: Up/Down (normal + euromode + tiltonly +
//                    integrated), My (toggle/drycontact/drycontact2/idle/moving),
//                    Toggle (non-80-bit / 80-bit), Prog (toggle shade), fallthrough
//   sendTiltCommand() — Up / Down / My
//
// Linked-remote management (linkRemote/unlinkRemote) lives in SomfyLinkedRemotes
// and is covered by test_linkedRemotes.cpp.
//
// SomfyRemote::sendCommand() is the real implementation; it writes lastFrame
// which is observable via shade.getLastFrame().

#include "TestableShade.h"
#include "nvs.h"
#include "MQTT.h"
#include "SomfyShadeController.h"
#include "SomfyRepeatCounts.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern SomfyShadeController somfy;
extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;

using ::testing::_;
using ::testing::AnyNumber;

// ── fixture ───────────────────────────────────────────────────────────────

class CommandTransmitterTest : public ::testing::Test {
  protected:
    TestableShade shade;

    void SetUp() override
    {
        shade.setShadeId(1);
        shade.setRemoteAddress(0x112233);
        shade.shadeType = shade_types::roller;
        shade.tiltType = tilt_types::none;
        shade.currentPos = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target = 50.0f;
        shade.tiltTarget = 50.0f;
        shade.direction = 0;
        shade.tiltDirection = 0;
        shade.setFlipPosition(false);
        shade.setProto(radio_proto::RTS);
        // repeats = 1; bitLength set by SomfyRemote default

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        nvs_stub_reset_all();
        mqtt_connected_flag = false;
        mqtt_published.clear();
    }

    void TearDown() override { nvs_stub_reset_all(); }

    // Make the shade look moving (direction != 0) so isIdle() == false
    void setMoving()
    {
        shade.direction = 1;
        shade.target = 100.0f; // not at target
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. sendCommand() — Up
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Up_TiltTypeVariants)
{
    struct Case {
        tilt_types tilt;
        float expectedTarget;
        float expectedTiltTarget; // < 0 = don't check
        uint8_t expectedRepeats;  // 0 = don't check
    };
    const Case cases[] = {
        {tilt_types::none, 0.0f, -1.0f, 0},
        {tilt_types::integrated, 0.0f, 0.0f, 0},
        {tilt_types::tiltonly, 100.0f, 0.0f, 0},
        {tilt_types::euromode, 0.0f, -1.0f, TILT_REPEATS},
    };
    for (auto &c : cases) {
        shade.tiltType = c.tilt;
        shade.currentPos = 50.0f;
        shade.sendCommand(somfy_commands::Up);
        EXPECT_FLOAT_EQ(shade.getTarget(), c.expectedTarget) << "tiltType=" << (int)c.tilt;
        if (c.expectedTiltTarget >= 0.0f)
            EXPECT_FLOAT_EQ(shade.getTiltTarget(), c.expectedTiltTarget) << "tiltType=" << (int)c.tilt;
        if (c.expectedRepeats) EXPECT_EQ(shade.getLastFrame().repeats, c.expectedRepeats) << "tiltType=" << (int)c.tilt;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// B. sendCommand() — Down
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Down_TiltTypeVariants)
{
    struct Case {
        tilt_types tilt;
        float expectedTarget;
        float expectedTiltTarget; // < 0 = don't check
        uint8_t expectedRepeats;  // 0 = don't check
    };
    const Case cases[] = {
        {tilt_types::none, 100.0f, -1.0f, 0},
        {tilt_types::integrated, 100.0f, 100.0f, 0},
        {tilt_types::tiltonly, 100.0f, 100.0f, 0},
        {tilt_types::euromode, 100.0f, -1.0f, TILT_REPEATS},
    };
    for (auto &c : cases) {
        shade.tiltType = c.tilt;
        shade.currentPos = 50.0f;
        shade.sendCommand(somfy_commands::Down);
        EXPECT_FLOAT_EQ(shade.getTarget(), c.expectedTarget) << "tiltType=" << (int)c.tilt;
        if (c.expectedTiltTarget >= 0.0f)
            EXPECT_FLOAT_EQ(shade.getTiltTarget(), c.expectedTiltTarget) << "tiltType=" << (int)c.tilt;
        if (c.expectedRepeats) EXPECT_EQ(shade.getLastFrame().repeats, c.expectedRepeats) << "tiltType=" << (int)c.tilt;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// C. sendCommand() — My
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_My_Toggle_SendsDirectly)
{
    shade.shadeType = shade_types::garage1; // isToggle() == true
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_My_DryContact_SendsDirectly)
{
    shade.shadeType = shade_types::drycontact;
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_My_DryContact2_ReturnsEarly)
{
    shade.shadeType = shade_types::drycontact2;
    // drycontact2 My is a no-op — frame should NOT be updated
    uint16_t codeBefore = shade.getLastFrame().rollingCode;
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().rollingCode, codeBefore);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Idle_MovesToMyPosition)
{
    // isIdle() == true: at target, direction == 0
    shade.shadeType = shade_types::roller;
    shade.tiltType = tilt_types::none;
    shade.target = shade.currentPos; // at target
    shade.targetSequencer.myPos = 25.0f;
    shade.sendCommand(somfy_commands::My);
    // moveToMyPosition() sets target to myPos
    EXPECT_FLOAT_EQ(shade.getTarget(), 25.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Moving_StopsAndSetsTarget)
{
    setMoving();
    shade.tiltType = tilt_types::none;
    shade.sendCommand(somfy_commands::My);
    // Stops: p_target(currentPos), p_tiltTarget(currentTiltPos)
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
    EXPECT_FLOAT_EQ(shade.getTarget(), shade.currentPos);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Moving_TiltOnly_DoesNotSetTarget)
{
    setMoving();
    shade.tiltType = tilt_types::tiltonly;
    shade.sendCommand(somfy_commands::My);
    // tiltonly: p_target is skipped, only p_tiltTarget is set
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), shade.currentTiltPos);
}

// ══════════════════════════════════════════════════════════════════════════════
// D. sendCommand() — Toggle and Prog
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Toggle_Non80Bit_SendsMyCommand)
{
    shade.bitLength = 56; // != 80
    shade.sendCommand(somfy_commands::Toggle);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_Toggle_80Bit_SendsToggleCommand)
{
    shade.bitLength = 80;
    shade.sendCommand(somfy_commands::Toggle);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Toggle);
}

TEST_F(CommandTransmitterTest, SendCommand_Prog_IsToggle_SendsToggleCommand)
{
    shade.shadeType = shade_types::garage1; // isToggle() == true
    shade.sendCommand(somfy_commands::Prog);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Toggle);
}

TEST_F(CommandTransmitterTest, SendCommand_Prog_NotToggle_SendsProgCommand)
{
    shade.shadeType = shade_types::roller; // isToggle() == false
    shade.sendCommand(somfy_commands::Prog);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Prog);
}

// ══════════════════════════════════════════════════════════════════════════════
// E. sendCommand() — fallthrough (Stop)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Stop_Fallthrough_SendsStopCommand)
{
    shade.sendCommand(somfy_commands::Stop);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Stop);
}

// ══════════════════════════════════════════════════════════════════════════════
// F. sendTiltCommand()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendTiltCommand_Integrated_TargetBehavior)
{
    // Up → tiltTarget=0, Down → tiltTarget=100, My → tiltTarget=currentTiltPos
    struct Case {
        somfy_commands cmd;
        float currentTilt;
        float expectedTiltTarget;
    };
    const Case cases[] = {
        {somfy_commands::Up, 60.0f, 0.0f},
        {somfy_commands::Down, 60.0f, 100.0f},
        {somfy_commands::My, 33.0f, 33.0f},
    };
    shade.tiltType = tilt_types::integrated;
    for (auto &c : cases) {
        shade.currentTiltPos = c.currentTilt;
        shade.sendTiltCommand(c.cmd);
        EXPECT_EQ(shade.getLastFrame().cmd, c.cmd) << "cmd=" << (int)c.cmd;
        EXPECT_FLOAT_EQ(shade.getTiltTarget(), c.expectedTiltTarget) << "cmd=" << (int)c.cmd;
    }
}

TEST_F(CommandTransmitterTest, SendTiltCommand_TiltMotor_AllCmdsUseTiltRepeats)
{
    shade.tiltType = tilt_types::tiltmotor;
    for (auto cmd : {somfy_commands::Up, somfy_commands::Down, somfy_commands::My}) {
        shade.sendTiltCommand(cmd);
        EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS) << "cmd=" << (int)cmd;
    }
}
