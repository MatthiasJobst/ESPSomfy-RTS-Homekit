// test_somfyGroup.cpp — unit tests for SomfyGroup.
// Covers: linkShade, unlinkShade, compressLinkedShadeIds, hasShadeId,
// updateFlags with linked shade, emitState with linked shade, sendCommand
// (all switch branches + linked-shade dispatch), fromJSON, toJSON/toJSONRef,
// publishState, publish/unpublish (all overloads).
#include "test_controller_access.h"
#include "SomfyFrame.h"
#include "SomfyCommandQueue.h"
#include "ShadeConfigFile.h"
#include "MQTT.h"
#include <cstring>
#include <gtest/gtest.h>

extern uint64_t test_clock_ms;
extern bool mqtt_connected_flag;
extern bool stub_shadeconfig_exists;
extern int stub_save_call_count;
extern std::unordered_map<std::string, std::string> mqtt_published;
extern std::unordered_map<std::string, std::string> mqtt_unpublished;

// ── Fixture ──────────────────────────────────────────────────────────────────

class GroupTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        for (auto &s : somfy.shades)
            s.clear();
        for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++)
            somfy.groupController.groupSlot(i).clear();
        for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
            somfy.roomController.roomSlot(i).clear();
        for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++)
            somfy.repeaterController.repeaterSlot(i) = 0;
        somfy.isDirty = false;
        somfy.startingAddress = 0x100000;
        stub_shadeconfig_exists = false;
        stub_save_call_count = 0;
        test_clock_ms = 0;
        mqtt_connected_flag = false;
        mqtt_published.clear();
        mqtt_unpublished.clear();
        somfy.commandDispatcher.cmdQueue = SomfyCommandQueue{};
        somfy.lastCommit = 0;
    }
};

// GroupWithShadeTest pre-populates group[0] (groupId=1) and shade[0] (shadeId=1),
// and links shade 1 into group 1's linkedShades[0].
class GroupWithShadeTest : public GroupTest {
  protected:
    void SetUp() override
    {
        GroupTest::SetUp();
        somfy.shades[0].setShadeId(1);
        somfy.shades[0].setRemoteAddress(0x100001);
        somfy.shades[0].bitLength = 56;
        somfy.shades[0].repeats = 1;
        somfy.groupController.groupSlot(0).setGroupId(1);
        somfy.groupController.groupSlot(0).setRemoteAddress(0x200001);
        somfy.groupController.groupSlot(0).bitLength = 56;
        somfy.groupController.groupSlot(0).linkedShades[0] = 1; // link shade 1
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A  linkShade
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, LinkShade_EmptySlot_AddsShadeAndReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    EXPECT_TRUE(somfy.groupController.groupSlot(0).linkShade(2));
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[0], 2);
}

TEST_F(GroupTest, LinkShade_AlreadyLinked_ReturnsTrueWithoutDuplicate)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 2;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).linkShade(2));
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[1], 0); // no duplicate
}

TEST_F(GroupTest, LinkShade_AllSlotsFull_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPED_SHADES; i++)
        somfy.groupController.groupSlot(0).linkedShades[i] = i + 1; // fill all 32 slots
    EXPECT_FALSE(somfy.groupController.groupSlot(0).linkShade(99));
}

// ══════════════════════════════════════════════════════════════════════════════
// B  unlinkShade / compressLinkedShadeIds
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, UnlinkShade_Present_RemovesAndCompresses)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 1;
    somfy.groupController.groupSlot(0).linkedShades[1] = 2;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).unlinkShade(1));
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[0], 2); // compressed
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[1], 0);
}

TEST_F(GroupTest, UnlinkShade_NotPresent_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 1;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).unlinkShade(99));
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[0], 1); // unchanged
}

TEST_F(GroupTest, CompressLinkedShadeIds_GapsRemoved)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 0;
    somfy.groupController.groupSlot(0).linkedShades[1] = 3;
    somfy.groupController.groupSlot(0).linkedShades[2] = 0;
    somfy.groupController.groupSlot(0).linkedShades[3] = 5;
    somfy.groupController.groupSlot(0).compressLinkedShadeIds();
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[0], 3);
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[1], 5);
    EXPECT_EQ(somfy.groupController.groupSlot(0).linkedShades[2], 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// C  hasShadeId
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, HasShadeId_Present_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 3;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).hasShadeId(3));
}

TEST_F(GroupTest, HasShadeId_NotPresent_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    EXPECT_FALSE(somfy.groupController.groupSlot(0).hasShadeId(5));
}

