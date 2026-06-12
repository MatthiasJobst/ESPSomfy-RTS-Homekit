// test_somfyRoom.cpp — unit tests for SomfyRoom.
// Covers: clear, save, fromJSON, toJSON, emitState, publish, unpublish.
#include "test_controller_access.h"
#include "SomfyFrame.h"
#include "ShadeConfigFile.h"
#include "MQTT.h"
#include <cstring>
#include <gtest/gtest.h>

extern int stub_save_call_count;
extern bool mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;
extern std::unordered_map<std::string, std::string> mqtt_unpublished;

// ── Fixture ──────────────────────────────────────────────────────────────────

class RoomTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
            somfy.roomController.roomSlot(i).clear();
        stub_save_call_count = 0;
        mqtt_connected_flag = false;
        mqtt_published.clear();
        mqtt_unpublished.clear();
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A  clear / save
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(RoomTest, Clear_ResetsRoomId)
{
    somfy.roomController.roomSlot(0).roomId = 3;
    somfy.roomController.roomSlot(0).clear();
    EXPECT_EQ(somfy.roomController.roomSlot(0).roomId, 0);
    EXPECT_STREQ(somfy.roomController.roomSlot(0).name, "");
}

TEST_F(RoomTest, Save_CallsCommit)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(0).save();
    EXPECT_EQ(stub_save_call_count, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// B  fromJSON / toJSON
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(RoomTest, FromJSON_NameAndSortOrder_Parsed)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["name"] = "Bedroom";
    obj["sortOrder"] = 3;
    EXPECT_TRUE(somfy.roomController.roomSlot(0).fromJSON(obj));
    EXPECT_STREQ(somfy.roomController.roomSlot(0).name, "Bedroom");
    EXPECT_EQ(somfy.roomController.roomSlot(0).sortOrder, 3);
}

TEST_F(RoomTest, ToJSON_DoesNotCrash)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(0).sortOrder = 2;
    strcpy(somfy.roomController.roomSlot(0).name, "Kitchen");
    JsonResponse json;
    somfy.roomController.roomSlot(0).toJSON(json);
}

// ══════════════════════════════════════════════════════════════════════════════
// C  emitState
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(RoomTest, EmitState_DoesNotCrash)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(0).emitState();
}

TEST_F(RoomTest, EmitState_WithEventName_DoesNotCrash)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(0).emitState("roomAdded");
}

TEST_F(RoomTest, EmitState_WithNumAndEvent_DoesNotCrash)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(0).emitState(1, "roomState");
}

// ══════════════════════════════════════════════════════════════════════════════
// D  publish — mqtt connected
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(RoomTest, Publish_MqttConnected_PublishesTopics)
{
    somfy.roomController.roomSlot(0).roomId = 2;
    strcpy(somfy.roomController.roomSlot(0).name, "Hall");
    mqtt_connected_flag = true;
    somfy.roomController.roomSlot(0).publish();
    EXPECT_NE(mqtt_published.find("rooms/2/roomId"), mqtt_published.end());
    EXPECT_NE(mqtt_published.find("rooms/2/name"), mqtt_published.end());
    EXPECT_NE(mqtt_published.find("rooms/2/sortOrder"), mqtt_published.end());
}

TEST_F(RoomTest, Publish_MqttDisconnected_DoesNotPublish)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    mqtt_connected_flag = false;
    somfy.roomController.roomSlot(0).publish();
    EXPECT_TRUE(mqtt_published.empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// E  unpublish — mqtt connected
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(RoomTest, Unpublish_MqttConnected_UnpublishesTopics)
{
    somfy.roomController.roomSlot(0).roomId = 3;
    mqtt_connected_flag = true;
    somfy.roomController.roomSlot(0).unpublish();
    EXPECT_NE(mqtt_unpublished.find("rooms/3/roomId"), mqtt_unpublished.end());
    EXPECT_NE(mqtt_unpublished.find("rooms/3/name"), mqtt_unpublished.end());
    EXPECT_NE(mqtt_unpublished.find("rooms/3/sortOrder"), mqtt_unpublished.end());
}

TEST_F(RoomTest, Unpublish_MqttDisconnected_DoesNothing)
{
    somfy.roomController.roomSlot(0).roomId = 1;
    mqtt_connected_flag = false;
    somfy.roomController.roomSlot(0).unpublish();
    EXPECT_TRUE(mqtt_unpublished.empty());
}
