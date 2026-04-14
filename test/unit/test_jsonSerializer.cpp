// test_jsonSerializer.cpp — tests for SomfyShade JSON methods:
//   validateJSON() — shadeType string/uint8 parsing, GPIO pin-conflict checks
//                    (-10 transceiver, -11 ethernet, -12 other shade)
//   fromJSON()     — applies every field; early-return on validation error;
//                    shadeType/tiltType string+uint8 branches; linkedAddresses;
//                    GPIO direction setup for GP_Relay / GP_Remote
//   toJSONRef()    — execution coverage (WResp is a no-op stub)
//   toJSON()       — execution coverage including linkedRemotes loop body

#include "TestableShade.h"
#include "nvs.h"
#include "MQTT.h"
#include "SomfyController.h"
#include <ArduinoJson.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern SomfyShadeController somfy;
extern bool              mqtt_connected_flag;
extern std::unordered_map<std::string, std::string> mqtt_published;
extern bool              transceiver_stub_uses_pin;
extern ConfigSettings    settings;

using ::testing::AnyNumber;
using ::testing::_;

// ── fixture ────────────────────────────────────────────────────────────────

class JsonSerializerTest : public ::testing::Test {
protected:
    TestableShade shade;

    void SetUp() override {
        shade.setShadeId(1);
        shade.setRemoteAddress(0x112233);
        shade.shadeType        = shade_types::roller;
        shade.tiltType         = tilt_types::none;
        shade.currentPos       = 50.0f;
        shade.currentTiltPos   = 0.0f;
        shade.target           = 50.0f;
        shade.tiltTarget       = 0.0f;
        shade.direction        = 0;
        shade.setFlipPosition(false);
        shade.setProto(radio_proto::RTS);
        shade.setUpTime(10000);
        shade.setDownTime(10000);
        shade.setTiltTime(7000);
        shade.setGpioUp(5);
        shade.setGpioDown(6);
        shade.setGpioMy(7);

        EXPECT_CALL(shade, emitState(_, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitState(_)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(shade, emitCommand(_, _, _, _, _)).Times(AnyNumber());

        nvs_stub_reset_all();
        mqtt_connected_flag = false;
        mqtt_published.clear();
        transceiver_stub_uses_pin         = false;
        somfy.transceiver.config.enabled  = false;
        settings.connType                 = conn_types_t::wifi;

        // Ensure all somfy shade slots are inactive (shadeId=255)
        for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
            somfy.shades[i].setShadeId(255);
            somfy.shades[i].proto = radio_proto::RTS;
            somfy.shades[i].gpioControl.gpioUp   = 0;
            somfy.shades[i].gpioControl.gpioDown = 0;
            somfy.shades[i].gpioControl.gpioMy   = 0;
        }
    }

    void TearDown() override {
        transceiver_stub_uses_pin         = false;
        somfy.transceiver.config.enabled  = false;
        settings.connType                 = conn_types_t::wifi;
        for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++)
            somfy.shades[i].setShadeId(255);
    }

    // Build a JsonObject from a JSON string for use in fromJSON / validateJSON.
    // The caller owns the doc and must keep it alive while the object is used.
    static void parseInto(JsonDocument &doc, const char *json) {
        deserializeJson(doc, json);
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A. validateJSON — return value and shadeType string parsing
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(JsonSerializerTest, Validate_NoKeys_ReturnsZero) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

TEST_F(JsonSerializerTest, Validate_ShadeType_Uint8_AppliedToThis) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    obj["shadeType"] = static_cast<uint8_t>(shade_types::blind);
    int8_t ret = shade.validateJSON(obj);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(shade.shadeType, shade_types::blind);
}

TEST_F(JsonSerializerTest, Validate_ShadeType_AllStrings) {
    const char *types[] = {
        "roller", "ldrapery", "rdrapery", "cdrapery",
        "garage1", "garage3", "blind", "awning",
        "shutter", "drycontact2", "drycontact"
    };
    for (const char *s : types) {
        JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
        obj["shadeType"] = s;
        EXPECT_EQ(shade.validateJSON(obj), 0) << "shadeType: " << s;
    }
}

// Proto key present but shade proto is RTS — pin check block is skipped.
TEST_F(JsonSerializerTest, Validate_ProtoKey_NotGPType_SkipsPinCheck) {
    shade.setProto(radio_proto::RTS);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::RTS);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// Transceiver enabled and usesPin returns true → -10.
TEST_F(JsonSerializerTest, Validate_TransceiverConflict_ReturnsMinus10) {
    shade.setProto(radio_proto::GP_Relay);
    somfy.transceiver.config.enabled = true;
    transceiver_stub_uses_pin        = true;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), -10);
}

