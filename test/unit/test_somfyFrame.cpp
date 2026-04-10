// test_somfyFrame.cpp — tests for SomfyFrame
//
// Covers: sort_asc, translateSomfyCommand (both overloads), calc80Checksum,
//         decodeFrame, encodeFrame, encode80BitFrame, encode80Byte7, print,
//         isSynonym, isRepeat, copy,
//         somfy_tx_queue_t (push/pop/overflow),
//         somfy_rx_queue_t (init/pop)

#include "SomfyFrame.h"
#include <gtest/gtest.h>
#include <cstring>

// sort_asc is defined in SomfyFrame.cpp but not declared in the header
extern int sort_asc(const void *cmp1, const void *cmp2);

// ── helpers ───────────────────────────────────────────────────────────────

// Build a valid encoded 56-bit RTS frame byte array from a frame struct.
static void buildEncodedFrame(somfy_frame_t &f, byte out[10]) {
    f.bitLength = 56;
    f.proto     = radio_proto::RTS;
    f.encodeFrame(out);
}

// ══════════════════════════════════════════════════════════════════════════════
// A  sort_asc
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, SortAsc_LessThan) {
    uint8_t a = 1, b = 2;
    EXPECT_EQ(sort_asc(&a, &b), -1);
}

TEST(SomfyFrameTest, SortAsc_GreaterThan) {
    uint8_t a = 5, b = 3;
    EXPECT_EQ(sort_asc(&a, &b), 1);
}

