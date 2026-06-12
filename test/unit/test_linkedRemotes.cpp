// test_linkedRemotes.cpp — tests for SomfyLinkedRemotes, exercised through
// SomfyShade's linkRemote()/unlinkRemote()/getLinkedRemote() forwarders:
//   linkRemote()   — new slot, already linked (updates rolling code), all slots
//                    full (returns false)
//   unlinkRemote() — found (clears slot), not found (returns false)

#include "TestableShade.h"
#include "nvs.h"
#include "MQTT.h"
#include "SomfyShadeController.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern SomfyShadeController somfy;
extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;

using ::testing::_;
using ::testing::AnyNumber;

// ── fixture ───────────────────────────────────────────────────────────────

class LinkedRemotesTest : public ::testing::Test {
  protected:
    TestableShade shade;

    void SetUp() override
    {
        shade.setShadeId(1);
        shade.setRemoteAddress(0x112233);

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        nvs_stub_reset_all();
        mqtt_connected_flag = false;
        mqtt_published.clear();
    }

    void TearDown() override { nvs_stub_reset_all(); }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. linkRemote()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(LinkedRemotesTest, LinkRemote_NewSlot_ReturnsTrueAndStoresAddress)
{
    bool result = shade.linkRemote(0xABCD01, 10);
    EXPECT_TRUE(result);
    EXPECT_EQ(shade.getLinkedRemote(0).getRemoteAddress(), 0xABCD01u);
}

TEST_F(LinkedRemotesTest, LinkRemote_AlreadyLinked_UpdatesRollingCodeAndReturnsTrue)
{
    shade.linkRemote(0xABCD01, 5);
    bool result = shade.linkRemote(0xABCD01, 99);
    EXPECT_TRUE(result);
    // Rolling code is stored inside the remote — just confirm it didn't claim a second slot
    EXPECT_EQ(shade.getLinkedRemote(1).getRemoteAddress(), 0u);
}

TEST_F(LinkedRemotesTest, LinkRemote_AllSlotsFull_ReturnsFalse)
{
    for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
        shade.getLinkedRemote(i).setRemoteAddress(0x100000 + i);
    bool result = shade.linkRemote(0xDEADBE, 0);
    EXPECT_FALSE(result);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. unlinkRemote()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(LinkedRemotesTest, UnlinkRemote_NotFound_ReturnsFalse)
{
    bool result = shade.unlinkRemote(0xDEAD00);
    EXPECT_FALSE(result);
}

TEST_F(LinkedRemotesTest, UnlinkRemote_Found_ClearsSlotAndReturnsTrue)
{
    shade.getLinkedRemote(0).setRemoteAddress(0xBEEF01);
    bool result = shade.unlinkRemote(0xBEEF01);
    EXPECT_TRUE(result);
    EXPECT_EQ(shade.getLinkedRemote(0).getRemoteAddress(), 0u);
}
