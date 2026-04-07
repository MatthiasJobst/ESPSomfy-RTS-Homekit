// SomfyJSONSerializer.cpp — JSON validation, deserialization, and serialization for SomfyShade.
#include "SomfyShade.h"
#include "SomfyController.h"
#include "SomfyJSONSerializer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cstring>

extern SomfyShadeController somfy;
extern ConfigSettings settings;

int8_t SomfyJSONSerializer::validateJSON(JsonObject &obj) {
  int8_t ret = 0;
  shade_types type = shade->shadeType;
  if(obj.containsKey("shadeType")) {
    if(obj["shadeType"].is<const char *>()) {
      if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
        type = shade_types::roller;
      else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
        type = shade_types::ldrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
        type = shade_types::rdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
        type = shade_types::cdrapery;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
        type = shade_types::garage1;
      else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
        type = shade_types::garage3;
      else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
        type = shade_types::blind;
      else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
        type = shade_types::awning;
      else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
        type = shade_types::shutter;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
        type = shade_types::drycontact2;
      else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
        type = shade_types::drycontact;
    }
    else {
      shade->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
    }
  }
  if(obj.containsKey("proto")) {
    radio_proto proto = shade->proto;
    if(proto == radio_proto::GP_Relay || proto == radio_proto::GP_Remote) {
      // Check to see if we are using the up and or down
      // GPIOs anywhere else.
      uint8_t upPin = obj.containsKey("gpioUp") ? obj["gpioUp"].as<uint8_t>() : shade->gpioControl.gpioUp;
      uint8_t downPin = obj.containsKey("gpioDown") ? obj["gpioDown"].as<uint8_t>() : shade->gpioControl.gpioDown;
      uint8_t myPin = obj.containsKey("gpioMy") ? obj["gpioMy"].as<uint8_t>() : shade->gpioControl.gpioMy;
      if(type == shade_types::drycontact ||
        ((type == shade_types::garage1 || type == shade_types::lgate1 || type == shade_types::cgate1 || type == shade_types::rgate1)
        && proto == radio_proto::GP_Remote)) upPin = myPin = 255;
      else if(type == shade_types::drycontact2) myPin = 255;
      if(proto == radio_proto::GP_Relay) myPin = 255;
      if(somfy.transceiver.config.enabled) {
        if((upPin != 255 && somfy.transceiver.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -10;
      }
      if(settings.connType == conn_types_t::ethernet || settings.connType == conn_types_t::ethernetpref) {
        if((upPin != 255 && settings.Ethernet.usesPin(upPin)) ||
          (downPin != 255 && somfy.transceiver.usesPin(downPin)) ||
          (myPin != 255 && somfy.transceiver.usesPin(myPin)))
          ret = -11;
      }
      if(ret == 0) {
        for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
          SomfyShade *s = &somfy.shades[i];
          if(s->getShadeId() == shade->getShadeId() || s->getShadeId() == 255) continue;
          if((upPin != 255 && s->usesPin(upPin)) ||
            (downPin != 255 && s->usesPin(downPin)) ||
            (myPin != 255 && s->usesPin(myPin))){
            ret = -12;
            break;
          }
        }
      }
    }
  }
  return ret;
}

int8_t SomfyJSONSerializer::fromJSON(JsonObject &obj) {
  int8_t err = this->validateJSON(obj);
  if(err == 0) {
    if(obj.containsKey("name")) strlcpy(shade->name, obj["name"], sizeof(shade->name));
    if(obj.containsKey("roomId")) shade->roomId = obj["roomId"];
    if(obj.containsKey("upTime")) shade->setUpTime(obj["upTime"]);
    if(obj.containsKey("downTime")) shade->setDownTime(obj["downTime"]);
    if(obj.containsKey("remoteAddress")) shade->setRemoteAddress(obj["remoteAddress"]);
    if(obj.containsKey("tiltTime")) shade->setTiltTime(obj["tiltTime"]);
    if(obj.containsKey("stepSize")) shade->setStepSize(obj["stepSize"]);
    if(obj.containsKey("hasTilt")) shade->tiltType = static_cast<bool>(obj["hasTilt"]) ? tilt_types::none : tilt_types::tiltmotor;
    if(obj.containsKey("bitLength")) shade->bitLength = obj["bitLength"];
    if(obj.containsKey("proto")) shade->proto = static_cast<radio_proto>(obj["proto"].as<uint8_t>());
    if(obj.containsKey("sunSensor")) shade->setSunSensor(obj["sunSensor"]);
    if(obj.containsKey("simMy")) shade->setSimMy(obj["simMy"]);
    if(obj.containsKey("light")) shade->setLight(obj["light"]);
    if(obj.containsKey("gpioFlags")) shade->gpioControl.gpioFlags = obj["gpioFlags"];
    if(obj.containsKey("gpioLLTrigger")) {
      if(obj["gpioLLTrigger"].as<bool>())
        shade->gpioControl.gpioFlags |= (uint8_t)gpio_flags_t::LowLevelTrigger;
      else
        shade->gpioControl.gpioFlags &= ~(uint8_t)gpio_flags_t::LowLevelTrigger;
    }

    if(obj.containsKey("shadeType")) {
      if(obj["shadeType"].is<const char *>()) {
        if(strncmp(obj["shadeType"].as<const char *>(), "roller", 7) == 0)
          shade->shadeType = shade_types::roller;
        else if(strncmp(obj["shadeType"].as<const char *>(), "ldrapery", 9) == 0)
          shade->shadeType = shade_types::ldrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "rdrapery", 9) == 0)
          shade->shadeType = shade_types::rdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "cdrapery", 9) == 0)
          shade->shadeType = shade_types::cdrapery;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage1", 7) == 0)
          shade->shadeType = shade_types::garage1;
        else if(strncmp(obj["shadeType"].as<const char *>(), "garage3", 7) == 0)
          shade->shadeType = shade_types::garage3;
        else if(strncmp(obj["shadeType"].as<const char *>(), "blind", 5) == 0)
          shade->shadeType = shade_types::blind;
        else if(strncmp(obj["shadeType"].as<const char *>(), "awning", 7) == 0)
          shade->shadeType = shade_types::awning;
        else if(strncmp(obj["shadeType"].as<const char *>(), "shutter", 8) == 0)
          shade->shadeType = shade_types::shutter;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact2", 12) == 0)
          shade->shadeType = shade_types::drycontact2;
        else if(strncmp(obj["shadeType"].as<const char *>(), "drycontact", 11) == 0)
          shade->shadeType = shade_types::drycontact;
      }
      else {
        shade->shadeType = static_cast<shade_types>(obj["shadeType"].as<uint8_t>());
      }
    }
    if(obj.containsKey("flipCommands")) shade->flipCommands = obj["flipCommands"].as<bool>();
    if(obj.containsKey("flipPosition")) shade->setFlipPosition(obj["flipPosition"].as<bool>());
    if(obj.containsKey("repeats")) shade->repeats = obj["repeats"];
    if(obj.containsKey("tiltType")) {
      if(obj["tiltType"].is<const char *>()) {
        if(strncmp(obj["tiltType"].as<const char *>(), "none", 4) == 0)
          shade->tiltType = tilt_types::none;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltmotor", 9) == 0)
          shade->tiltType = tilt_types::tiltmotor;
        else if(strncmp(obj["tiltType"].as<const char *>(), "integ", 5) == 0)
          shade->tiltType = tilt_types::integrated;
        else if(strncmp(obj["tiltType"].as<const char *>(), "tiltonly", 8) == 0)
          shade->tiltType = tilt_types::tiltonly;
      }
      else {
        shade->tiltType = static_cast<tilt_types>(obj["tiltType"].as<uint8_t>());
      }
    }
    if(obj.containsKey("linkedAddresses")) {
      uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
      memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
      JsonArray arr = obj["linkedAddresses"];
      uint8_t i = 0;
      for(uint32_t addr : arr) {
        linkedAddresses[i++] = addr;
      }
      for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
        shade->getLinkedRemote(j).setRemoteAddress(linkedAddresses[j]);
      }
    }
    if(obj.containsKey("flags")) shade->flags = obj["flags"];
    if(shade->proto == radio_proto::GP_Remote || shade->proto == radio_proto::GP_Relay) {
      if(obj.containsKey("gpioUp")) shade->gpioControl.gpioUp = obj["gpioUp"];
      if(obj.containsKey("gpioDown")) shade->gpioControl.gpioDown = obj["gpioDown"];
      gpio_set_direction((gpio_num_t)shade->gpioControl.gpioUp, GPIO_MODE_OUTPUT);
      gpio_set_direction((gpio_num_t)shade->gpioControl.gpioDown, GPIO_MODE_OUTPUT);
    }
    if(shade->proto == radio_proto::GP_Remote) {
      if(obj.containsKey("gpioMy")) shade->gpioControl.gpioMy = obj["gpioMy"];
      gpio_set_direction((gpio_num_t)shade->gpioControl.gpioMy, GPIO_MODE_OUTPUT);
    }
  }
  return err;
}

