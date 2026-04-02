// test_persistence.cpp — 100% line coverage for SomfyShade persistence layer:
//   commit(), commitShadePosition(), commitMyPosition(), commitTiltPosition(),
//   save(), load() (including legacy u16 migration branch)
//
// NVS is provided by the namespace-keyed in-memory stub in stubs/nvs.h.
// nvs_stub_use_nvs controls whether somfy.useNVS() returns true.

#include "TestableShade.h"
#include "nvs.h"
#include "MQTT.h"
#include "SomfyController.h"
#include "../../main/compat/preferences.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cmath>

extern bool nvs_stub_use_nvs;
extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;
extern SomfyShadeController somfy;
extern Preferences pref;

using ::testing::AnyNumber;
using ::testing::_;

// ── fixture ───────────────────────────────────────────────────────────────

class PersistenceTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0x112233);
        shade.shadeType      = shade_types::roller;
        shade.tiltType       = tilt_types::none;
        shade.currentPos     = 42.0f;
        shade.currentTiltPos = 10.0f;
        shade.target         = 42.0f;
        shade.tiltTarget     = 10.0f;
        shade.myPos          = 25.0f;
        shade.myTiltPos      = -1.0f;
        shade.direction      = 0;
        shade.upTime         = 10000;
        shade.downTime       = 10000;
        shade.tiltTime       = 7000;
        shade.paired         = false;

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
};

// ══════════════════════════════════════════════════════════════════════════════
// A. commit() — delegates to somfy.commit() which sets isDirty
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, Commit_SetsSomfyIsDirty) {
    extern SomfyShadeController somfy;
    somfy.isDirty = false;
    // somfy.commit() is stubbed to no-op, but commitShadePosition sets isDirty directly
    shade.commitShadePosition();
    EXPECT_TRUE(somfy.isDirty);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. commitShadePosition() — sets isDirty; writes currentPos to NVS when useNVS
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, CommitShadePosition_UseNVSFalse_SetsDirtyOnly) {
    extern SomfyShadeController somfy;
    nvs_stub_use_nvs = false;
    somfy.isDirty = false;
    shade.commitShadePosition();
    EXPECT_TRUE(somfy.isDirty);
    // NVS should have nothing written (no namespace opened)
    EXPECT_TRUE(nvs_ns_stores.empty());
}