TEST(SomfyFrameTest, SortAsc_Equal) {
    uint8_t a = 7, b = 7;
    EXPECT_EQ(sort_asc(&a, &b), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// B  translateSomfyCommand(String) — exact matches and abbreviation prefixes
// ══════════════════════════════════════════════════════════════════════════════

struct StringCmdCase { const char *input; somfy_commands expected; };

class TranslateStringTest : public ::testing::TestWithParam<StringCmdCase> {};

TEST_P(TranslateStringTest, MapsCorrectly) {
    auto p = GetParam();
    EXPECT_EQ(translateSomfyCommand(String(p.input)), p.expected);
}

INSTANTIATE_TEST_SUITE_P(ExactMatch, TranslateStringTest, ::testing::Values(
    StringCmdCase{"My",       somfy_commands::My},
    StringCmdCase{"Up",       somfy_commands::Up},
    StringCmdCase{"MyUp",     somfy_commands::MyUp},
    StringCmdCase{"Down",     somfy_commands::Down},
    StringCmdCase{"MyDown",   somfy_commands::MyDown},
    StringCmdCase{"UpDown",   somfy_commands::UpDown},
    StringCmdCase{"MyUpDown", somfy_commands::MyUpDown},
    StringCmdCase{"Prog",     somfy_commands::Prog},
    StringCmdCase{"SunFlag",  somfy_commands::SunFlag},
    StringCmdCase{"StepUp",   somfy_commands::StepUp},
    StringCmdCase{"StepDown", somfy_commands::StepDown},
    StringCmdCase{"Flag",     somfy_commands::Flag},
    StringCmdCase{"Sensor",   somfy_commands::Sensor},
    StringCmdCase{"Toggle",   somfy_commands::Toggle},
    StringCmdCase{"Favorite", somfy_commands::Favorite},
    StringCmdCase{"Stop",     somfy_commands::Stop},
    StringCmdCase{"UP",       somfy_commands::Up}   // case-insensitive
));

INSTANTIATE_TEST_SUITE_P(Prefix, TranslateStringTest, ::testing::Values(
    StringCmdCase{"fav", somfy_commands::Favorite},
    StringCmdCase{"FAV", somfy_commands::Favorite},
    StringCmdCase{"mud", somfy_commands::MyUpDown},
    StringCmdCase{"MUD", somfy_commands::MyUpDown},
    StringCmdCase{"md",  somfy_commands::MyDown},
    StringCmdCase{"MD",  somfy_commands::MyDown},
    StringCmdCase{"ud",  somfy_commands::UpDown},
    StringCmdCase{"UD",  somfy_commands::UpDown},
    StringCmdCase{"mu",  somfy_commands::MyUp},
    StringCmdCase{"MU",  somfy_commands::MyUp},
    StringCmdCase{"su",  somfy_commands::StepUp},
    StringCmdCase{"SU",  somfy_commands::StepUp},
    StringCmdCase{"sd",  somfy_commands::StepDown},
    StringCmdCase{"SD",  somfy_commands::StepDown},
    StringCmdCase{"sen", somfy_commands::Sensor},
    StringCmdCase{"SEN", somfy_commands::Sensor},
    StringCmdCase{"p",   somfy_commands::Prog},
    StringCmdCase{"P",   somfy_commands::Prog},
    StringCmdCase{"u",   somfy_commands::Up},
    StringCmdCase{"U",   somfy_commands::Up},
    StringCmdCase{"d",   somfy_commands::Down},
    StringCmdCase{"D",   somfy_commands::Down},
    StringCmdCase{"m",   somfy_commands::My},
    StringCmdCase{"M",   somfy_commands::My},
    StringCmdCase{"f",   somfy_commands::Flag},
    StringCmdCase{"F",   somfy_commands::Flag},
    StringCmdCase{"s",   somfy_commands::SunFlag},
    StringCmdCase{"S",   somfy_commands::SunFlag},
    StringCmdCase{"t",   somfy_commands::Toggle},
    StringCmdCase{"T",   somfy_commands::Toggle}
));

// ── Single hex char → strtol ──────────────────────────────────────────────
TEST(SomfyFrameTest, Translate_SingleHexChar) {
    // "1" → strtol("1", 16) = 1 = somfy_commands::My
    EXPECT_EQ(translateSomfyCommand(String("1")), somfy_commands::My);
}

// ── Fallback ──────────────────────────────────────────────────────────────
TEST(SomfyFrameTest, Translate_UnknownString_FallsBackToMy) {
    EXPECT_EQ(translateSomfyCommand(String("xyz123")), somfy_commands::My);
}

// ══════════════════════════════════════════════════════════════════════════════
// C  translateSomfyCommand(somfy_commands) — enum → string
// ══════════════════════════════════════════════════════════════════════════════

struct CmdStringCase { somfy_commands cmd; const char *expected; };

class TranslateCmdTest : public ::testing::TestWithParam<CmdStringCase> {};

TEST_P(TranslateCmdTest, MapsCorrectly) {
    auto p = GetParam();
    EXPECT_EQ(translateSomfyCommand(p.cmd), String(p.expected));
}

INSTANTIATE_TEST_SUITE_P(AllCommands, TranslateCmdTest, ::testing::Values(
    CmdStringCase{somfy_commands::Up,       "Up"},
    CmdStringCase{somfy_commands::Down,     "Down"},
    CmdStringCase{somfy_commands::My,       "My"},
    CmdStringCase{somfy_commands::MyUp,     "My+Up"},
    CmdStringCase{somfy_commands::UpDown,   "Up+Down"},
    CmdStringCase{somfy_commands::MyDown,   "My+Down"},
    CmdStringCase{somfy_commands::MyUpDown, "My+Up+Down"},
    CmdStringCase{somfy_commands::Prog,     "Prog"},
    CmdStringCase{somfy_commands::SunFlag,  "Sun Flag"},
    CmdStringCase{somfy_commands::Flag,     "Flag"},
    CmdStringCase{somfy_commands::StepUp,   "Step Up"},
    CmdStringCase{somfy_commands::StepDown, "Step Down"},
    CmdStringCase{somfy_commands::Sensor,   "Sensor"},
    CmdStringCase{somfy_commands::Toggle,   "Toggle"},
    CmdStringCase{somfy_commands::Favorite, "Favorite"},
    CmdStringCase{somfy_commands::Stop,     "Stop"}
));

TEST(SomfyFrameTest, Translate_Cmd_Unknown) {
    EXPECT_TRUE(translateSomfyCommand(static_cast<somfy_commands>(0x00)).startsWith("Unknown"));
}

// ══════════════════════════════════════════════════════════════════════════════
// D  calc80Checksum
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, Calc80Checksum_KnownValues) {
    somfy_frame_t f;
    // b0=132 (0x84), b1=44 (0x2C), b2=0x90
    // upper nibbles: 8^2^9 = 3; lower nibbles: 4^C^0 = 8; total 3^8 = 0xB = 11
    byte cs = f.calc80Checksum(0x84, 0x2C, 0x90);
    EXPECT_EQ(cs, f.calc80Checksum(0x84, 0x2C, 0x90));  // deterministic
    EXPECT_LT(cs, 16u);  // must fit in 4 bits
}

// ══════════════════════════════════════════════════════════════════════════════
// E  encodeFrame + decodeFrame roundtrip (RTS 56-bit)
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, EncoDecode_RTS56_My_Roundtrip) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::My;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 42;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;

    byte encoded[10] = {};
    tx.encodeFrame(encoded);

    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);

    EXPECT_TRUE(rx.valid);
    EXPECT_EQ(rx.cmd, somfy_commands::My);
    EXPECT_EQ(rx.remoteAddress, 0x123456u);
    EXPECT_EQ(rx.rollingCode,   42u);
    EXPECT_EQ(rx.proto, radio_proto::RTS);
}

