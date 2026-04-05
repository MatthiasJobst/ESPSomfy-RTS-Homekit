// test_commandTransmitter.cpp — tests for the CommandTransmitter
// methods of SomfyShade:
//   sendCommand()  — all cmd branches: Up/Down (normal + euromode + tiltonly +
//                    integrated), My (toggle/drycontact/drycontact2/idle/moving),
//                    Toggle (non-80-bit / 80-bit), Prog (toggle shade), fallthrough
//   sendTiltCommand() — Up / Down / My
//   linkRemote()   — already linked (updates rolling code), new slot, NVS write,
//                    all slots full (returns false)
//   unlinkRemote() — found (NVS write), not found (returns false)
//
// SomfyRemote::sendCommand() is the real implementation; it writes lastFrame
// which is observable via shade.getLastFrame().

#include "TestableShade.h"
#include "nvs.h"
#include "MQTT.h"
#include "SomfyController.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern bool             nvs_stub_use_nvs;
extern SomfyShadeController somfy;
extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;

using ::testing::AnyNumber;
using ::testing::_;

// ── fixture ───────────────────────────────────────────────────────────────

class CommandTransmitterTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0x112233);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.currentPos     = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target         = 50.0f;
        shade.tiltTarget     = 50.0f;
        shade.direction      = 0;
        shade.tiltDirection  = 0;
        shade.setFlipPosition(false);
        shade.setProto(radio_proto::RTS);
        // repeats = 1; bitLength set by SomfyRemote default

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        nvs_stub_use_nvs = false;
        nvs_stub_reset_all();
        mqtt_connected_flag = false;
        mqtt_published.clear();
    }

    void TearDown() override {
        nvs_stub_use_nvs = false;
        nvs_stub_reset_all();
    }

    // Make the shade look moving (direction != 0) so isIdle() == false
    void setMoving() {
        shade.direction = 1;
        shade.target    = 100.0f;   // not at target
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. sendCommand() — Up
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Up_Normal_SetsTargetZero) {
    shade.tiltType = tilt_types::none;
    shade.sendCommand(somfy_commands::Up);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Up);
}

TEST_F(CommandTransmitterTest, SendCommand_Up_Integrated_SetsTiltTargetZero) {
    shade.tiltType = tilt_types::integrated;
    shade.sendCommand(somfy_commands::Up);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_Up_TiltOnly_SetsPositionAndTiltTargets) {
    shade.tiltType   = tilt_types::tiltonly;
    shade.currentPos = 50.0f;
    shade.sendCommand(somfy_commands::Up);
    // tiltonly Up: p_target(100), p_tiltTarget(0), p_currentPos(100)
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_Up_Euromode_UsesTiltRepeats) {
    shade.tiltType = tilt_types::euromode;
    shade.sendCommand(somfy_commands::Up);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
    EXPECT_FLOAT_EQ(shade.getTarget(), 0.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. sendCommand() — Down
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Down_Normal_SetsTargetHundred) {
    shade.tiltType = tilt_types::none;
    shade.sendCommand(somfy_commands::Down);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Down);
}

TEST_F(CommandTransmitterTest, SendCommand_Down_Integrated_SetsTiltTargetHundred) {
    shade.tiltType = tilt_types::integrated;
    shade.sendCommand(somfy_commands::Down);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_Down_TiltOnly_SetsAllToHundred) {
    shade.tiltType   = tilt_types::tiltonly;
    shade.currentPos = 50.0f;
    shade.sendCommand(somfy_commands::Down);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_Down_Euromode_UsesTiltRepeats) {
    shade.tiltType = tilt_types::euromode;
    shade.sendCommand(somfy_commands::Down);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
    EXPECT_FLOAT_EQ(shade.getTarget(), 100.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// C. sendCommand() — My
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_My_Toggle_SendsDirectly) {
    shade.shadeType = shade_types::garage1;   // isToggle() == true
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_My_DryContact_SendsDirectly) {
    shade.shadeType = shade_types::drycontact;
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_My_DryContact2_ReturnsEarly) {
    shade.shadeType = shade_types::drycontact2;
    // drycontact2 My is a no-op — frame should NOT be updated
    uint16_t codeBefore = shade.getLastFrame().rollingCode;
    shade.sendCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().rollingCode, codeBefore);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Idle_MovesToMyPosition) {
    // isIdle() == true: at target, direction == 0
    shade.shadeType = shade_types::roller;
    shade.tiltType  = tilt_types::none;
    shade.target    = shade.currentPos;   // at target
    shade.targetSequencer.myPos     = 25.0f;
    shade.sendCommand(somfy_commands::My);
    // moveToMyPosition() sets target to myPos
    EXPECT_FLOAT_EQ(shade.getTarget(), 25.0f);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Moving_StopsAndSetsTarget) {
    setMoving();
    shade.tiltType = tilt_types::none;
    shade.sendCommand(somfy_commands::My);
    // Stops: p_target(currentPos), p_tiltTarget(currentTiltPos)
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
    EXPECT_FLOAT_EQ(shade.getTarget(), shade.currentPos);
}

TEST_F(CommandTransmitterTest, SendCommand_My_Moving_TiltOnly_DoesNotSetTarget) {
    setMoving();
    shade.tiltType = tilt_types::tiltonly;
    shade.sendCommand(somfy_commands::My);
    // tiltonly: p_target is skipped, only p_tiltTarget is set
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), shade.currentTiltPos);
}

// ══════════════════════════════════════════════════════════════════════════════
// D. sendCommand() — Toggle and Prog
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Toggle_Non80Bit_SendsMyCommand) {
    shade.bitLength = 56;   // != 80
    shade.sendCommand(somfy_commands::Toggle);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
}

