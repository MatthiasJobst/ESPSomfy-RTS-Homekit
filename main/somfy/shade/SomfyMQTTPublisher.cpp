// SomfyMQTTPublisher.cpp — MQTT and socket-emit implementations for SomfyShade.
// All methods read shade state via the back-pointer set by the SomfyShade() constructor.
#include "SomfyShade.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "MQTT.h"
#include "HomeKit.h"
#include "ConfigFile.h"
#include "SomfyMQTTPublisher.h"

static char mqttTopicBuffer[55];

extern SocketEmitter        sockEmit;
extern ConfigSettings       settings;
extern MQTTClass            mqtt;

// ── Low-level topic builders ───────────────────────────────────────────────

bool SomfyMQTTPublisher::publish(const char *topic, int8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyMQTTPublisher::publish(const char *topic, const char *val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyMQTTPublisher::publish(const char *topic, uint8_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyMQTTPublisher::publish(const char *topic, uint32_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyMQTTPublisher::publish(const char *topic, uint16_t val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

bool SomfyMQTTPublisher::publish(const char *topic, bool val, bool retain) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", shade->getShadeId(), topic);
    mqtt.publish(mqttTopicBuffer, val, retain);
    return true;
  }
  return false;
}

// ── Full publish / unpublish ───────────────────────────────────────────────

void SomfyMQTTPublisher::publish() {
  if(mqtt.connected()) {
    this->publish("shadeId",       shade->getShadeId(), true);
    this->publish("name",          shade->name, true);
    this->publish("remoteAddress", shade->getRemoteAddress(), true);
    this->publish("shadeType",     static_cast<uint8_t>(shade->shadeType), true);
    this->publish("tiltType",      static_cast<uint8_t>(shade->tiltType), true);
    this->publish("flags",         shade->flags.getFlags(), true);
    this->publish("flipCommands",  shade->flipCommands, true);
    this->publish("flipPosition",  shade->getFlipPosition(), true);
    shade->publishState();
    shade->publishDisco();
    sockEmit.loop();
  }
}

void SomfyMQTTPublisher::unpublish() { SomfyMQTTPublisher::unpublish(shade->getShadeId()); }

void SomfyMQTTPublisher::unpublish(uint8_t id) {
  if(mqtt.connected()) {
    SomfyMQTTPublisher::unpublish(id, "shadeId");
    SomfyMQTTPublisher::unpublish(id, "name");
    SomfyMQTTPublisher::unpublish(id, "remoteAddress");
    SomfyMQTTPublisher::unpublish(id, "shadeType");
    SomfyMQTTPublisher::unpublish(id, "tiltType");
    SomfyMQTTPublisher::unpublish(id, "flags");
    SomfyMQTTPublisher::unpublish(id, "flipCommands");
    SomfyMQTTPublisher::unpublish(id, "flipPosition");
    SomfyMQTTPublisher::unpublish(id, "position");
    SomfyMQTTPublisher::unpublish(id, "direction");
    SomfyMQTTPublisher::unpublish(id, "target");
    SomfyMQTTPublisher::unpublish(id, "lastRollingCode");
    SomfyMQTTPublisher::unpublish(id, "mypos");
    SomfyMQTTPublisher::unpublish(id, "myTiltPos");
    SomfyMQTTPublisher::unpublish(id, "tiltDirection");
    SomfyMQTTPublisher::unpublish(id, "tiltPosition");
    SomfyMQTTPublisher::unpublish(id, "tiltTarget");
    SomfyMQTTPublisher::unpublish(id, "windy");
    SomfyMQTTPublisher::unpublish(id, "sunny");
    if(settings.MQTT.pubDisco) {
      char topic[128] = "";
      snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
      snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, id);
      mqtt.unpublish(topic);
    }
  }
}

void SomfyMQTTPublisher::unpublish(uint8_t id, const char *topic) {
  if(mqtt.connected()) {
    snprintf(mqttTopicBuffer, sizeof(mqttTopicBuffer), "shades/%u/%s", id, topic);
    mqtt.unpublish(mqttTopicBuffer);
  }
}

// ── State snapshots ───────────────────────────────────────────────────────

void SomfyMQTTPublisher::publishState() {
  homekit.notifyShadeState(shade);
  if(mqtt.connected()) {
    this->publish("position",      shade->transformPosition(shade->currentPos), true);
    this->publish("direction",     shade->direction, true);
    this->publish("target",        shade->transformPosition(shade->target), true);
    this->publish("lastRollingCode", shade->lastRollingCode);
    this->publish("mypos",         shade->transformPosition(shade->getMyPos()), true);
    this->publish("myTiltPos",     shade->transformPosition(shade->getMyTiltPos()), true);
    if(shade->tiltType != tilt_types::none) {
      this->publish("tiltDirection", shade->tiltDirection, true);
      this->publish("tiltPosition",  shade->transformPosition(shade->currentTiltPos), true);
      this->publish("tiltTarget",    shade->transformPosition(shade->tiltTarget), true);
    }
    if(shade->hasSunSensor()) {
      this->publish("sunFlag", shade->flags.hasSunFlag());
      this->publish("sunny",   shade->flags.isSunny());
    }
    this->publish("windy", shade->flags.isWindy());
  }
}

void SomfyMQTTPublisher::publishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  snprintf(topic, sizeof(topic), "%s/shades/%d", settings.MQTT.rootTopic, shade->getShadeId());
  obj["~"] = topic;
  JsonObject dobj = obj.createNestedObject("device");
  dobj["hw_version"] = settings.fwVersion.name;
  dobj["name"] = settings.hostname;
  dobj["mf"] = "rstrouse";
  JsonArray arrids = dobj.createNestedArray("identifiers");
  snprintf(topic, sizeof(topic), "mqtt_espsomfyrts_%s", settings.serverId);
  arrids.add(topic);
  dobj["via_device"] = topic;
  dobj["model"] = "ESPSomfy-RTS MQTT";
  snprintf(topic, sizeof(topic), "%s/status", settings.MQTT.rootTopic);
  obj["availability_topic"] = topic;
  obj["payload_available"] = "online";
  obj["payload_not_available"] = "offline";
  obj["name"] = shade->name;
  snprintf(topic, sizeof(topic), "mqtt_%s_shade%d", settings.serverId, shade->getShadeId());
  obj["unique_id"] = topic;
  switch(shade->shadeType) {
    case shade_types::blind:
      obj["device_class"] = "blind";
      obj["payload_close"] = shade->getFlipPosition() ? "-1" : "1";
      obj["payload_open"]  = shade->getFlipPosition() ? "1"  : "-1";
      obj["position_open"]   = shade->getFlipPosition() ? 100 : 0;
      obj["position_closed"] = shade->getFlipPosition() ? 0   : 100;
      obj["state_closing"]   = shade->getFlipPosition() ? "-1" : "1";
      obj["state_opening"]   = shade->getFlipPosition() ? "1"  : "-1";
      break;
    case shade_types::lgate:
    case shade_types::cgate:
    case shade_types::rgate:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
    case shade_types::ldrapery:
    case shade_types::rdrapery:
    case shade_types::cdrapery:
      obj["device_class"] = "curtain";
      obj["payload_close"] = shade->getFlipPosition() ? "-1" : "1";
      obj["payload_open"]  = shade->getFlipPosition() ? "1"  : "-1";
      obj["position_open"]   = shade->getFlipPosition() ? 100 : 0;
      obj["position_closed"] = shade->getFlipPosition() ? 0   : 100;
      obj["state_closing"]   = shade->getFlipPosition() ? "-1" : "1";
      obj["state_opening"]   = shade->getFlipPosition() ? "1"  : "-1";
      break;
    case shade_types::garage1:
    case shade_types::garage3:
      obj["device_class"] = "garage";
      obj["payload_close"] = shade->getFlipPosition() ? "-1" : "1";
      obj["payload_open"]  = shade->getFlipPosition() ? "1"  : "-1";
      obj["position_open"]   = shade->getFlipPosition() ? 100 : 0;
      obj["position_closed"] = shade->getFlipPosition() ? 0   : 100;
      obj["state_closing"]   = shade->getFlipPosition() ? "-1" : "1";
      obj["state_opening"]   = shade->getFlipPosition() ? "1"  : "-1";
      break;
    case shade_types::awning:
      obj["device_class"] = "awning";
      obj["payload_close"] = shade->getFlipPosition() ? "1"  : "-1";
      obj["payload_open"]  = shade->getFlipPosition() ? "-1" : "1";
      obj["position_open"]   = shade->getFlipPosition() ? 0   : 100;
      obj["position_closed"] = shade->getFlipPosition() ? 100 : 0;
      obj["state_closing"]   = shade->getFlipPosition() ? "1"  : "-1";
      obj["state_opening"]   = shade->getFlipPosition() ? "-1" : "1";
      break;
    case shade_types::shutter:
      obj["device_class"] = "shutter";
      obj["payload_close"] = shade->getFlipPosition() ? "-1" : "1";
      obj["payload_open"]  = shade->getFlipPosition() ? "1"  : "-1";
      obj["position_open"]   = shade->getFlipPosition() ? 100 : 0;
      obj["position_closed"] = shade->getFlipPosition() ? 0   : 100;
      obj["state_closing"]   = shade->getFlipPosition() ? "-1" : "1";
      obj["state_opening"]   = shade->getFlipPosition() ? "1"  : "-1";
      break;
    case shade_types::drycontact2:
    case shade_types::drycontact:
      break;
    default:
      obj["device_class"] = "shade";
      obj["payload_close"] = shade->getFlipPosition() ? "-1" : "1";
      obj["payload_open"]  = shade->getFlipPosition() ? "1"  : "-1";
      obj["position_open"]   = shade->getFlipPosition() ? 100 : 0;
      obj["position_closed"] = shade->getFlipPosition() ? 0   : 100;
      obj["state_closing"]   = shade->getFlipPosition() ? "-1" : "1";
      obj["state_opening"]   = shade->getFlipPosition() ? "1"  : "-1";
      break;
  }
  if(shade->shadeType != shade_types::drycontact && shade->shadeType != shade_types::drycontact2) {
    if(shade->tiltType != tilt_types::tiltonly) {
      obj["command_topic"]      = "~/direction/set";
      obj["position_topic"]     = "~/position";
      obj["set_position_topic"] = "~/target/set";
      obj["state_topic"]        = "~/direction";
      obj["payload_stop"]       = "0";
      obj["state_stopped"]      = "0";
    }
    else {
      obj["payload_close"] = nullptr;
      obj["payload_open"]  = nullptr;
      obj["payload_stop"]  = nullptr;
    }
    if(shade->tiltType != tilt_types::none) {
      obj["tilt_command_topic"] = "~/tiltTarget/set";
      obj["tilt_status_topic"]  = "~/tiltPosition";
    }
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, shade->getShadeId());
  }
  else {
    obj["payload_on"]  = 100;
    obj["payload_off"] = 0;
    obj["state_off"]   = 0;
    obj["state_on"]    = 100;
    obj["state_topic"]   = "~/position";
    obj["command_topic"] = "~/target/set";
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, shade->getShadeId());
  }
  obj["enabled_by_default"] = true;
  mqtt.publishDisco(topic, obj, true);
}

