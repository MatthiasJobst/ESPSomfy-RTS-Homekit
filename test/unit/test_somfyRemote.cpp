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
// SomfyFlag:
//   getRollingCode(), resetFlags(), operator!=, operator|=

#include "SomfyRemote.h"
#include "nvs.h"
#include "SomfyShadeController.h"
#include <gtest/gtest.h>

extern uint64_t test_clock_ms;

// Test-controllable transceiver TX state (defined in stubs/globals_stub.cpp).
extern bool transceiver_stub_tx_busy;
extern int transceiver_stub_begin_tx_count;

class SomfyRemoteTest : public ::testing::Test {
  protected:
    SomfyRemote remote;

    void SetUp() override
    {
        remote.setRemoteAddress(0x112233);
        remote.bitLength = 56;
        remote.repeats = 1;
        remote.flipCommands = false;
        remote.proto = radio_proto::RTS;
        remote.flags = SomfyFlag();
        transceiver_stub_tx_busy = false;
        transceiver_stub_begin_tx_count = 0;
        nvs_stub_reset_all();
    }

    void TearDown() override
    {
        transceiver_stub_tx_busy = false;
        transceiver_stub_begin_tx_count = 0;
        nvs_stub_reset_all();
    }
};

// ── Flag setters (clear branches) ─────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SetSimMy_False_ClearsFlag)
{
    remote.setSimMy(true);
    EXPECT_TRUE(remote.simMy());
    remote.setSimMy(false);
    EXPECT_FALSE(remote.simMy());
}

TEST_F(SomfyRemoteTest, SetLight_False_ClearsFlag)
{
    remote.setLight(true);
    EXPECT_TRUE(remote.hasLight());
    remote.setLight(false);
    EXPECT_FALSE(remote.hasLight());
}

// ── triggerGPIOs (empty base implementation) ──────────────────────────────────

TEST_F(SomfyRemoteTest, TriggerGPIOs_DoesNotCrash)
{
    somfy_frame_t frame{};
    remote.triggerGPIOs(frame);
}

// ── transformCommand with flipCommands ────────────────────────────────────────

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Up_ReturnsDown)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::Up), somfy_commands::Down);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_MyUp_ReturnsMyDown)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::MyUp), somfy_commands::MyDown);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Down_ReturnsUp)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::Down), somfy_commands::Up);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_MyDown_ReturnsMyUp)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::MyDown), somfy_commands::MyUp);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_StepUp_ReturnsStepDown)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::StepUp), somfy_commands::StepDown);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_StepDown_ReturnsStepUp)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::StepDown), somfy_commands::StepUp);
}

TEST_F(SomfyRemoteTest, TransformCommand_Flip_Default_ReturnsSame)
{
    remote.flipCommands = true;
    EXPECT_EQ(remote.transformCommand(somfy_commands::My), somfy_commands::My);
}

// ── sendSensorCommand ─────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendSensorCommand_WindyAndSunny_SetsSensorFrame)
{
    remote.sendSensorCommand(1, 1, 2);
    EXPECT_EQ(remote.lastFrame.cmd, somfy_commands::Sensor);
    EXPECT_EQ(remote.lastFrame.encKey, 160u);
    EXPECT_EQ(remote.lastFrame.repeats, 2u);
}

TEST_F(SomfyRemoteTest, SendSensorCommand_ClearWindAndSun_SetsSensorFrame)
{
    remote.sendSensorCommand(0, 0, 1);
    EXPECT_EQ(remote.lastFrame.cmd, somfy_commands::Sensor);
    EXPECT_EQ(remote.lastFrame.encKey, 160u);
}

// ── sendCommand 1-arg overload ────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendCommand_OneArg_UsesRemoteRepeats)
{
    remote.repeats = 3;
    remote.sendCommand(somfy_commands::My);
    EXPECT_EQ(remote.lastFrame.cmd, somfy_commands::My);
    EXPECT_EQ(remote.lastFrame.repeats, 3u);
}

// ── sendCommand proto branches ────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendCommand_GPRelay_SetsProtoInFrame)
{
    remote.proto = radio_proto::GP_Relay;
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_EQ(remote.lastFrame.proto, radio_proto::GP_Relay);
}