TEST_F(CommandTransmitterTest, SendCommand_Toggle_80Bit_SendsToggleCommand) {
    shade.bitLength = 80;
    shade.sendCommand(somfy_commands::Toggle);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Toggle);
}

TEST_F(CommandTransmitterTest, SendCommand_Prog_IsToggle_SendsToggleCommand) {
    shade.shadeType = shade_types::garage1;   // isToggle() == true
    shade.sendCommand(somfy_commands::Prog);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Toggle);
}

TEST_F(CommandTransmitterTest, SendCommand_Prog_NotToggle_SendsProgCommand) {
    shade.shadeType = shade_types::roller;    // isToggle() == false
    shade.sendCommand(somfy_commands::Prog);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Prog);
}

// ══════════════════════════════════════════════════════════════════════════════
// E. sendCommand() — fallthrough (Stop)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendCommand_Stop_Fallthrough_SendsStopCommand) {
    shade.sendCommand(somfy_commands::Stop);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Stop);
}

// ══════════════════════════════════════════════════════════════════════════════
// F. sendTiltCommand()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, SendTiltCommand_Up_SetsTiltTargetZero) {
    shade.tiltType   = tilt_types::integrated;
    shade.currentTiltPos = 60.0f;
    shade.sendTiltCommand(somfy_commands::Up);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Up);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 0.0f);
}

TEST_F(CommandTransmitterTest, SendTiltCommand_Up_TiltMotor_UsesTiltRepeats) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.sendTiltCommand(somfy_commands::Up);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
}

TEST_F(CommandTransmitterTest, SendTiltCommand_Down_SetsTiltTargetHundred) {
    shade.tiltType = tilt_types::integrated;
    shade.sendTiltCommand(somfy_commands::Down);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::Down);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 100.0f);
}

TEST_F(CommandTransmitterTest, SendTiltCommand_Down_TiltMotor_UsesTiltRepeats) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.sendTiltCommand(somfy_commands::Down);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
}

TEST_F(CommandTransmitterTest, SendTiltCommand_My_SetsTiltTargetToCurrentTiltPos) {
    shade.tiltType       = tilt_types::integrated;
    shade.currentTiltPos = 33.0f;
    shade.sendTiltCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().cmd, somfy_commands::My);
    EXPECT_FLOAT_EQ(shade.getTiltTarget(), 33.0f);
}

TEST_F(CommandTransmitterTest, SendTiltCommand_My_TiltMotor_UsesTiltRepeats) {
    shade.tiltType = tilt_types::tiltmotor;
    shade.sendTiltCommand(somfy_commands::My);
    EXPECT_EQ(shade.getLastFrame().repeats, TILT_REPEATS);
}

// ══════════════════════════════════════════════════════════════════════════════
// G. linkRemote()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, LinkRemote_NewSlot_ReturnsTrueAndStoresAddress) {
    bool result = shade.linkRemote(0xABCD01, 10);
    EXPECT_TRUE(result);
    EXPECT_EQ(shade.getLinkedRemote(0).getRemoteAddress(), 0xABCD01u);
}

TEST_F(CommandTransmitterTest, LinkRemote_AlreadyLinked_UpdatesRollingCodeAndReturnsTrue) {
    shade.linkRemote(0xABCD01, 5);
    bool result = shade.linkRemote(0xABCD01, 99);
    EXPECT_TRUE(result);
    // Rolling code is stored inside the remote — just confirm it didn't claim a second slot
    EXPECT_EQ(shade.getLinkedRemote(1).getRemoteAddress(), 0u);
}

TEST_F(CommandTransmitterTest, LinkRemote_AllSlotsFull_ReturnsFalse) {
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
        shade.getLinkedRemote(i).setRemoteAddress(0x100000 + i);
    bool result = shade.linkRemote(0xDEADBE, 0);
    EXPECT_FALSE(result);
}

TEST_F(CommandTransmitterTest, LinkRemote_UseNVSTrue_WritesLinkedAddrToNVS) {
    nvs_stub_use_nvs = true;
    shade.linkRemote(0xCAFE01, 0);
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("linkedAddr"), 1u);
}

TEST_F(CommandTransmitterTest, LinkRemote_UseNVSFalse_DoesNotWriteNVS) {
    nvs_stub_use_nvs = false;
    shade.linkRemote(0xCAFE02, 0);
    EXPECT_TRUE(nvs_ns_stores.empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// H. unlinkRemote()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandTransmitterTest, UnlinkRemote_NotFound_ReturnsFalse) {
    bool result = shade.unlinkRemote(0xDEAD00);
    EXPECT_FALSE(result);
}

TEST_F(CommandTransmitterTest, UnlinkRemote_Found_ClearsSlotAndReturnsTrue) {
    shade.getLinkedRemote(0).setRemoteAddress(0xBEEF01);
    bool result = shade.unlinkRemote(0xBEEF01);
    EXPECT_TRUE(result);
    EXPECT_EQ(shade.getLinkedRemote(0).getRemoteAddress(), 0u);
}

TEST_F(CommandTransmitterTest, UnlinkRemote_UseNVSTrue_WritesLinkedAddrToNVS) {
    nvs_stub_use_nvs = true;
    shade.getLinkedRemote(0).setRemoteAddress(0xBEEF02);
    shade.getLinkedRemote(1).setRemoteAddress(0xBEEF03);
    shade.unlinkRemote(0xBEEF02);
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("linkedAddr"), 1u);
}

TEST_F(CommandTransmitterTest, UnlinkRemote_UseNVSFalse_DoesNotWriteNVS) {
    nvs_stub_use_nvs = false;
    shade.getLinkedRemote(0).setRemoteAddress(0xBEEF04);
    shade.unlinkRemote(0xBEEF04);
    EXPECT_TRUE(nvs_ns_stores.empty());
}