void SomfyMQTTPublisher::unpublishDisco() {
  if(!mqtt.connected() || !settings.MQTT.pubDisco) return;
  char topic[128] = "";
  if(shade->shadeType != shade_types::drycontact && shade->shadeType != shade_types::drycontact2)
    snprintf(topic, sizeof(topic), "%s/cover/%d/config", settings.MQTT.discoTopic, shade->getShadeId());
  else
    snprintf(topic, sizeof(topic), "%s/switch/%d/config", settings.MQTT.discoTopic, shade->getShadeId());
  mqtt.unpublish(topic);
}

// ── Socket + MQTT event emitters ─────────────────────────────────────────

void SomfyMQTTPublisher::emitState(uint8_t num, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId",      shade->getShadeId());
  json->addElem("type",         static_cast<uint8_t>(shade->shadeType));
  json->addElem("remoteAddress",(uint32_t)shade->getRemoteAddress());
  json->addElem("name",         shade->name);
  json->addElem("direction",    shade->direction);
  json->addElem("position",     shade->transformPosition(shade->currentPos));
  json->addElem("target",       shade->transformPosition(shade->target));
  json->addElem("myPos",        shade->transformPosition(shade->getMyPos()));
  json->addElem("tiltType",     static_cast<uint8_t>(shade->tiltType));
  json->addElem("flipCommands", shade->flipCommands);
  json->addElem("flipPosition", shade->getFlipPosition());
  json->addElem("flags",        shade->flags.getFlags());
  json->addElem("sunSensor",    shade->hasSunSensor());
  json->addElem("light",        shade->hasLight());
  json->addElem("sortOrder",    shade->sortOrder);
  if(shade->tiltType != tilt_types::none) {
    json->addElem("tiltDirection", shade->tiltDirection);
    json->addElem("tiltTarget",    shade->transformPosition(shade->tiltTarget));
    json->addElem("tiltPosition",  shade->transformPosition(shade->currentTiltPos));
    json->addElem("myTiltPos",     shade->transformPosition(shade->getMyTiltPos()));
  }
  json->endObject();
  sockEmit.endEmit(num);
}

void SomfyMQTTPublisher::emitCommand(uint8_t num, somfy_commands cmd, const char *source,
                                      uint32_t sourceAddress, const char *evt) {
  JsonSockEvent *json = sockEmit.beginEmit(evt);
  json->beginObject();
  json->addElem("shadeId",       shade->getShadeId());
  json->addElem("remoteAddress", (uint32_t)shade->getRemoteAddress());
  json->addElem("cmd",           translateSomfyCommand(cmd).c_str());
  json->addElem("source",        source);
  json->addElem("rcode",         (uint32_t)shade->lastRollingCode);
  json->addElem("sourceAddress", (uint32_t)sourceAddress);
  json->endObject();
  sockEmit.endEmit(num);
  if(mqtt.connected()) {
    this->publish("cmdSource",  source);
    this->publish("cmdAddress", sourceAddress);
    this->publish("cmd",        translateSomfyCommand(cmd).c_str());
  }
}