// Transceiver disabled but connType==ethernet and transceiver.usesPin hits on downPin → -11.
// (The ethernet branch in production code reuses transceiver.usesPin for downPin/myPin.)
TEST_F(JsonSerializerTest, Validate_EthernetConflict_ReturnsMinus11) {
    shade.setProto(radio_proto::GP_Relay);
    somfy.transceiver.config.enabled = false;
    transceiver_stub_uses_pin        = true;
    settings.connType                = conn_types_t::ethernet;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), -11);
}

// Another shade (slot 0) uses the same downPin → -12.
TEST_F(JsonSerializerTest, Validate_OtherShadeConflict_ReturnsMinus12) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setGpioDown(10);
    // Put a different shade in slot 0 with gpioDown=10 and GP_Relay proto
    somfy.shades[0].setShadeId(2);  // non-255 → active
    somfy.shades[0].proto = radio_proto::GP_Relay;
    somfy.shades[0].gpioControl.gpioDown = 10;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), -12);
}

// GP_Relay, no conflicts at all → 0.
TEST_F(JsonSerializerTest, Validate_GPRelay_NoConflict_ReturnsZero) {
    shade.setProto(radio_proto::GP_Relay);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// GP_Remote + drycontact type → upPin and myPin forced to 255, only downPin checked.
TEST_F(JsonSerializerTest, Validate_GPRemote_DryContact_UpMyPinsIgnored) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::drycontact;
    // No conflict expected because upPin/myPin are set to 255
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["shadeType"] = "drycontact";
    obj["proto"]     = static_cast<uint8_t>(radio_proto::GP_Remote);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// GP_Remote + garage1 type → upPin and myPin forced to 255.
TEST_F(JsonSerializerTest, Validate_GPRemote_Garage1_UpMyPinsIgnored) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::roller;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["shadeType"] = "garage1";
    obj["proto"]     = static_cast<uint8_t>(radio_proto::GP_Remote);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// GP_Remote + drycontact2 → else-if branch: myPin forced to 255.
TEST_F(JsonSerializerTest, Validate_GPRemote_DryContact2_MyPinIgnored) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::roller;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["shadeType"] = "drycontact2";
    obj["proto"]     = static_cast<uint8_t>(radio_proto::GP_Remote);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// gpioUp/Down/My overridden from JSON object (exercises the ternary branches).
TEST_F(JsonSerializerTest, Validate_GpioPinsFromObj) {
    shade.setProto(radio_proto::GP_Remote);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"]   = static_cast<uint8_t>(radio_proto::GP_Remote);
    obj["gpioUp"]  = (uint8_t)20;
    obj["gpioDown"]= (uint8_t)21;
    obj["gpioMy"]  = (uint8_t)22;
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// B. fromJSON — field application and early-return on validation error
// ══════════════════════════════════════════════════════════════════════════════

// validateJSON returns non-zero → fromJSON propagates error without applying fields.
TEST_F(JsonSerializerTest, FromJSON_ValidationError_EarlyReturn) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setGpioDown(10);
    somfy.shades[0].setShadeId(2);
    somfy.shades[0].proto = radio_proto::GP_Relay;
    somfy.shades[0].gpioControl.gpioDown = 10;

    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    obj["name"]  = "ShouldNotApply";
    int8_t ret = shade.fromJSON(obj);
    EXPECT_EQ(ret, -12);
    EXPECT_STREQ(shade.name, "");  // field was not applied
}

// name, roomId, upTime, downTime, remoteAddress, tiltTime, stepSize applied.
TEST_F(JsonSerializerTest, FromJSON_BasicFields_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["name"]          = "Living Room";
    obj["roomId"]        = (uint8_t)3;
    obj["upTime"]        = (uint32_t)8000;
    obj["downTime"]      = (uint32_t)9000;
    obj["remoteAddress"] = (uint32_t)0xAABBCC;
    obj["tiltTime"]      = (uint32_t)5000;
    obj["stepSize"]      = (uint16_t)50;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_STREQ(shade.name, "Living Room");
    EXPECT_EQ(shade.roomId,   3);
    EXPECT_EQ(shade.getUpTime(),   8000u);
    EXPECT_EQ(shade.getDownTime(), 9000u);
    EXPECT_EQ(shade.getRemoteAddress(), 0xAABBCCu);
    EXPECT_EQ(shade.getTiltTime(), 5000u);
    EXPECT_EQ(shade.getStepSize(), 50u);
}

// hasTilt=true sets tiltType=none; hasTilt=false sets tiltType=tiltmotor.
TEST_F(JsonSerializerTest, FromJSON_HasTilt_True_SetsTiltTypeNone) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["hasTilt"] = true;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.tiltType, tilt_types::none);
}