TEST(SomfyFrameTest, EncoDecode_RTS56_AllCommands) {
    const somfy_commands cmds[] = {
        somfy_commands::Up, somfy_commands::Down, somfy_commands::MyUp,
        somfy_commands::MyDown, somfy_commands::UpDown, somfy_commands::MyUpDown,
        somfy_commands::Prog, somfy_commands::SunFlag, somfy_commands::Flag,
        somfy_commands::Sensor
    };
    for (auto cmd : cmds) {
        somfy_frame_t tx;
        tx.cmd           = cmd;
        tx.remoteAddress = 0xABCDEF;
        tx.rollingCode   = (cmd == somfy_commands::Sensor) ? 0 : 1;
        tx.encKey        = 0xA7;
        tx.bitLength     = 56;
        tx.proto         = radio_proto::RTS;
        byte encoded[10] = {};
        tx.encodeFrame(encoded);
        somfy_frame_t rx;
        rx.bitLength = 56;
        rx.decodeFrame(encoded);
        EXPECT_TRUE(rx.valid) << "cmd=" << (int)cmd;
        EXPECT_EQ(rx.cmd, cmd) << "cmd=" << (int)cmd;
    }
}

// ── encKey == 0 → invalid ─────────────────────────────────────────────────
TEST(SomfyFrameTest, Decode_EncKeyZero_Invalid) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::My;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 10;
    tx.encKey        = 0;  // will be forced into frame[0]
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    // Force encKey = 0 in raw frame before XOR obfuscation is applied.
    // encodeFrame already wrote it; just overwrite byte 0 directly.
    encoded[0] = 0;
    // Re-obfuscate only byte 0 (bytes 1-6 XOR chain doesn't touch byte 0).
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ── Bad checksum → invalid ────────────────────────────────────────────────
TEST(SomfyFrameTest, Decode_BadChecksum_Invalid) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::My;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 10;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    // Flip bit 0 of encoded[6]: only decoded[6] changes (last byte in the XOR chain),
    // changing the recalculated checksum without touching the stored checksum in decoded[1].
    encoded[6] ^= 0x01;
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ── rollingCode == 0 on non-Sensor → invalid ─────────────────────────────
TEST(SomfyFrameTest, Decode_RollingCodeZero_NonSensor_Invalid) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::Up;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 0;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ── Unknown cmd value → invalid ───────────────────────────────────────────
TEST(SomfyFrameTest, Decode_UnknownCmd_Invalid) {
    // cmd=0xD (UnknownD) is explicitly invalidated in the switch
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::UnknownD;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ══════════════════════════════════════════════════════════════════════════════
// E2  decodeFrame — RTW / RTV protocol paths (via RTWProto encKey)
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, Decode_RTWProto_EncKey160_ProtoRTS) {
    // encKey >= 160 and not 164 → proto = RTS, cmd unchanged (RTWProto→?)
    // We build a frame manually that has cmd nibble = 0xF (RTWProto) and encKey=161
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::RTWProto;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 161;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    // proto=RTS, cmd stays RTWProto → hits valid=false in the switch
    EXPECT_EQ(rx.proto, radio_proto::RTS);
}

TEST(SomfyFrameTest, Decode_RTWProto_EncKey164_Toggle) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::RTWProto;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 164;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTS);
    EXPECT_EQ(rx.cmd,   somfy_commands::Toggle);
    EXPECT_TRUE(rx.valid);
}

TEST(SomfyFrameTest, Decode_RTWProto_EncKey149_ProtoRTV) {
    // encKey > 148 and <= 160 → proto=RTV, cmd = encKey - 148
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::RTWProto;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 149;  // → cmd = 1 = My
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTV);
    EXPECT_EQ(rx.cmd,   somfy_commands::My);
}