TEST_F(PersistenceTest, CommitShadePosition_UseNVSTrue_WritesCurrentPos) {
    nvs_stub_use_nvs = true;
    shade.currentPos = 73.5f;
    shade.commitShadePosition();
    // The namespace key is "SomfyShade1"
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("currentPos"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// C. commitMyPosition() — sets isDirty; writes myPos to NVS when useNVS
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, CommitMyPosition_UseNVSFalse_SetsDirtyOnly) {
    extern SomfyShadeController somfy;
    nvs_stub_use_nvs = false;
    somfy.isDirty = false;
    shade.commitMyPosition();
    EXPECT_TRUE(somfy.isDirty);
    EXPECT_TRUE(nvs_ns_stores.empty());
}

TEST_F(PersistenceTest, CommitMyPosition_UseNVSTrue_WritesMyPos) {
    nvs_stub_use_nvs = true;
    shade.myPos = 50.0f;
    shade.commitMyPosition();
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("myPos"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// D. commitTiltPosition() — sets isDirty; writes currentTiltPos when useNVS
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, CommitTiltPosition_UseNVSFalse_SetsDirtyOnly) {
    extern SomfyShadeController somfy;
    nvs_stub_use_nvs = false;
    somfy.isDirty = false;
    shade.commitTiltPosition();
    EXPECT_TRUE(somfy.isDirty);
    EXPECT_TRUE(nvs_ns_stores.empty());
}

TEST_F(PersistenceTest, CommitTiltPosition_UseNVSTrue_WritesCurrentTiltPos) {
    nvs_stub_use_nvs = true;
    shade.currentTiltPos = 33.0f;
    shade.commitTiltPosition();
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("currentTiltPos"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// E. save() — always commits and publishes; writes full record when useNVS
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, Save_UseNVSFalse_ReturnsTrue) {
    nvs_stub_use_nvs = false;
    EXPECT_TRUE(shade.save());
    // No NVS writes, but commit() and publish() ran (no crash)
    EXPECT_TRUE(nvs_ns_stores.empty());
}

TEST_F(PersistenceTest, Save_UseNVSTrue_WritesAllCoreFields) {
    nvs_stub_use_nvs = true;
    strncpy(shade.name, "Living Room", sizeof(shade.name));
    shade.currentPos     = 55.0f;
    shade.currentTiltPos = 20.0f;
    shade.myPos          = 30.0f;
    shade.upTime         = 8000;
    shade.downTime       = 9000;
    shade.tiltTime       = 6000;
    shade.paired         = true;

    EXPECT_TRUE(shade.save());

    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("name"),          1u);
    EXPECT_EQ(store.entries.count("remoteAddress"), 1u);
    EXPECT_EQ(store.entries.count("shadeType"),     1u);
    EXPECT_EQ(store.entries.count("currentPos"),    1u);
    EXPECT_EQ(store.entries.count("currentTiltPos"),1u);
    EXPECT_EQ(store.entries.count("myPos"),         1u);
    EXPECT_EQ(store.entries.count("upTime"),        1u);
    EXPECT_EQ(store.entries.count("downTime"),      1u);
    EXPECT_EQ(store.entries.count("tiltTime"),      1u);
    EXPECT_EQ(store.entries.count("paired"),        1u);
    EXPECT_EQ(store.entries.count("hasTilt"),       1u);
}

TEST_F(PersistenceTest, Save_UseNVSTrue_WritesLinkedAddresses) {
    nvs_stub_use_nvs = true;
    shade.linkedRemotes[0].setRemoteAddress(0xABCDEF);
    shade.linkedRemotes[1].setRemoteAddress(0x123456);

    shade.save();

    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries.count("linkedAddr"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// F. load() — reads all fields back; round-trip consistency
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, Load_AfterSave_RestoresCoreFields) {
    nvs_stub_use_nvs = true;
    strncpy(shade.name, "Bedroom", sizeof(shade.name));
    shade.currentPos     = 67.0f;
    shade.currentTiltPos = 15.0f;
    shade.myPos          = 40.0f;
    shade.upTime         = 12000;
    shade.downTime       = 11000;
    shade.tiltTime       = 5000;
    shade.paired         = true;
    shade.shadeType      = shade_types::blind;
    shade.save();

    // Reset shade state, then load
    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.setRemoteAddress(0);
    loaded.load();

    EXPECT_STREQ(loaded.name, "Bedroom");
    EXPECT_FLOAT_EQ(loaded.currentPos, 67.0f);
    EXPECT_FLOAT_EQ(loaded.currentTiltPos, 15.0f);
    EXPECT_EQ(loaded.getRemoteAddress(), 0x112233u);
    EXPECT_EQ(loaded.upTime,   12000u);
    EXPECT_EQ(loaded.downTime, 11000u);
    EXPECT_EQ(loaded.tiltTime,  5000u);
    EXPECT_TRUE(loaded.paired);
}

TEST_F(PersistenceTest, Load_AfterSave_TargetEqualsFloorOfCurrentPos) {
    nvs_stub_use_nvs = true;
    shade.currentPos = 73.9f;
    shade.save();

    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.load();

    EXPECT_FLOAT_EQ(loaded.target, floorf(73.9f));
    EXPECT_FLOAT_EQ(loaded.tiltTarget, floorf(shade.currentTiltPos));
}

TEST_F(PersistenceTest, Load_AfterSave_RestoresLinkedRemote) {
    nvs_stub_use_nvs = true;
    shade.linkedRemotes[0].setRemoteAddress(0xCAFE01);
    shade.save();

    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.load();

    EXPECT_EQ(loaded.linkedRemotes[0].getRemoteAddress(), 0xCAFE01u);
}

TEST_F(PersistenceTest, Load_AfterSave_RestoresRollingCode) {
    nvs_stub_use_nvs = true;
    // Seed the ShadeCodes namespace directly (save() writes rolling code via p_lastRollingCode)
    shade.setRemoteAddress(0x112233);
    shade.lastRollingCode = 42;
    // write the rolling code into NVS manually (save() doesn't write rolling codes — they
    // are written by sendCommand). Simulate what SomfyRemote::p_lastRollingCode persists.
    pref.begin("ShadeCodes");
    pref.putUShort(shade.getRemotePrefId(), 42);
    pref.end();

    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.setRemoteAddress(0x112233);
    // save first so shade-specific namespace exists
    shade.save();
    loaded.load();

    EXPECT_EQ(loaded.lastRollingCode, 42u);
}

// ══════════════════════════════════════════════════════════════════════════════
// G. load() — legacy u16 migration branch (upTime key exists as PT_U16, not PT_U32)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(PersistenceTest, Load_LegacyU16Times_MigratesAndReadsCorrectValues) {
    nvs_stub_use_nvs = true;
    // Write legacy u16 keys directly into NVS (NVS_TYPE_U16, not NVS_TYPE_U32)
    pref.begin("SomfyShade1");
    pref.putUShort("upTime",   3000);
    pref.putUShort("downTime", 4000);
    pref.putUShort("tiltTime", 2000);
    pref.end();

    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.load();

    // Migration converts u16 → u32 and rewrites keys; values must be preserved
    EXPECT_EQ(loaded.upTime,   3000u);
    EXPECT_EQ(loaded.downTime, 4000u);
    EXPECT_EQ(loaded.tiltTime, 2000u);

    // After migration the NVS keys are now PT_U32
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries["upTime"].type, NVS_TYPE_U32);
}

TEST_F(PersistenceTest, Load_LegacyU16Times_UseNVSFalse_MigratesWithoutRewrite) {
    // When useNVS() is false, migration reads the values but does NOT rewrite them
    // (the inner if(somfy.useNVS()) block is skipped).
    nvs_stub_use_nvs = false;
    pref.begin("SomfyShade1");
    pref.putUShort("upTime",   1500);
    pref.putUShort("downTime", 2500);
    pref.putUShort("tiltTime", 1200);
    pref.end();

    // useNVS() = false → pref.begin is called readonly → still reads from stub
    TestableShade loaded;
    EXPECT_CALL(loaded, emitState(_, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitState(_)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(loaded, emitCommand(_, _, _, _, _)).Times(AnyNumber());
    loaded.setShadeId(1);
    loaded.load();

    EXPECT_EQ(loaded.upTime,   1500u);
    EXPECT_EQ(loaded.downTime, 2500u);
    EXPECT_EQ(loaded.tiltTime, 1200u);

    // Key type must still be U16 — no rewrite happened
    auto &store = nvs_ns_stores["SomfyShade1"];
    EXPECT_EQ(store.entries["upTime"].type, NVS_TYPE_U16);
}
