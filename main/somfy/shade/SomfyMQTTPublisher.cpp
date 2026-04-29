// SomfyMQTTPublisher.cpp — Socket-emit and HomeKit state publishing for SomfyShade.
#include "SomfyShade.h"
#include "SomfyShadeController.h"
#include "Sockets.h"
#include "HomeKit.h"
#include "SomfyMQTTPublisher.h"

extern SocketEmitter sockEmit;

void SomfyMQTTPublisher::publishState() {
  homekit.notifyShadeState(shade);
}

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
}