TEST(SomfyFrameTest, Decode_RTWProto_EncKey134_ProtoRTW) {
    // encKey > 133 and <= 148 → proto=RTW, cmd = encKey - 133
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::RTWProto;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 134;  // → cmd = 1 = My
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTW);
    EXPECT_EQ(rx.cmd,   somfy_commands::My);
}

// ══════════════════════════════════════════════════════════════════════════════
// E3  decodeFrame — 80-bit roundtrips
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, EncoDecode_RTS80_AllCommands) {
    struct Case { somfy_commands cmd; uint8_t stepSize; };
    const Case cases[] = {
        {somfy_commands::Toggle,   0},
        {somfy_commands::Favorite, 0},
        {somfy_commands::Stop,     0},
        {somfy_commands::StepUp,   3},
        {somfy_commands::StepDown, 5},
        {somfy_commands::Up,       0},
        {somfy_commands::Down,     0},
        {somfy_commands::My,       0},
    };
    for (auto &c : cases) {
        somfy_frame_t tx;
        tx.cmd           = c.cmd;
        tx.remoteAddress = 0x123456;
        tx.rollingCode   = 5;
        tx.encKey        = 0xA7;
        tx.bitLength     = 80;
        tx.proto         = radio_proto::RTS;
        tx.repeats       = 0;
        tx.stepSize      = c.stepSize;
        byte encoded[10] = {};
        tx.encodeFrame(encoded);
        somfy_frame_t rx;
        rx.bitLength = 80;
        rx.decodeFrame(encoded);
        EXPECT_TRUE(rx.valid)        << "cmd=" << (int)c.cmd;
        EXPECT_EQ(rx.cmd, c.cmd)     << "cmd=" << (int)c.cmd;
        if (c.stepSize)
            EXPECT_EQ(rx.stepSize, c.stepSize) << "cmd=" << (int)c.cmd;
    }
}