TEST_F(GroupTest, HasShadeId_SecondSlot_IteratesPastFirst)
{
    // linkedShades[0]=1 (non-zero, not 2) → loop continues → linkedShades[1]=2 → return true
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 1;
    somfy.groupController.groupSlot(0).linkedShades[1] = 2;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).hasShadeId(2));
}

// ══════════════════════════════════════════════════════════════════════════════
// D  updateFlags with linked shade
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupWithShadeTest, UpdateFlags_LinkedShadeHasFlags_AggregatesToGroup)
{
    somfy.shades[0].flags.setFlags(0x20); // Sunny
    somfy.groupController.groupSlot(0).updateFlags();
    EXPECT_EQ(somfy.groupController.groupSlot(0).flags.getFlags(), 0x20);
}

// ══════════════════════════════════════════════════════════════════════════════
// E  emitState with linked shade found
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupWithShadeTest, EmitState_WithLinkedShade_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).emitState();
}

TEST_F(GroupTest, EmitState_NoLinkedShades_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).emitState();
}

TEST_F(GroupWithShadeTest, EmitState_WithEventName_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).emitState("groupAdded");
}

TEST_F(GroupWithShadeTest, EmitState_WithNumAndEvent_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).emitState(1, "groupState");
}

// ══════════════════════════════════════════════════════════════════════════════
// F  sendCommand — switch branches + linked-shade dispatch
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupWithShadeTest, SendCommand_OneArg_Dispatches)
{
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::My);
    EXPECT_EQ(somfy.groupController.groupSlot(0).direction, 0);
}

TEST_F(GroupWithShadeTest, SendCommand_My_SetsDirectionZero)
{
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::My, 1);
    EXPECT_EQ(somfy.groupController.groupSlot(0).direction, 0);
}

TEST_F(GroupWithShadeTest, SendCommand_Up_SetsDirectionMinus1)
{
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::Up, 1);
    EXPECT_EQ(somfy.groupController.groupSlot(0).direction, -1);
}

TEST_F(GroupWithShadeTest, SendCommand_Down_SetsDirectionPlus1)
{
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::Down, 1);
    EXPECT_EQ(somfy.groupController.groupSlot(0).direction, 1);
}

TEST_F(GroupWithShadeTest, SendCommand_Other_DoesNotChangeDirection)
{
    somfy.groupController.groupSlot(0).direction = 0;
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::Prog, 1);
    // default branch — direction unchanged
    EXPECT_EQ(somfy.groupController.groupSlot(0).direction, 0);
}

TEST_F(GroupWithShadeTest, SendCommand_LinkedShadeDispatched_DoesNotCrash)
{
    // shade[0] is linked and exists — processInternalCommand and emitCommand are called
    somfy.groupController.groupSlot(0).sendCommand(somfy_commands::Up, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// G  fromJSON
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, FromJSON_BasicFields_Parsed)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["name"] = "Living Room";
    obj["roomId"] = 2;
    obj["remoteAddress"] = 0x300001;
    obj["bitLength"] = 56;
    obj["repeats"] = 3;
    obj["flipCommands"] = true;
    somfy.groupController.groupSlot(0).fromJSON(obj);
    EXPECT_STREQ(somfy.groupController.groupSlot(0).name, "Living Room");
    EXPECT_EQ(somfy.groupController.groupSlot(0).roomId, 2);
    EXPECT_EQ(somfy.groupController.groupSlot(0).repeats, 3);
}

TEST_F(GroupTest, FromJSON_WithLinkedShades_PopulatesLinkedShades)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    JsonArray arr = obj["linkedShades"].to<JsonArray>();
    arr.add(1);
    arr.add(2);
    EXPECT_TRUE(somfy.groupController.groupSlot(0).fromJSON(obj));
    EXPECT_TRUE(somfy.groupController.groupSlot(0).hasShadeId(1));
    EXPECT_TRUE(somfy.groupController.groupSlot(0).hasShadeId(2));
    EXPECT_FALSE(somfy.groupController.groupSlot(0).hasShadeId(3));
}

TEST_F(GroupTest, FromJSON_WithLinkedShades_ClearsExistingBeforeRepopulating)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).linkedShades[0] = 7; // pre-existing shade that is not in the JSON
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    JsonArray arr = obj["linkedShades"].to<JsonArray>();
    arr.add(3);
    somfy.groupController.groupSlot(0).fromJSON(obj);
    EXPECT_TRUE(somfy.groupController.groupSlot(0).hasShadeId(3));
    EXPECT_FALSE(somfy.groupController.groupSlot(0).hasShadeId(7)); // old entry must be gone
}