TEST_F(JsonSerializerTest, FromJSON_HasTilt_False_SetsTiltMotor) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["hasTilt"] = false;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.tiltType, tilt_types::tiltmotor);
}

// bitLength and proto applied.
TEST_F(JsonSerializerTest, FromJSON_BitLengthAndProto_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["bitLength"] = (uint8_t)80;
    obj["proto"]     = static_cast<uint8_t>(radio_proto::RTS);
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.bitLength, 80);
    EXPECT_EQ(shade.proto, radio_proto::RTS);
}

// sunSensor, simMy, light flags applied via setters.
TEST_F(JsonSerializerTest, FromJSON_SunSensor_SimMy_Light_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["sunSensor"] = true;
    obj["simMy"]     = true;
    obj["light"]     = true;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_TRUE(shade.hasSunSensor());
    EXPECT_TRUE(shade.simMy());
    EXPECT_TRUE(shade.hasLight());
}

// gpioFlags applied directly.
TEST_F(JsonSerializerTest, FromJSON_GpioFlags_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["gpioFlags"] = (uint8_t)0x03;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.gpioControl.gpioFlags, 0x03);
}

// gpioLLTrigger=true sets the LowLevelTrigger bit.
TEST_F(JsonSerializerTest, FromJSON_GpioLLTrigger_True_SetsBit) {
    shade.setGpioFlags(0x00);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["gpioLLTrigger"] = true;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_NE(shade.gpioControl.gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger, 0);
}

// gpioLLTrigger=false clears the LowLevelTrigger bit.
TEST_F(JsonSerializerTest, FromJSON_GpioLLTrigger_False_ClearsBit) {
    shade.setGpioFlags(0xFF);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["gpioLLTrigger"] = false;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.gpioControl.gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger, 0);
}

// shadeType as string — all 11 values applied to this->shadeType.
TEST_F(JsonSerializerTest, FromJSON_ShadeType_AllStrings) {
    const struct { const char *s; shade_types t; } cases[] = {
        { "roller",      shade_types::roller      },
        { "ldrapery",    shade_types::ldrapery    },
        { "rdrapery",    shade_types::rdrapery    },
        { "cdrapery",    shade_types::cdrapery    },
        { "garage1",     shade_types::garage1     },
        { "garage3",     shade_types::garage3     },
        { "blind",       shade_types::blind       },
        { "awning",      shade_types::awning      },
        { "shutter",     shade_types::shutter     },
        { "drycontact2", shade_types::drycontact2 },
        { "drycontact",  shade_types::drycontact  },
    };
    for (auto &c : cases) {
        JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
        obj["shadeType"] = c.s;
        shade.fromJSON(obj);
        EXPECT_EQ(shade.shadeType, c.t) << "shadeType string: " << c.s;
    }
}