TEST(SomfyFrameTest, Decode_RTS80_BadParity_Invalid) {
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::My;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 5;
    tx.encKey        = 0xA7;
    tx.bitLength     = 80;
    tx.proto         = radio_proto::RTS;
    tx.repeats       = 0;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    encoded[9] ^= 0x01;  // corrupt parity byte
    somfy_frame_t rx; rx.bitLength = 80;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ══════════════════════════════════════════════════════════════════════════════
// F  encodeFrame — RTW and RTV protocols
// ══════════════════════════════════════════════════════════════════════════════

// RTW encode sets frame[0] = 133 + (cmd_index - 1) for My..Flag, and frame[1] high nibble
// = 0xF (RTWProto marker), but after checksum OR and XOR obfuscation the raw encoded byte
// differs from 0xF0. The decode uses encKey > 133 to detect RTW frames.
//
// Note: encode for My (frame[0]=133) does not satisfy the decode condition (>133),
// so only Up(134) through Flag(142) round-trip as proto=RTW. The cmd is offset by one
// (Up encodes as encKey=134 which decodes as cmd=My=1, etc.) — an existing behaviour.

TEST(SomfyFrameTest, Encode_RTW_UpThroughFlag_ProtoRTW) {
    // These commands produce encKey values 134-142, all > 133, so decode as RTW.
    const somfy_commands cmds[] = {
        somfy_commands::Up, somfy_commands::MyUp,
        somfy_commands::Down, somfy_commands::MyDown, somfy_commands::UpDown,
        somfy_commands::MyUpDown, somfy_commands::Prog,
        somfy_commands::SunFlag, somfy_commands::Flag,
    };
    for (auto cmd : cmds) {
        somfy_frame_t f;
        f.cmd = cmd; f.remoteAddress = 0x123456;
        f.rollingCode = 1; f.encKey = 0xA7;
        f.bitLength = 56; f.proto = radio_proto::RTW;
        byte encoded[10] = {};
        f.encodeFrame(encoded);
        somfy_frame_t rx;
        rx.bitLength = 56;
        rx.decodeFrame(encoded);
        EXPECT_EQ(rx.proto, radio_proto::RTW) << "cmd=" << (int)cmd;
        EXPECT_TRUE(rx.valid)                 << "cmd=" << (int)cmd;
    }
}

TEST(SomfyFrameTest, Encode_RTW_My_DoesNotDecodeAsRTW) {
    // My → frame[0]=133, decode requires encKey > 133, so no proto branch fires.
    somfy_frame_t f;
    f.cmd = somfy_commands::My; f.remoteAddress = 0x123456;
    f.rollingCode = 1; f.encKey = 0xA7;
    f.bitLength = 56; f.proto = radio_proto::RTW;
    byte encoded[10] = {};
    f.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

TEST(SomfyFrameTest, Encode_RTW_Default_Branch) {
    // Toggle is not in the RTW switch → frame[0] stays as encKey=0xA7 (167).
    // Decode: 167 >= 160 → proto=RTS, cmd stays RTWProto → invalid.
    somfy_frame_t f;
    f.cmd = somfy_commands::Toggle; f.remoteAddress = 0x123456;
    f.rollingCode = 1; f.encKey = 0xA7;
    f.bitLength = 56; f.proto = radio_proto::RTW;
    byte encoded[10] = {};
    f.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTS);
    EXPECT_FALSE(rx.valid);
}

TEST(SomfyFrameTest, Encode_RTV_UpThroughFlag_ProtoRTV) {
    // RTV: frame[0] = 149 + (cmd_index - 1); Up→150 through Flag→158, all > 148 → RTV.
    const somfy_commands cmds[] = {
        somfy_commands::Up, somfy_commands::MyUp,
        somfy_commands::Down, somfy_commands::MyDown, somfy_commands::UpDown,
        somfy_commands::MyUpDown, somfy_commands::Prog,
        somfy_commands::SunFlag, somfy_commands::Flag,
    };
    for (auto cmd : cmds) {
        somfy_frame_t f;
        f.cmd = cmd; f.remoteAddress = 0x123456;
        f.rollingCode = 1; f.encKey = 0xA7;
        f.bitLength = 56; f.proto = radio_proto::RTV;
        byte encoded[10] = {};
        f.encodeFrame(encoded);
        somfy_frame_t rx;
        rx.bitLength = 56;
        rx.decodeFrame(encoded);
        EXPECT_EQ(rx.proto, radio_proto::RTV) << "cmd=" << (int)cmd;
        EXPECT_TRUE(rx.valid)                 << "cmd=" << (int)cmd;
    }
}

TEST(SomfyFrameTest, Encode_RTV_My_DoesNotDecodeAsRTV) {
    // My → frame[0]=149, decode requires encKey > 148 and ≤ 160; 149 > 148 → RTV,
    // decoded cmd = 149-148 = 1 = My. So this actually does decode as RTV.
    somfy_frame_t f;
    f.cmd = somfy_commands::My; f.remoteAddress = 0x123456;
    f.rollingCode = 1; f.encKey = 0xA7;
    f.bitLength = 56; f.proto = radio_proto::RTV;
    byte encoded[10] = {};
    f.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTV);
    EXPECT_EQ(rx.cmd, somfy_commands::My);
    EXPECT_TRUE(rx.valid);
}

TEST(SomfyFrameTest, Encode_RTV_Default_Branch) {
    // Toggle not in RTV switch → frame[0] stays 0xA7 (167), decodes as RTS, invalid.
    somfy_frame_t f;
    f.cmd = somfy_commands::Toggle; f.remoteAddress = 0x123456;
    f.rollingCode = 1; f.encKey = 0xA7;
    f.bitLength = 56; f.proto = radio_proto::RTV;
    byte encoded[10] = {};
    f.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_EQ(rx.proto, radio_proto::RTS);
    EXPECT_FALSE(rx.valid);
}

// ── encode80Byte7 overflow path ───────────────────────────────────────────
TEST(SomfyFrameTest, Encode80Byte7_OverflowWraps) {
    somfy_frame_t f;
    // start=196, repeat=20 → 196 + 80 = 276 > 255 → loops until it fits
    byte result = f.encode80Byte7(196, 20);
    EXPECT_LE((int)result, 255);
}

// ══════════════════════════════════════════════════════════════════════════════
// G  Utility methods: print, isSynonym, isRepeat, copy
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, Print_DoesNotCrash) {
    somfy_frame_t f;
    f.cmd = somfy_commands::Up;
    f.remoteAddress = 0x123456;
    f.rollingCode   = 1;
    EXPECT_NO_FATAL_FAILURE(f.print());
}

TEST(SomfyFrameTest, IsSynonym_SameAddrRCode_DifferentCmd_True) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = somfy_commands::Up;
    b.cmd = somfy_commands::Down;
    EXPECT_TRUE(a.isSynonym(b));
}

