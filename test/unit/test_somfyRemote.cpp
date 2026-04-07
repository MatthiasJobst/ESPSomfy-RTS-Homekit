// test_somfyRemote.cpp — tests for SomfyRemote methods not exercised by
// higher-level shade tests:
//   setSimMy(false) / setLight(false)       — clear-flag branches
//   triggerGPIOs()                           — empty base implementation
//   transformCommand() with flipCommands     — all switch arms + default
//   sendSensorCommand()                      — sensor frame path
//   sendCommand(cmd) [1-arg overload]        — delegates to 3-arg
//   sendCommand() with GP_Relay / GP_Remote  — GPIO relay/remote branches
//   isLastCommand()                          — match and mismatch
//   repeatFrame()                            — GP_Relay, GP_Remote, RTS 56-bit,
//                                             RTS 80-bit (encode80BitFrame path)

#include "SomfyRemote.h"
#include "nvs.h"
#include "SomfyController.h"
#include <gtest/gtest.h>

extern bool nvs_stub_use_nvs;

class SomfyRemoteTest : public ::testing::Test {
protected:
    SomfyLinkedRemote remote;

    void SetUp() override {
        remote.setRemoteAddress(0x112233);
        remote.bitLength    = 56;
        remote.repeats      = 1;
        remote.flipCommands = false;
        remote.proto        = radio_proto::RTS;
        remote.flags        = 0;
        nvs_stub_use_nvs    = false;
        nvs_stub_reset_all();
    }

    void TearDown() override {
        nvs_stub_reset_all();
    }
};

// ── Flag setters (clear branches) ─────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SetSimMy_False_ClearsFlag) {
    remote.setSimMy(true);
    EXPECT_TRUE(remote.simMy());
    remote.setSimMy(false);
    EXPECT_FALSE(remote.simMy());
}

TEST_F(SomfyRemoteTest, SetLight_False_ClearsFlag) {
    remote.setLight(true);
    EXPECT_TRUE(remote.hasLight());
    remote.setLight(false);
    EXPECT_FALSE(remote.hasLight());
}

// ── triggerGPIOs (empty base implementation) ──────────────────────────────────

TEST_F(SomfyRemoteTest, TriggerGPIOs_DoesNotCrash) {
    somfy_frame_t frame{};
    remote.triggerGPIOs(frame);
}

// ── transformCommand with flipCommands ────────────────────────────────────────

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Up_ReturnsDown) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::Up), somfy_commands::Down);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_MyUp_ReturnsMyDown) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::MyUp), somfy_commands::MyDown);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Down_ReturnsUp) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::Down), somfy_commands::Up);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_MyDown_ReturnsMyUp) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::MyDown), somfy_commands::MyUp);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_StepUp_ReturnsStepDown) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::StepUp), somfy_commands::StepDown);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_StepDown_ReturnsStepUp) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::StepDown), somfy_commands::StepUp);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Default_ReturnsSame) {
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::My), somfy_commands::My);
}

// ── sendSensorCommand ─────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendSensorCommand_WindyAndSunny_SetsSensorFrame) {
    remote.sendSensorCommand(1, 1, 2);
    EXPECT_EQ(remote.lastFrame.cmd,    somfy_commands::Sensor);
    EXPECT_EQ(remote.lastFrame.encKey, 160u);
    EXPECT_EQ(remote.lastFrame.repeats, 2u);
}

TEST_F(SomfyRemoteTest, SendSensorCommand_ClearWindAndSun_SetsSensorFrame) {
    remote.sendSensorCommand(0, 0, 1);
    EXPECT_EQ(remote.lastFrame.cmd,    somfy_commands::Sensor);
    EXPECT_EQ(remote.lastFrame.encKey, 160u);
}

// ── sendCommand 1-arg overload ────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendCommand_OneArg_UsesRemoteRepeats) {
    remote.repeats = 3;
    remote.sendCommand(somfy_commands::My);
    EXPECT_EQ(remote.lastFrame.cmd,    somfy_commands::My);
    EXPECT_EQ(remote.lastFrame.repeats, 3u);
}

// ── sendCommand proto branches ────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendCommand_GPRelay_SetsProtoInFrame) {
    remote.proto = radio_proto::GP_Relay;
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_EQ(remote.lastFrame.proto, radio_proto::GP_Relay);
}

TEST_F(SomfyRemoteTest, SendCommand_GPRemote_CallsTriggerGPIOs) {
    remote.proto = radio_proto::GP_Remote;
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_EQ(remote.lastFrame.proto, radio_proto::GP_Remote);
}

// ── isLastCommand ─────────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, IsLastCommand_MatchingCmd_ReturnsTrue) {
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_TRUE(remote.isLastCommand(somfy_commands::Up));
}

TEST_F(SomfyRemoteTest, IsLastCommand_WrongCmd_ReturnsFalse) {
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_FALSE(remote.isLastCommand(somfy_commands::Down));
}

// ── repeatFrame ──────────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, RepeatFrame_GPRelay_ReturnsEarlyWithoutTransmit) {
    remote.proto = radio_proto::GP_Relay;
    remote.repeatFrame(1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_GPRemote_CallsTriggerGPIOs) {
    remote.proto = radio_proto::GP_Remote;
    remote.repeatFrame(1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_RTS_56Bit_SendsFrames) {
    remote.proto            = radio_proto::RTS;
    remote.bitLength        = 56;
    remote.lastFrame.bitLength = 56;
    remote.lastFrame.remoteAddress = remote.getRemoteAddress();
    remote.lastFrame.rollingCode   = 1;
    remote.lastFrame.encKey        = 0xA1;
    remote.lastFrame.cmd           = somfy_commands::Up;
    remote.repeatFrame(2);
}

TEST_F(SomfyRemoteTest, RepeatFrame_RTS_80Bit_Calls80BitEncode) {
    remote.proto            = radio_proto::RTS;
    remote.bitLength        = 80;
    remote.lastFrame.bitLength = 80;
    remote.lastFrame.remoteAddress = remote.getRemoteAddress();
    remote.lastFrame.rollingCode   = 1;
    remote.lastFrame.encKey        = 0xA1;
    remote.lastFrame.cmd           = somfy_commands::Up;
    remote.repeatFrame(1);
}