// ══════════════════════════════════════════════════════════════════════════════
// H  toJSON / toJSONRef
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, ToJSON_NoLinkedShades_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    JsonResponse json;
    somfy.groupController.groupSlot(0).toJSON(json);
}

TEST_F(GroupWithShadeTest, ToJSON_WithLinkedShade_DoesNotCrash)
{
    JsonResponse json;
    somfy.groupController.groupSlot(0).toJSON(json);
}

TEST_F(GroupTest, ToJSONRef_DoesNotCrash)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    JsonResponse json;
    somfy.groupController.groupSlot(0).toJSONRef(json);
}

// ══════════════════════════════════════════════════════════════════════════════
// I  publishState / publish / unpublish  (mqtt connected)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(GroupTest, PublishState_MqttConnected_PublishesTopics)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    somfy.groupController.groupSlot(0).publishState();
    EXPECT_NE(mqtt_published.find("groups/1/direction"), mqtt_published.end());
}

TEST_F(GroupTest, Publish_NoArg_MqttConnected_PublishesGroupId)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    somfy.groupController.groupSlot(0).publish();
    EXPECT_NE(mqtt_published.find("groups/1/groupId"), mqtt_published.end());
}

TEST_F(GroupTest, Publish_NoArg_MqttDisconnected_DoesNotPublish)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    somfy.groupController.groupSlot(0).publish();
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(GroupTest, Unpublish_NoArg_MqttConnected_UnpublishesTopics)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    somfy.groupController.groupSlot(0).unpublish();
    EXPECT_NE(mqtt_unpublished.find("groups/1/groupId"), mqtt_unpublished.end());
}

TEST_F(GroupTest, UnpublishStatic_Id_MqttConnected_UnpublishesTopics)
{
    mqtt_connected_flag = true;
    SomfyGroup::unpublish(2);
    EXPECT_NE(mqtt_unpublished.find("groups/2/groupId"), mqtt_unpublished.end());
}

TEST_F(GroupTest, UnpublishStatic_IdAndTopic_MqttConnected_UnpublishesTopic)
{
    mqtt_connected_flag = true;
    SomfyGroup::unpublish(3, "name");
    EXPECT_NE(mqtt_unpublished.find("groups/3/name"), mqtt_unpublished.end());
}

TEST_F(GroupTest, UnpublishStatic_MqttDisconnected_DoesNothing)
{
    mqtt_connected_flag = false;
    SomfyGroup::unpublish(1, "name");
    EXPECT_TRUE(mqtt_unpublished.empty());
}

// ── publish(topic, T) overloads ───────────────────────────────────────────────

TEST_F(GroupTest, Publish_StringVal_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("name", "test", true));
    EXPECT_EQ(mqtt_published["groups/1/name"], "test");
}

TEST_F(GroupTest, Publish_StringVal_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("name", "test", true));
}

TEST_F(GroupTest, Publish_Int8Val_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("direction", (int8_t)1, true));
}

TEST_F(GroupTest, Publish_Int8Val_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("direction", (int8_t)1, true));
}

TEST_F(GroupTest, Publish_Uint8Val_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("groupId", (uint8_t)1, true));
}

TEST_F(GroupTest, Publish_Uint8Val_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("groupId", (uint8_t)1, true));
}

TEST_F(GroupTest, Publish_Uint32Val_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("remoteAddress", (uint32_t)0x100001, true));
}

TEST_F(GroupTest, Publish_Uint32Val_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("remoteAddress", (uint32_t)0x100001, true));
}

TEST_F(GroupTest, Publish_Uint16Val_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("rollingCode", (uint16_t)42, true));
}

TEST_F(GroupTest, Publish_Uint16Val_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("rollingCode", (uint16_t)42, true));
}

TEST_F(GroupTest, Publish_BoolVal_MqttConnected_ReturnsTrue)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = true;
    EXPECT_TRUE(somfy.groupController.groupSlot(0).publish("flipCommands", true, true));
}

TEST_F(GroupTest, Publish_BoolVal_MqttDisconnected_ReturnsFalse)
{
    somfy.groupController.groupSlot(0).setGroupId(1);
    mqtt_connected_flag = false;
    EXPECT_FALSE(somfy.groupController.groupSlot(0).publish("flipCommands", true, true));
}