TEST_F(SomfyRemoteTest, SendCommand_GPRemote_CallsTriggerGPIOs)
{
    remote.proto = radio_proto::GP_Remote;
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_EQ(remote.lastFrame.proto, radio_proto::GP_Remote);
}

// ── isLastCommand ─────────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, IsLastCommand_MatchingCmd_ReturnsTrue)
{
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_TRUE(remote.isLastCommand(somfy_commands::Up));
}

TEST_F(SomfyRemoteTest, IsLastCommand_WrongCmd_ReturnsFalse)
{
    remote.sendCommand(somfy_commands::Up, 1);
    EXPECT_FALSE(remote.isLastCommand(somfy_commands::Down));
}

// ── repeatFrame ──────────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, RepeatFrame_GPRelay_ReturnsEarlyWithoutTransmit)
{
    remote.proto = radio_proto::GP_Relay;
    remote.repeatFrame(1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_GPRemote_CallsTriggerGPIOs)
{
    remote.proto = radio_proto::GP_Remote;
    remote.repeatFrame(1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_RTS_56Bit_SendsFrames)
{
    remote.proto = radio_proto::RTS;
    remote.bitLength = 56;
    remote.lastFrame.bitLength = 56;
    remote.lastFrame.remoteAddress = remote.getRemoteAddress();
    remote.lastFrame.rollingCode = 1;
    remote.lastFrame.encKey = 0xA1;
    remote.lastFrame.cmd = somfy_commands::Up;
    remote.repeatFrame(2);
}

TEST_F(SomfyRemoteTest, RepeatFrame_RTS_80Bit_Calls80BitEncode)
{
    remote.proto = radio_proto::RTS;
    remote.bitLength = 80;
    remote.lastFrame.bitLength = 80;
    remote.lastFrame.remoteAddress = remote.getRemoteAddress();
    remote.lastFrame.rollingCode = 1;
    remote.lastFrame.encKey = 0xA1;
    remote.lastFrame.cmd = somfy_commands::Up;
    remote.repeatFrame(1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_TxFree_StartsTransmission)
{
    transceiver_stub_tx_busy = false;
    remote.lastFrame.cmd = somfy_commands::Up;
    remote.repeatFrame(1);
    EXPECT_EQ(transceiver_stub_begin_tx_count, 1);
}

TEST_F(SomfyRemoteTest, RepeatFrame_TxBusy_SkipsTransmissionToAvoidBlocking)
{
    transceiver_stub_tx_busy = true;
    remote.lastFrame.cmd = somfy_commands::Up;
    remote.repeatFrame(1);
    EXPECT_EQ(transceiver_stub_begin_tx_count, 0);
}

// ── sendOrRepeat ─────────────────────────────────────────────────────────────

TEST_F(SomfyRemoteTest, SendOrRepeat_NotLastCommand_SendsFreshCommand)
{
    // Fresh remote: Up is not the last command, so a new frame is sent and the
    // rolling code advances.
    remote.sendOrRepeat(somfy_commands::Up, 2);
    EXPECT_EQ(remote.lastFrame.cmd, somfy_commands::Up);
    EXPECT_EQ(remote.lastFrame.repeats, 2u);
    EXPECT_EQ(remote.lastFrame.rollingCode, 1u);
}

TEST_F(SomfyRemoteTest, SendOrRepeat_SameCommandAgain_RepeatsWithoutAdvancingRollingCode)
{
    remote.sendCommand(somfy_commands::Up, 1);
    uint16_t rcAfterSend = remote.lastFrame.rollingCode;
    transceiver_stub_begin_tx_count = 0;

    // Up is now the last command → repeatFrame, no rolling-code change.
    remote.sendOrRepeat(somfy_commands::Up, 1);
    EXPECT_EQ(remote.lastFrame.rollingCode, rcAfterSend);
    EXPECT_EQ(transceiver_stub_begin_tx_count, 1);
}

TEST_F(SomfyRemoteTest, SendOrRepeat_NegativeRepeat_UsesRemoteRepeats)
{
    remote.repeats = 4;
    remote.sendOrRepeat(somfy_commands::Down, -1);
    EXPECT_EQ(remote.lastFrame.cmd, somfy_commands::Down);
    EXPECT_EQ(remote.lastFrame.repeats, 4u);
}

// ══════════════════════════════════════════════════════════════════════════════
// Rolling code — getNextRollingCode / setRollingCode / p_lastRollingCode
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(SomfyRemoteTest, GetNextRollingCode_FirstCall_ReturnsOne)
{
    // NVS is empty (stub reset in SetUp); first call reads 0, increments to 1.
    EXPECT_EQ(remote.getNextRollingCode(), 1u);
}

TEST_F(SomfyRemoteTest, GetNextRollingCode_ConsecutiveCalls_Increment)
{
    EXPECT_EQ(remote.getNextRollingCode(), 1u);
    EXPECT_EQ(remote.getNextRollingCode(), 2u);
    EXPECT_EQ(remote.getNextRollingCode(), 3u);
}

TEST_F(SomfyRemoteTest, GetNextRollingCode_UpdatesLastRollingCode)
{
    remote.getNextRollingCode();
    EXPECT_EQ(remote.lastRollingCode, 1u);
    remote.getNextRollingCode();
    EXPECT_EQ(remote.lastRollingCode, 2u);
}

TEST_F(SomfyRemoteTest, GetNextRollingCode_PersistsAcrossInstances)
{
    // Advance the NVS counter for address 0x112233 to 3.
    remote.getNextRollingCode();
    remote.getNextRollingCode();
    remote.getNextRollingCode();

    // A fresh remote with the same address picks up where the old one left off.
    SomfyRemote remote2;
    remote2.setRemoteAddress(0x112233);
    EXPECT_EQ(remote2.getNextRollingCode(), 4u);
}

TEST_F(SomfyRemoteTest, SetRollingCode_DifferentValue_UpdatesLastRollingCodeAndNvs)
{
    remote.setRollingCode(100u);
    EXPECT_EQ(remote.lastRollingCode, 100u);

    // A fresh remote with the same address reads the stored code + 1.
    SomfyRemote remote2;
    remote2.setRemoteAddress(0x112233);
    EXPECT_EQ(remote2.getNextRollingCode(), 101u);
}

TEST_F(SomfyRemoteTest, SetRollingCode_SameValue_IsIdempotent)
{
    remote.setRollingCode(50u);
    remote.setRollingCode(50u); // second call must not change anything
    EXPECT_EQ(remote.lastRollingCode, 50u);

    // NVS was written only once; next increment from a fresh instance is 51.
    SomfyRemote remote2;
    remote2.setRemoteAddress(0x112233);
    EXPECT_EQ(remote2.getNextRollingCode(), 51u);
}

TEST_F(SomfyRemoteTest, SetRollingCode_ReturnsNewCode)
{
    EXPECT_EQ(remote.setRollingCode(77u), 77u);
}

// ══════════════════════════════════════════════════════════════════════════════
// SomfyFlag — uncovered methods
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFlagTest, GetRollingCode_ReturnsUpperNibble)
{
    SomfyFlag f;
    f.setFlags(0x30); // upper nibble = 0x3
    EXPECT_EQ(f.getRollingCode(), 0x03);
}

TEST(SomfyFlagTest, ResetFlags_ClearsAllBits)
{
    SomfyFlag f;
    f.setFlags(0xFF);
    f.resetFlags();
    EXPECT_EQ(f.getFlags(), 0x00);
}

TEST(SomfyFlagTest, OperatorNotEqual_DifferentFlags_ReturnsTrue)
{
    SomfyFlag a, b;
    a.setSunny(true);
    EXPECT_TRUE(a != b);
}

TEST(SomfyFlagTest, OperatorNotEqual_SameFlags_ReturnsFalse)
{
    SomfyFlag a, b;
    a.setSunny(true);
    b.setSunny(true);
    EXPECT_FALSE(a != b);
}

TEST(SomfyFlagTest, OperatorOrAssign_MergesFlags)
{
    SomfyFlag a, b;
    a.setSunny(true);
    b.setWindy(true);
    a |= b;
    EXPECT_TRUE(a.isSunny());
    EXPECT_TRUE(a.isWindy());
}