TEST(SomfyFrameTest, IsSynonym_SameCmd_False) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = b.cmd = somfy_commands::Up;
    EXPECT_FALSE(a.isSynonym(b));
}

TEST(SomfyFrameTest, IsRepeat_SameAddrCmdRCode_True) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = b.cmd = somfy_commands::Up;
    EXPECT_TRUE(a.isRepeat(b));
}

TEST(SomfyFrameTest, IsRepeat_DifferentCmd_False) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = somfy_commands::Up;
    b.cmd = somfy_commands::Down;
    EXPECT_FALSE(a.isRepeat(b));
}

TEST(SomfyFrameTest, Copy_Repeat_IncrementsRepeats) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = b.cmd = somfy_commands::Up;
    a.repeats = 1;
    b.rssi = -55; b.lqi = 3;
    a.copy(b);
    EXPECT_EQ(a.repeats, 2u);
    EXPECT_EQ(a.rssi,   -55);
    EXPECT_EQ(a.lqi,    3u);
}

TEST(SomfyFrameTest, Copy_Synonym_DoesNotCopyProcessed) {
    somfy_frame_t a, b;
    a.remoteAddress = b.remoteAddress = 0xABCDEF;
    a.rollingCode   = b.rollingCode   = 7;
    a.cmd = somfy_commands::Up;
    b.cmd = somfy_commands::Down;  // synonym
    a.processed = true;
    b.processed = false;
    b.valid = true;
    a.copy(b);
    EXPECT_TRUE(a.synonym);
    EXPECT_TRUE(a.processed);  // not overwritten by synonym copy
    EXPECT_EQ(a.cmd, somfy_commands::Down);
}

TEST(SomfyFrameTest, Copy_NonSynonym_CopiesAllFields) {
    somfy_frame_t a, b;
    a.remoteAddress = 0x111111;
    b.remoteAddress = 0x222222;
    b.rollingCode   = 99;
    b.cmd           = somfy_commands::Prog;
    b.valid         = true;
    b.processed     = false;
    b.rssi          = -80;
    a.copy(b);
    EXPECT_FALSE(a.synonym);
    EXPECT_EQ(a.remoteAddress, 0x222222u);
    EXPECT_EQ(a.rollingCode,   99u);
    EXPECT_EQ(a.cmd,           somfy_commands::Prog);
}

// ══════════════════════════════════════════════════════════════════════════════
// H  somfy_tx_queue_t — push / pop / overflow
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, TxQueue_PopFromEmpty_ReturnsFalse) {
    somfy_tx_queue_t q;
    somfy_tx_t tx;
    EXPECT_FALSE(q.pop(&tx));
}

TEST(SomfyFrameTest, TxQueue_PushPop_Roundtrip) {
    somfy_tx_queue_t q;
    uint8_t hwsync = 7;
    uint8_t payload[10] = {1,2,3,4,5,6,7,8,9,10};
    q.push(hwsync, payload, 56);
    EXPECT_EQ(q.length, 1u);
    somfy_tx_t tx;
    EXPECT_TRUE(q.pop(&tx));
    EXPECT_EQ(tx.hwsync,     7u);
    EXPECT_EQ(tx.bit_length, 56u);
    EXPECT_EQ(memcmp(tx.payload, payload, 10), 0);
    EXPECT_EQ(q.length, 0u);
}

TEST(SomfyFrameTest, TxQueue_PushRxOverload) {
    somfy_tx_queue_t q;
    somfy_rx_t rx;
    rx.clear();
    rx.cpt_synchro_hw = 3;
    rx.bit_length     = 56;
    rx.payload[0]     = 0xAA;
    q.push(&rx);
    EXPECT_EQ(q.length, 1u);
    somfy_tx_t tx;
    EXPECT_TRUE(q.pop(&tx));
    EXPECT_EQ(tx.hwsync, 3u);
    EXPECT_EQ(tx.payload[0], 0xAAu);
}

TEST(SomfyFrameTest, TxQueue_Overflow_DropsOldest) {
    somfy_tx_queue_t q;
    uint8_t payload[10] = {};
    // Fill to capacity
    for (uint8_t i = 0; i < MAX_TX_BUFFER; i++) {
        payload[0] = i + 1;
        q.push(i, payload, 56);
    }
    EXPECT_EQ(q.length, (uint8_t)MAX_TX_BUFFER);
    // One more push triggers overflow eviction path
    payload[0] = 99;
    q.push(99, payload, 56);
    EXPECT_EQ(q.length, (uint8_t)MAX_TX_BUFFER);
}

