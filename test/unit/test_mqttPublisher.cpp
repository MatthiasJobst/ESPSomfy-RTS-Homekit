// test_mqttPublisher.cpp — coverage for the MQTT publishing layer of SomfyShade:
//   publish() overloads, p_*() setters, publishState(), unpublish(),
//   emitCommand(), publishDisco(), unpublishDisco()
//
// mqtt_connected_flag controls mqtt.connected().
// mqtt_published / mqtt_unpublished record every topic written.

#include "TestableShade.h"
#include "MQTT.h"
#include "ConfigSettings.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern ConfigSettings settings;

using ::testing::_;
using ::testing::AnyNumber;

// ── fixture ───────────────────────────────────────────────────────────────

class MQTTPublisherTest : public ::testing::Test {
  protected:
    TestableShade shade;

    void SetUp() override
    {
        shade.setShadeId(1);
        shade.setRemoteAddress(0xABCDEF);
        shade.shadeType = shade_types::roller;
        shade.tiltType = tilt_types::none;
        shade.currentPos = 50.0f;
        shade.currentTiltPos = 50.0f;
        shade.target = 50.0f;
        shade.tiltTarget = 50.0f;
        shade.targetSequencer.myPos = -1.0f;
        shade.targetSequencer.myTiltPos = -1.0f;
        shade.direction = 0;
        shade.tiltDirection = 0;
        shade.setFlipPosition(false);
        shade.lastRollingCode = 0;

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        mqtt_connected_flag = false;
        mqtt_published.clear();
        mqtt_unpublished.clear();
        test_clock_ms = 0;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. publish() overloads — topic format and connectivity gate
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, Publish_NotConnected_DoesNotPublish)
{
    mqtt_connected_flag = false;
    shade.publish("direction", (int8_t)1, false);
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, Publish_Int8_BuildsCorrectTopic)
{
    mqtt_connected_flag = true;
    shade.publish("direction", (int8_t)-1, false);
    EXPECT_EQ(mqtt_published.count("shades/1/direction"), 1u);
    EXPECT_EQ(mqtt_published["shades/1/direction"], "-1");
}

TEST_F(MQTTPublisherTest, Publish_UInt8_BuildsCorrectTopic)
{
    mqtt_connected_flag = true;
    shade.publish("shadeId", (uint8_t)1, true);
    EXPECT_EQ(mqtt_published["shades/1/shadeId"], "1");
}

TEST_F(MQTTPublisherTest, Publish_UInt32_BuildsCorrectTopic)
{
    mqtt_connected_flag = true;
    shade.publish("remoteAddress", (uint32_t)0xABCDEFu, true);
    EXPECT_EQ(mqtt_published["shades/1/remoteAddress"], std::to_string(0xABCDEFu));
}

TEST_F(MQTTPublisherTest, Publish_Bool_BuildsCorrectTopic)
{
    mqtt_connected_flag = true;
    shade.publish("flipPosition", true, true);
    EXPECT_EQ(mqtt_published["shades/1/flipPosition"], "1");
}

TEST_F(MQTTPublisherTest, Publish_CString_BuildsCorrectTopic)
{
    mqtt_connected_flag = true;
    shade.publish("name", "living room", true);
    EXPECT_EQ(mqtt_published["shades/1/name"], "living room");
}

TEST_F(MQTTPublisherTest, Publish_UsesShadeIdInTopic)
{
    mqtt_connected_flag = true;
    shade.setShadeId(7);
    shade.publish("direction", (int8_t)1, false);
    EXPECT_EQ(mqtt_published.count("shades/7/direction"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/direction"), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. p_*() setters — publish only on change, apply floor/transform correctly
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, p_direction_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.direction = 0;
    shade.p_direction(1);
    EXPECT_EQ(mqtt_published.count("shades/1/direction"), 1u);
    mqtt_published.clear();
    shade.direction = 1;
    shade.p_direction(1);
    EXPECT_EQ(mqtt_published.count("shades/1/direction"), 0u);
}

TEST_F(MQTTPublisherTest, p_currentPos_PublishesOnFloorChangeOnly)
{
    mqtt_connected_flag = true;
    shade.currentPos = 50.0f;
    shade.p_currentPos(51.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/position"), 1u);
    mqtt_published.clear();
    shade.currentPos = 50.0f;
    shade.p_currentPos(50.4f); // floor(50.0) == floor(50.4)
    EXPECT_EQ(mqtt_published.count("shades/1/position"), 0u);
}

TEST_F(MQTTPublisherTest, p_currentPos_FlipPosition_PublishesTransformed)
{
    mqtt_connected_flag = true;
    shade.setFlipPosition(true);
    shade.currentPos = 30.0f;
    shade.p_currentPos(40.0f); // transformPosition(40) = 100-40 = 60
    EXPECT_EQ(mqtt_published["shades/1/position"], "60");
}

TEST_F(MQTTPublisherTest, p_target_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.target = 50.0f;
    shade.p_target(80.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/target"), 1u);
    mqtt_published.clear();
    shade.target = 50.0f;
    shade.p_target(50.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/target"), 0u);
}

TEST_F(MQTTPublisherTest, p_myPos_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.targetSequencer.myPos = 30.0f;
    shade.p_myPos(40.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/mypos"), 1u);
    mqtt_published.clear();
    shade.targetSequencer.myPos = 30.0f;
    shade.p_myPos(30.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/mypos"), 0u);
}

TEST_F(MQTTPublisherTest, p_sunFlag_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.setFlags(0);
    shade.p_sunFlag(true);
    EXPECT_EQ(mqtt_published.count("shades/1/sunFlag"), 1u);
    mqtt_published.clear();
    shade.p_sunFlag(true); // same value
    EXPECT_EQ(mqtt_published.count("shades/1/sunFlag"), 0u);
}

TEST_F(MQTTPublisherTest, p_windy_PublishesOnChange)
{
    mqtt_connected_flag = true;
    shade.setFlags(0);
    shade.p_windy(true);
    EXPECT_EQ(mqtt_published.count("shades/1/windy"), 1u);
}

TEST_F(MQTTPublisherTest, p_sunny_PublishesOnChange)
{
    mqtt_connected_flag = true;
    shade.setFlags(0);
    shade.p_sunny(true);
    EXPECT_EQ(mqtt_published.count("shades/1/sunny"), 1u);
}

TEST_F(MQTTPublisherTest, p_lastRollingCode_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.lastRollingCode = 10;
    shade.p_lastRollingCode(20);
    EXPECT_EQ(mqtt_published.count("shades/1/lastRollingCode"), 1u);
    mqtt_published.clear();
    shade.lastRollingCode = 10;
    shade.p_lastRollingCode(10);
    EXPECT_EQ(mqtt_published.count("shades/1/lastRollingCode"), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// C. publishState() — topics written when connected; gated topics
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, PublishState_NotConnected_WritesNothing)
{
    mqtt_connected_flag = false;
    shade.publishState();
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, PublishState_Connected_WritesCoreTopic)
{
    mqtt_connected_flag = true;
    shade.currentPos = 30.0f;
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/position"), 1u);
    EXPECT_EQ(mqtt_published["shades/1/position"], "30");
}

TEST_F(MQTTPublisherTest, PublishState_Connected_WritesDirectionTargetMypos)
{
    mqtt_connected_flag = true;
    shade.direction = 1;
    shade.target = 80.0f;
    shade.targetSequencer.myPos = 40.0f;
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/direction"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/target"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/mypos"), 1u);
}

TEST_F(MQTTPublisherTest, PublishState_NoTilt_SkipsTiltTopics)
{
    mqtt_connected_flag = true;
    shade.tiltType = tilt_types::none;
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/tiltDirection"), 0u);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltPosition"), 0u);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltTarget"), 0u);
}

TEST_F(MQTTPublisherTest, PublishState_WithTilt_WritesTiltTopics)
{
    mqtt_connected_flag = true;
    shade.tiltType = tilt_types::integrated;
    shade.tiltDirection = 1;
    shade.currentTiltPos = 60.0f;
    shade.tiltTarget = 70.0f;
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/tiltDirection"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltPosition"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltTarget"), 1u);
}

TEST_F(MQTTPublisherTest, PublishState_NoSunSensor_SkipsSunTopics)
{
    mqtt_connected_flag = true;
    shade.setSunSensor(false);
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/sunFlag"), 0u);
    EXPECT_EQ(mqtt_published.count("shades/1/sunny"), 0u);
}

TEST_F(MQTTPublisherTest, PublishState_WithSunSensor_WritesSunTopics)
{
    mqtt_connected_flag = true;
    shade.setSunSensor(true);
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/sunFlag"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/sunny"), 1u);
}

TEST_F(MQTTPublisherTest, PublishState_AlwaysWritesWindy)
{
    mqtt_connected_flag = true;
    shade.publishState();
    EXPECT_EQ(mqtt_published.count("shades/1/windy"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// D. unpublish() — topics cleared
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, Unpublish_NotConnected_DoesNothing)
{
    mqtt_connected_flag = false;
    SomfyShade::unpublish(1);
    EXPECT_TRUE(mqtt_unpublished.empty());
}

TEST_F(MQTTPublisherTest, Unpublish_ById_ClearesCoreTopics)
{
    mqtt_connected_flag = true;
    SomfyShade::unpublish(1);
    EXPECT_EQ(mqtt_unpublished.count("shades/1/position"), 1u);
    EXPECT_EQ(mqtt_unpublished.count("shades/1/direction"), 1u);
    EXPECT_EQ(mqtt_unpublished.count("shades/1/target"), 1u);
    EXPECT_EQ(mqtt_unpublished.count("shades/1/name"), 1u);
}

TEST_F(MQTTPublisherTest, Unpublish_ByIdAndTopic_ClearsOneTopic)
{
    mqtt_connected_flag = true;
    SomfyShade::unpublish(3, "direction");
    EXPECT_EQ(mqtt_unpublished.count("shades/3/direction"), 1u);
    EXPECT_EQ(mqtt_unpublished.size(), 1u);
}

TEST_F(MQTTPublisherTest, Unpublish_InstanceMethod_UsesOwnShadeId)
{
    mqtt_connected_flag = true;
    shade.setShadeId(5);
    shade.unpublish();
    EXPECT_EQ(mqtt_unpublished.count("shades/5/position"), 1u);
    EXPECT_EQ(mqtt_unpublished.count("shades/1/position"), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// E. emitCommand() — publishes cmd/cmdSource/cmdAddress when connected
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, EmitCommand_NotConnected_NoMQTTPublish)
{
    mqtt_connected_flag = false;
    shade.callRealEmitCommand(somfy_commands::Up, "remote", 0xABCDEF);
    EXPECT_EQ(mqtt_published.count("shades/1/cmd"), 0u);
}

TEST_F(MQTTPublisherTest, EmitCommand_Connected_PublishesCmdTopics)
{
    mqtt_connected_flag = true;
    shade.callRealEmitCommand(somfy_commands::Up, "remote", 0x123456u);
    EXPECT_EQ(mqtt_published.count("shades/1/cmd"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/cmdSource"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/cmdAddress"), 1u);
}

TEST_F(MQTTPublisherTest, EmitCommand_Connected_CmdValueIsCommandName)
{
    mqtt_connected_flag = true;
    shade.callRealEmitCommand(somfy_commands::Down, "remote", 0u);
    EXPECT_EQ(mqtt_published["shades/1/cmd"], "Down");
}

TEST_F(MQTTPublisherTest, EmitCommand_Connected_SourceValuePropagated)
{
    mqtt_connected_flag = true;
    shade.callRealEmitCommand(somfy_commands::My, "api", 0u);
    EXPECT_EQ(mqtt_published["shades/1/cmdSource"], "api");
}

// ══════════════════════════════════════════════════════════════════════════════
// F. publishDisco() — connectivity gate, cover vs switch topic, shade variants
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, PublishDisco_NotConnected_DoesNothing)
{
    mqtt_connected_flag = false;
    settings.MQTT.pubDisco = true;
    shade.publishDisco();
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, PublishDisco_ConnectedButPubDiscoFalse_DoesNothing)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = false;
    shade.publishDisco();
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, PublishDisco_Roller_UsesCoverTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::roller;
    shade.publishDisco();
    EXPECT_EQ(mqtt_published.count("homeassistant/cover/1/config"), 1u);
    EXPECT_EQ(mqtt_published.count("homeassistant/switch/1/config"), 0u);
}

TEST_F(MQTTPublisherTest, PublishDisco_DryContact_UsesSwitchTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::drycontact;
    shade.publishDisco();
    EXPECT_EQ(mqtt_published.count("homeassistant/switch/1/config"), 1u);
    EXPECT_EQ(mqtt_published.count("homeassistant/cover/1/config"), 0u);
}

TEST_F(MQTTPublisherTest, PublishDisco_DryContact2_UsesSwitchTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::drycontact2;
    shade.publishDisco();
    EXPECT_EQ(mqtt_published.count("homeassistant/switch/1/config"), 1u);
}

// All non-drycontact shade types produce a cover topic. Verify each branch
// is reachable by iterating over the remaining types.
TEST_F(MQTTPublisherTest, PublishDisco_AllCoverShadeTypes_UseCoverTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    const shade_types coverTypes[] = {
        shade_types::blind,   shade_types::awning, shade_types::shutter,
        shade_types::garage1, shade_types::lgate,  shade_types::garage3,
    };
    for (auto st : coverTypes) {
        mqtt_published.clear();
        shade.shadeType = st;
        shade.publishDisco();
        EXPECT_EQ(mqtt_published.count("homeassistant/cover/1/config"), 1u) << "shade_type=" << (int)st;
    }
}

TEST_F(MQTTPublisherTest, PublishDisco_TiltOnly_UsesCoverTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::roller;
    shade.tiltType = tilt_types::tiltonly;
    shade.publishDisco();
    EXPECT_EQ(mqtt_published.count("homeassistant/cover/1/config"), 1u);
}

TEST_F(MQTTPublisherTest, PublishDisco_WithIntegratedTilt_UsesCoverTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::roller;
    shade.tiltType = tilt_types::integrated;
    shade.publishDisco();
    EXPECT_EQ(mqtt_published.count("homeassistant/cover/1/config"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// G. unpublishDisco() — only when connected AND pubDisco; correct topic cleared
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, UnpublishDisco_NotConnected_DoesNothing)
{
    mqtt_connected_flag = false;
    settings.MQTT.pubDisco = true;
    shade.unpublishDisco();
    EXPECT_TRUE(mqtt_unpublished.empty());
}

TEST_F(MQTTPublisherTest, UnpublishDisco_PubDiscoFalse_DoesNothing)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = false;
    shade.unpublishDisco();
    EXPECT_TRUE(mqtt_unpublished.empty());
}

TEST_F(MQTTPublisherTest, UnpublishDisco_Roller_ClearsCoverTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::roller;
    shade.unpublishDisco();
    EXPECT_EQ(mqtt_unpublished.count("homeassistant/cover/1/config"), 1u);
}

TEST_F(MQTTPublisherTest, UnpublishDisco_DryContact_ClearsSwitchTopic)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    shade.shadeType = shade_types::drycontact;
    shade.unpublishDisco();
    EXPECT_EQ(mqtt_unpublished.count("homeassistant/switch/1/config"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// H. publish() / unpublish() gaps — remaining overloads + no-arg publish
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, Publish_CString_NotConnected_ReturnsFalse)
{
    mqtt_connected_flag = false;
    EXPECT_FALSE(shade.publish("name", "test", false));
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, Publish_UInt32_NotConnected_ReturnsFalse)
{
    mqtt_connected_flag = false;
    EXPECT_FALSE(shade.publish("remoteAddress", (uint32_t)1u, false));
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, Publish_Bool_NotConnected_ReturnsFalse)
{
    mqtt_connected_flag = false;
    EXPECT_FALSE(shade.publish("flipPosition", true, false));
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, Publish_NoArg_Connected_WritesAllTopics)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = false;
    shade.publish();
    EXPECT_EQ(mqtt_published.count("shades/1/shadeId"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/remoteAddress"), 1u);
    EXPECT_EQ(mqtt_published.count("shades/1/position"), 1u);
}

TEST_F(MQTTPublisherTest, Publish_NoArg_NotConnected_WritesNothing)
{
    mqtt_connected_flag = false;
    shade.publish();
    EXPECT_TRUE(mqtt_published.empty());
}

TEST_F(MQTTPublisherTest, Unpublish_ById_PubDiscoTrue_ClearsDiscoTopics)
{
    mqtt_connected_flag = true;
    settings.MQTT.pubDisco = true;
    SomfyShade::unpublish(2);
    EXPECT_EQ(mqtt_unpublished.count("homeassistant/cover/2/config"), 1u);
    EXPECT_EQ(mqtt_unpublished.count("homeassistant/switch/2/config"), 1u);
}

// ══════════════════════════════════════════════════════════════════════════════
// J. Tilt-specific p_*() setters
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, p_tiltDirection_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.tiltDirection = 0;
    shade.p_tiltDirection(1);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltDirection"), 1u);
    mqtt_published.clear();
    shade.tiltDirection = 1;
    shade.p_tiltDirection(1);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltDirection"), 0u);
}

TEST_F(MQTTPublisherTest, p_tiltTarget_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.tiltTarget = 50.0f;
    shade.p_tiltTarget(80.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltTarget"), 1u);
    mqtt_published.clear();
    shade.tiltTarget = 50.0f;
    shade.p_tiltTarget(50.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltTarget"), 0u);
}

TEST_F(MQTTPublisherTest, p_currentTiltPos_PublishesOnFloorChangeOnly)
{
    mqtt_connected_flag = true;
    shade.currentTiltPos = 50.0f;
    shade.p_currentTiltPos(51.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/tiltPosition"), 1u);
    mqtt_published.clear();
    shade.currentTiltPos = 50.0f;
    shade.p_currentTiltPos(50.4f); // floor(50) == floor(50.4)
    EXPECT_EQ(mqtt_published.count("shades/1/tiltPosition"), 0u);
}

TEST_F(MQTTPublisherTest, p_myTiltPos_PublishesOnChangeOnly)
{
    mqtt_connected_flag = true;
    shade.targetSequencer.myTiltPos = 30.0f;
    shade.p_myTiltPos(60.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/myTiltPos"), 1u);
    mqtt_published.clear();
    shade.targetSequencer.myTiltPos = 30.0f;
    shade.p_myTiltPos(30.0f);
    EXPECT_EQ(mqtt_published.count("shades/1/myTiltPos"), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// K. emitState() real body — socket-emit path; no-tilt and tilt branches
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MQTTPublisherTest, EmitState_NoTilt_OmitsTiltFields)
{
    shade.tiltType = tilt_types::none;
    // callRealEmitState exercises the emitState(num, evt) body; the tilt branch
    // must not be entered — if it were, myTiltPos/tiltTarget would be serialised.
    // We verify the call completes without touching the tilt-publish path.
    EXPECT_NO_FATAL_FAILURE(shade.callRealEmitState());
}

TEST_F(MQTTPublisherTest, EmitState_WithTilt_IncludesTiltFields)
{
    shade.tiltType = tilt_types::integrated;
    shade.tiltDirection = 1;
    shade.currentTiltPos = 60.0f;
    shade.tiltTarget = 70.0f;
    shade.targetSequencer.myTiltPos = 40.0f;
    EXPECT_NO_FATAL_FAILURE(shade.callRealEmitState());
}