// shadeType as uint8_t applied.
TEST_F(JsonSerializerTest, FromJSON_ShadeType_Uint8_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["shadeType"] = static_cast<uint8_t>(shade_types::awning);
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.shadeType, shade_types::awning);
}

// flipCommands, flipPosition, repeats applied.
TEST_F(JsonSerializerTest, FromJSON_FlipAndRepeats_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["flipCommands"] = true;
    obj["flipPosition"] = true;
    obj["repeats"]      = (uint8_t)4;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_TRUE(shade.flipCommands);
    EXPECT_TRUE(shade.getFlipPosition());
    EXPECT_EQ(shade.repeats, 4);
}

// tiltType string: none / tiltmotor / integ (→ integrated) / tiltonly.
TEST_F(JsonSerializerTest, FromJSON_TiltType_AllStrings) {
    const struct { const char *s; tilt_types t; } cases[] = {
        { "none",      tilt_types::none       },
        { "tiltmotor", tilt_types::tiltmotor  },
        { "integ",     tilt_types::integrated },
        { "tiltonly",  tilt_types::tiltonly   },
    };
    for (auto &c : cases) {
        JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
        obj["tiltType"] = c.s;
        shade.fromJSON(obj);
        EXPECT_EQ(shade.tiltType, c.t) << "tiltType string: " << c.s;
    }
}

TEST_F(JsonSerializerTest, FromJSON_TiltType_Uint8_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["tiltType"] = static_cast<uint8_t>(tilt_types::integrated);
    shade.fromJSON(obj);
    EXPECT_EQ(shade.tiltType, tilt_types::integrated);
}

// linkedAddresses array populates linkedRemotes.
TEST_F(JsonSerializerTest, FromJSON_LinkedAddresses_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    JsonArray arr = obj["linkedAddresses"].to<JsonArray>();
    arr.add((uint32_t)0x111111);
    arr.add((uint32_t)0x222222);
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.getLinkedRemote(0).getRemoteAddress(), 0x111111u);
    EXPECT_EQ(shade.getLinkedRemote(1).getRemoteAddress(), 0x222222u);
}

// flags field applied.
TEST_F(JsonSerializerTest, FromJSON_Flags_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["flags"] = (uint8_t)0x05;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.getFlags().getFlags(), 0x05);
}

// GP_Relay: gpioUp and gpioDown updated; gpio_set_direction called (no crash).
TEST_F(JsonSerializerTest, FromJSON_GPRelay_GpioUpDown_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"]    = static_cast<uint8_t>(radio_proto::GP_Relay);
    obj["gpioUp"]   = (uint8_t)15;
    obj["gpioDown"] = (uint8_t)16;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.gpioControl.gpioUp,   15);
    EXPECT_EQ(shade.gpioControl.gpioDown, 16);
}

// GP_Remote: gpioUp, gpioDown, and gpioMy all updated; gpio_set_direction called.
TEST_F(JsonSerializerTest, FromJSON_GPRemote_GpioUpDownMy_Applied) {
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"]    = static_cast<uint8_t>(radio_proto::GP_Remote);
    obj["gpioUp"]   = (uint8_t)20;
    obj["gpioDown"] = (uint8_t)21;
    obj["gpioMy"]   = (uint8_t)22;
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.gpioControl.gpioUp,   20);
    EXPECT_EQ(shade.gpioControl.gpioDown, 21);
    EXPECT_EQ(shade.gpioControl.gpioMy,   22);
}

// GP_Remote with proto already set on the shade (not via JSON key):
// gpio_set_direction for My is still called even without gpioMy in the JSON.
TEST_F(JsonSerializerTest, FromJSON_GPRemote_AlreadySet_GpioDirectionCalled) {
    shade.setProto(radio_proto::GP_Remote);
    shade.setGpioUp(25);
    shade.setGpioDown(26);
    shade.setGpioMy(27);
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    // Include proto key so the proto block is entered (but proto is already GP_Remote)
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Remote);
    EXPECT_EQ(shade.fromJSON(obj), 0);
    EXPECT_EQ(shade.gpioControl.gpioMy, 27);  // unchanged, direction was set
}