void SomfyJSONSerializer::toJSONRef(JsonResponse &json) {
  json.addElem("shadeId", shade->getShadeId());
  json.addElem("roomId", shade->roomId);
  json.addElem("name", shade->name);
  json.addElem("remoteAddress", (uint32_t)shade->getRemoteAddress());
  json.addElem("paired", shade->paired);
  json.addElem("shadeType", static_cast<uint8_t>(shade->shadeType));
  json.addElem("flipCommands", shade->flipCommands);
  json.addElem("flipPosition", shade->flipCommands);
  json.addElem("bitLength", shade->bitLength);
  json.addElem("proto", static_cast<uint8_t>(shade->proto));
  json.addElem("flags", shade->flags);
  json.addElem("sunSensor", shade->hasSunSensor());
  json.addElem("hasLight", shade->hasLight());
  json.addElem("repeats", shade->repeats);
}

void SomfyJSONSerializer::toJSON(JsonResponse &json) {
  json.addElem("shadeId", shade->getShadeId());
  json.addElem("roomId", shade->roomId);
  json.addElem("name", shade->name);
  json.addElem("remoteAddress", (uint32_t)shade->getRemoteAddress());
  json.addElem("upTime", (uint32_t)shade->getUpTime());
  json.addElem("downTime", (uint32_t)shade->getDownTime());
  json.addElem("paired", shade->paired);
  json.addElem("lastRollingCode", (uint32_t)shade->lastRollingCode);
  json.addElem("position", shade->transformPosition(shade->currentPos));
  json.addElem("tiltType", static_cast<uint8_t>(shade->tiltType));
  json.addElem("tiltPosition", shade->transformPosition(shade->currentTiltPos));
  json.addElem("tiltDirection", shade->tiltDirection);
  json.addElem("tiltTime", (uint32_t)shade->getTiltTime());
  json.addElem("stepSize", (uint32_t)shade->getStepSize());
  json.addElem("tiltTarget", shade->transformPosition(shade->tiltTarget));
  json.addElem("target", shade->transformPosition(shade->target));
  json.addElem("myPos", shade->transformPosition(shade->getMyPos()));
  json.addElem("myTiltPos", shade->transformPosition(shade->getMyTiltPos()));
  json.addElem("direction", shade->direction);
  json.addElem("shadeType", static_cast<uint8_t>(shade->shadeType));
  json.addElem("bitLength", shade->bitLength);
  json.addElem("proto", static_cast<uint8_t>(shade->proto));
  json.addElem("flags", shade->flags);
  json.addElem("flipCommands", shade->flipCommands);
  json.addElem("flipPosition", shade->getFlipPosition());
  json.addElem("inGroup", shade->isInGroup());
  json.addElem("sunSensor", shade->hasSunSensor());
  json.addElem("light", shade->hasLight());
  json.addElem("repeats", shade->repeats);
  json.addElem("sortOrder", shade->sortOrder);
  json.addElem("gpioUp", shade->gpioControl.gpioUp);
  json.addElem("gpioDown", shade->gpioControl.gpioDown);
  json.addElem("gpioMy", shade->gpioControl.gpioMy);
  json.addElem("gpioLLTrigger", ((shade->gpioControl.gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0) ? false : true);
  json.addElem("simMy", shade->simMy());
  json.beginArray("linkedRemotes");
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    SomfyLinkedRemote &lremote = shade->getLinkedRemote(i);
    if(lremote.getRemoteAddress() != 0) {
      json.beginObject();
      lremote.toJSON(json);
      json.endObject();
    }
  }
  json.endArray();
}