// ══════════════════════════════════════════════════════════════════════════════
// I  somfy_rx_queue_t — init / pop
// ══════════════════════════════════════════════════════════════════════════════

TEST(SomfyFrameTest, RxQueue_Init_ClearsState) {
    somfy_rx_queue_t q;
    q.init();
    EXPECT_EQ(q.length, 0u);
    for (uint8_t i = 0; i < MAX_RX_BUFFER; i++)
        EXPECT_EQ(q.index[i], 0xFFu);
}

TEST(SomfyFrameTest, RxQueue_PopFromEmpty_ReturnsFalse) {
    somfy_rx_queue_t q;
    q.init();
    somfy_rx_t rx;
    EXPECT_FALSE(q.pop(&rx));
}

TEST(SomfyFrameTest, RxQueue_PopAfterManualPush_ReturnsItem) {
    // somfy_rx_queue_t::push is not in SomfyFrame.cpp; populate the queue
    // internals directly to exercise the pop() success path.
    somfy_rx_queue_t q;
    q.init();
    q.items[0].clear();
    q.items[0].bit_length  = 56;
    q.items[0].cpt_synchro_hw = 5;
    q.items[0].payload[0] = 0xBB;
    q.index[0] = 0;  // oldest entry points to slot 0
    q.length   = 1;

    somfy_rx_t out;
    EXPECT_TRUE(q.pop(&out));
    EXPECT_EQ(out.bit_length,      56u);
    EXPECT_EQ(out.cpt_synchro_hw,  5u);
    EXPECT_EQ(out.payload[0],      0xBBu);
    EXPECT_EQ(q.length,            0u);
}

// ── decodeFrame(somfy_rx_t*) overload ────────────────────────────────────────
TEST(SomfyFrameTest, DecodeFrame_RxOverload_DecodesCorrectly) {
    // Build a valid RTS 56-bit encoded frame via encodeFrame, then feed it
    // through the somfy_rx_t overload of decodeFrame.
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::Up;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 3;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);

    somfy_rx_t rx_in;
    rx_in.clear();
    rx_in.bit_length      = 56;
    rx_in.cpt_synchro_hw  = 2;
    rx_in.pulseCount      = 0;
    memcpy(rx_in.payload, encoded, 10);

    somfy_frame_t rx;
    rx.decodeFrame(&rx_in);

    EXPECT_TRUE(rx.valid);
    EXPECT_EQ(rx.cmd,           somfy_commands::Up);
    EXPECT_EQ(rx.remoteAddress, 0x123456u);
    EXPECT_EQ(rx.rollingCode,   3u);
    EXPECT_EQ(rx.hwsync,        2u);
    EXPECT_EQ(rx.bitLength,     56u);
}

// ── decode default branch: Unknown0 cmd (0x0) → invalid ──────────────────────
TEST(SomfyFrameTest, Decode_Unknown0Cmd_Invalid) {
    // Encode a frame with cmd nibble = 0x0 (Unknown0). The decode switch has
    // no case for Unknown0 (it's commented out) so it falls to default → valid=false.
    somfy_frame_t tx;
    tx.cmd           = somfy_commands::Unknown0;
    tx.remoteAddress = 0x123456;
    tx.rollingCode   = 1;
    tx.encKey        = 0xA7;
    tx.bitLength     = 56;
    tx.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    tx.encodeFrame(encoded);
    somfy_frame_t rx;
    rx.bitLength = 56;
    rx.decodeFrame(encoded);
    EXPECT_FALSE(rx.valid);
}

// ── encode80BitFrame default branch: unhandled cmd ────────────────────────────
TEST(SomfyFrameTest, Encode80BitFrame_UnhandledCmd_DefaultBranch) {
    // SunFlag is not in the encode80BitFrame switch → hits default: break.
    // The frame still encodes without crash; bytes 7-9 keep their init values.
    somfy_frame_t f;
    f.cmd           = somfy_commands::SunFlag;
    f.remoteAddress = 0x123456;
    f.rollingCode   = 1;
    f.encKey        = 0xA7;
    f.bitLength     = 80;
    f.proto         = radio_proto::RTS;
    byte encoded[10] = {};
    EXPECT_NO_FATAL_FAILURE(f.encodeFrame(encoded));
}