// ══════════════════════════════════════════════════════════════════════════════
// C. toJSONRef — execution coverage (WResp is a no-op stub, no assertions)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(JsonSerializerTest, ToJSONRef_ExecutesWithoutCrash) {
    shade.name[0] = '\0';
    shade.paired  = true;
    shade.shadeType = shade_types::blind;
    JsonResponse json;
    shade.toJSONRef(json);
    // All addElem calls are no-ops; test verifies execution path is reachable.
}

// ══════════════════════════════════════════════════════════════════════════════
// D. toJSON — execution coverage including linkedRemotes loop body
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(JsonSerializerTest, ToJSON_NoLinkedRemotes_ExecutesWithoutCrash) {
    JsonResponse json;
    shade.toJSON(json);
}

// With a linked remote present the loop body (beginObject / toJSON / endObject) runs.
TEST_F(JsonSerializerTest, ToJSON_WithLinkedRemote_LoopBodyExecuted) {
    shade.getLinkedRemote(0).setRemoteAddress(0xDEADBEEF);
    shade.getLinkedRemote(0).setRollingCode(42);
    JsonResponse json;
    shade.toJSON(json);
    // Clean up
    shade.getLinkedRemote(0).setRemoteAddress(0);
}

// gpioLLTrigger flag set → the true branch of the ternary in toJSON is taken.
TEST_F(JsonSerializerTest, ToJSON_GpioLLTrigger_Set) {
    shade.setGpioFlags(static_cast<uint8_t>(gpio_flags_t::LowLevelTrigger));
    JsonResponse json;
    shade.toJSON(json);
}

// ── Validate: transceiver hits on downPin (upPin==255) → -10 ─────────────────
// GP_Relay + drycontact: upPin and myPin are forced to 255, downPin remains.
// With transceiver enabled and usesPin=true, line 59 (downPin check) is reached.
TEST_F(JsonSerializerTest, Validate_TransceiverConflict_OnDownPin_ReturnsMinus10) {
    shade.setProto(radio_proto::GP_Relay);
    shade.shadeType = shade_types::drycontact;  // upPin forced to 255
    somfy.transceiver.config.enabled = true;
    transceiver_stub_uses_pin        = true;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), -10);
}

// ── Validate: ethernet branch, myPin check (line 66) is reached ──────────────
// GP_Remote + roller: upPin/downPin/myPin all set. EthernetSettings.usesPin
// always returns false, so line 65 is false; line 66 (myPin check) is evaluated.
TEST_F(JsonSerializerTest, Validate_EthernetBranch_MyPinCheck_Reached) {
    shade.setProto(radio_proto::GP_Remote);
    shade.shadeType = shade_types::roller;
    somfy.transceiver.config.enabled = false;
    transceiver_stub_uses_pin        = false;
    settings.connType                = conn_types_t::ethernet;
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Remote);
    // No conflict (all usesPin calls return false) → 0
    EXPECT_EQ(shade.validateJSON(obj), 0);
}

// ── Validate: shade in loop checked but no conflict → loop body continues ─────
// Sets up a non-conflicting shade (RTS proto, usesPin returns false) in slot 0.
// This makes the for-loop body reach the closing } (line 79) without breaking.
TEST_F(JsonSerializerTest, Validate_OtherShade_NoConflict_LoopContinues) {
    shade.setProto(radio_proto::GP_Relay);
    shade.setGpioDown(10);
    // Slot 0: different shade, RTS proto (usesPin always false)
    somfy.shades[0].setShadeId(2);
    somfy.shades[0].proto = radio_proto::RTS;
    somfy.shades[0].gpioControl.gpioDown = 10;  // same pin but RTS → usesPin returns false
    JsonDocument doc; JsonObject obj = doc.to<JsonObject>();
    obj["proto"] = static_cast<uint8_t>(radio_proto::GP_Relay);
    EXPECT_EQ(shade.validateJSON(obj), 0);
}
