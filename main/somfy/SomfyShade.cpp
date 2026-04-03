// SomfyShade.cpp — SomfyShade method implementations: movement control (open/close/stop/
// my/tilt), position interpolation and transformation, internal-command processing,
// frame emission and relay, JSON and MQTT I/O, NVS load/save, HomeKit bridge callbacks.
#include "compat/preferences.h"
#include <esp_task_wdt.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "GitOTA.h"
#include "SomfyShade.h"
#include "SomfyTransceiver.h"
#include "SomfyController.h"
#include "ConfigFile.h"

static const char *TAG = "SomfyShade";

extern SomfyShadeController somfy;
extern ConfigSettings settings;
extern GitUpdater git;

void SomfyShade::clear() {
  this->setShadeId(255);
  this->setRemoteAddress(0);
  this->moveStart = 0;
  this->tiltStart = 0;
  this->flagManager = SomfyFlagManager{};
  this->startPos = 0.0f;
  this->startTiltPos = 0.0f;
  this->settingMyPos = false;
  this->settingPos = false;
  this->settingTiltPos = false;
  this->awaitMy = 0;
  this->flipPosition = false;
  this->flipCommands = false;
  this->lastRollingCode = 0;
  this->shadeType = shade_types::roller;
  this->tiltType = tilt_types::none;
  //this->txQueue.clear();
  this->currentPos = 0.0f;
  this->currentTiltPos = 0.0f;
  this->direction = 0;
  this->tiltDirection = 0;  
  this->target = 0.0f;
  this->tiltTarget = 0.0f;
  this->myPos = -1.0f;
  this->myTiltPos = -1.0f;
  this->bitLength = somfy.transceiver.config.type;
  this->proto = somfy.transceiver.config.proto;
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
    this->linkedRemotes[i].setRemoteAddress(0);
  this->paired = false;
  this->name[0] = 0x00;
  this->upTime = 10000;
  this->downTime = 10000;
  this->tiltTime = 7000;
  this->stepSize = 100;
  this->repeats = 1;
  this->sortOrder = 255;
}

bool SomfyShade::linkRemote(uint32_t address, uint16_t rollingCode) {
  return commandTransmitter.linkRemote(address, rollingCode);
}

void SomfyShade::commit()              { persistence.commit(); }
void SomfyShade::commitShadePosition() { persistence.commitShadePosition(); }
void SomfyShade::commitMyPosition()    { persistence.commitMyPosition(); }
void SomfyShade::commitTiltPosition()  { persistence.commitTiltPosition(); }

bool SomfyShade::unlinkRemote(uint32_t address) {
  return commandTransmitter.unlinkRemote(address);
}

bool SomfyShade::isAtTarget() { 
  float epsilon = .00001;
  if(this->tiltType == tilt_types::tiltonly) return fabs(this->currentTiltPos - this->tiltTarget) < epsilon;
  else if(this->tiltType == tilt_types::none) return fabs(this->currentPos - this->target) < epsilon;
  return fabs(this->currentPos - this->target) < epsilon && fabs(this->currentTiltPos - this->tiltTarget) < epsilon; 
}

bool SomfyShade::isInGroup() {
  if(this->getShadeId() == 255) return false;
  for(uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) {
    if(somfy.groups[i].getGroupId() != 255 && somfy.groups[i].hasShadeId(this->getShadeId())) return true;
  }
  return false;
}

void SomfyShade::setGPIOs() {
  this->gpioControl.setGPIOs(this->proto, this->currentPos, this->direction,
                              this->tiltDirection, this->shadeType, this->tiltType);
}

void SomfyShade::triggerGPIOs(somfy_frame_t &frame) {
  this->gpioControl.triggerGPIOs(frame, this->proto, this->shadeType, this->isToggle());
}

void SomfyShade::checkMovement() {
  const uint64_t curTime = millis();
  // We need to first evaluate the sensor flags as these could be triggering movement from previous sensor inputs. So
  // we must check this before setting the directional items or it will not get processed until the next loop.
  int32_t downTime = (int32_t)this->downTime;
  int32_t upTime = (int32_t)this->upTime;
  int32_t tiltTime = (int32_t)this->tiltTime;
  if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) downTime = upTime = tiltTime = 1;
  

  // We are checking movement for essentially 3 types of motors.
  // If this is an integrated tilt we need to first tilt in the direction we are moving then move.  We know 
  // what needs to be done by the tilt type.  Set a tilt first flag to indicate whether we should be tilting or
  // moving. If this is only a tilt action then the regular tilt action should operate fine.
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  this->p_direction(this->currentPos == this->target ? 0 : this->currentPos > this->target ? -1 : 1);
  bool tilt_first = this->tiltType == tilt_types::integrated && ((this->direction == -1 && this->currentTiltPos != 0.0f) || (this->direction == 1 && this->currentTiltPos != 100.0f));

  this->p_tiltDirection(this->currentTiltPos == this->tiltTarget ? 0 : this->currentTiltPos > this->tiltTarget ? -1 : 1);
  if(tilt_first) this->p_tiltDirection(this->direction);
  else if(this->direction != 0) this->p_tiltDirection(0);
  uint8_t currPos = floor(this->currentPos);
  uint8_t currTiltPos = floor(this->currentTiltPos);
  if(this->direction != 0) this->lastMovement = this->direction;
  {
    auto tick = this->flagManager.tickTimers(this->flags, curTime, this->shadeId);
    if(tick.setSunTarget)
      this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
    if(tick.setNoSunTarget) {
      if(this->tiltType == tilt_types::tiltonly) this->p_tiltTarget(0.0f);
      this->p_target(0.0f);
    }
    if(tick.setWindTarget) {
      if(this->tiltType == tilt_types::tiltonly) this->p_tiltTarget(0.0f);
      this->p_target(0.0f);
    }
  }

  if(!tilt_first && this->direction > 0) {
    if(downTime == 0) {
      this->p_currentPos(100.0);
      //this->p_direction(0);
    }
    else {
      // The shade is moving down so we need to calculate its position through the down position.
      // 10000ms from 0 to 100
      // The starting posion is a float value from 0-1 that indicates how much the shade is open. So
      // if we take the starting position * the total down time then this will tell us how many ms it
      // has moved in the down position.
      int32_t msFrom0 = (int32_t)floor((this->startPos/100) * downTime);
      
      // So if the start position is .1 it is 10% closed so we have a 1000ms (1sec) of time to account for
      // before we add any more time.
      msFrom0 += (curTime - this->moveStart);
      // Now we should have the total number of ms that the shade moved from the top.  But just so we
      // don't have any rounding errors make sure that it is not greater than the max down time.
      msFrom0 = min(downTime, msFrom0);
      if(msFrom0 >= downTime) {
        this->p_currentPos(100.0f);
        //this->p_direction(0);        
      }
      else {
        // So now we know how much time has elapsed from the 0 position to down.  The current position should be
        // a ratio of how much time has travelled over the total time to go 100%.
  
        // We should now have the number of ms it will take to reach the shade fully close.
        this->p_currentPos((min(max((float)0.0, (float)msFrom0 / (float)downTime), (float)1.0)) * 100);
        // If the current position is >= 1 then we are at the bottom of the shade.
        if(this->currentPos >= 100) { // LCOV_EXCL_LINE — ratio clamped ≤1.0; unreachable when msFrom0<downTime
          this->p_currentPos(100.0); // LCOV_EXCL_LINE
          //this->p_direction(0);
        } // LCOV_EXCL_LINE
      }
    }
    if(this->currentPos >= this->target) {
      this->p_currentPos(this->target);
      
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        if(!isAtTarget()) {
          ESP_LOGI(TAG, "We are not at our tilt target: %.2f", this->tiltTarget);
          if(this->target != 100.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          delay(100);
          // We now need to move the tilt to the position we requested.
          this->moveToTiltTarget(this->tiltTarget);
        }
        else
          if(this->target != 100.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(!tilt_first && this->direction < 0) {
    if(upTime == 0) {
      this->p_currentPos(0);
      //this->p_direction(0);
    }
    else {
      // The shade is moving up so we need to calculate its position through the up position. Shades
      // often move slower in the up position so since we are using a relative position the up time
      // can be calculated.
      // 10000ms from 100 to 0;
      int32_t msFrom100 = upTime - (int32_t)floor((this->startPos/100) * upTime);
      msFrom100 += (curTime - this->moveStart);
      msFrom100 = min(upTime, msFrom100);
      if(msFrom100 >= upTime) {
        this->p_currentPos(0.0f);
        //this->p_direction(0);
      }
      else {
        float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)upTime), (float)1.0)) * 100;
        // We should now have the number of ms it will take to reach the shade fully open.
        // If we are at the top of the shade then set the movement to 0.
        if(fpos <= 0.0) { // LCOV_EXCL_LINE — ratio clamped ≥0.0; unreachable when msFrom100<upTime
          this->p_currentPos(0.0f); // LCOV_EXCL_LINE
          //this->p_direction(0);
        } // LCOV_EXCL_LINE
        else
          this->p_currentPos(fpos);
      }
    }
    if(this->currentPos <= this->target) {
      this->p_currentPos(this->target);
      
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingPos) {
        if(!isAtTarget()) {
          ESP_LOGI(TAG, "We are not at our tilt target: %.2f", this->tiltTarget);
          if(this->target != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          delay(100);
          // We now need to move the tilt to the position we requested.
          this->moveToTiltTarget(this->tiltTarget);
        }
        else
          if(this->target != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
      }
      this->p_direction(0);
      this->tiltStart = curTime;
      this->startTiltPos = this->currentTiltPos;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->tiltDirection > 0) {
    if(tilt_first) this->moveStart = curTime;
    int32_t msFrom0 = (int32_t)floor((this->startTiltPos/100) * tiltTime);
    msFrom0 += (curTime - this->tiltStart);
    msFrom0 = min(tiltTime, msFrom0);
    if(msFrom0 >= tiltTime) {
      this->p_currentTiltPos(100.0f);
      //this->p_tiltDirection(0);        
      ESP_LOGD(TAG, "Setting tiltDirection to 0 (not enough time) %.4f %.4f", msFrom0, tiltTime);
    }
    else {
      float fpos = (min(max((float)0.0, (float)msFrom0 / (float)tiltTime), (float)1.0)) * 100;
      
      if(fpos > 100.0f) { // LCOV_EXCL_LINE — ratio clamped ≤1.0; unreachable when msFrom0<tiltTime
        this->p_currentTiltPos(100.0f); // LCOV_EXCL_LINE
        //this->p_tiltDirection(0);
        ESP_LOGD(TAG, "Setting tiltDirection to 0 (100%)"); // LCOV_EXCL_LINE
      } // LCOV_EXCL_LINE
      else this->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(this->currentTiltPos >= 100.0f) {
        this->p_currentTiltPos(100.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
        ESP_LOGD(TAG, "Setting tiltDirection to 0 (tilt_first)");
      }
    }
    else if(this->currentTiltPos >= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          ESP_LOGD(TAG, "Sending My -- tiltTarget: %.2f, tiltDirection: %d", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 100.0f || this->currentPos != 100.0f) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 100.
          if(this->tiltTarget != 100.0f) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  else if(this->tiltDirection < 0) {
    if(tilt_first) this->moveStart = curTime;
    if(tiltTime == 0) {
      this->p_tiltDirection(0);
      this->p_currentTiltPos(0.0f);
    }
    else {
      int32_t msFrom100 = tiltTime - (int32_t)floor((this->startTiltPos/100) * tiltTime);
      msFrom100 += (curTime - this->tiltStart);
      msFrom100 = min(tiltTime, msFrom100);
      if(msFrom100 >= tiltTime) {
        this->p_currentTiltPos(0.0f);
        //this->p_tiltDirection(0);
      }
      float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)tiltTime), (float)1.0)) * 100;
      // If we are at the top of the shade then set the movement to 0.
      if(fpos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        //this->p_tiltDirection(0);
      }
      else this->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(this->currentTiltPos <= 0.0f) {
        this->p_currentTiltPos(0.0f);
        this->moveStart = curTime;
        this->startPos = this->currentPos;
        //this->p_tiltDirection(0);
      }
    }
    else if(this->currentTiltPos <= this->tiltTarget) {
      this->p_currentTiltPos(this->tiltTarget);
      // If we need to stop the shade do this before we indicate that we are
      // not moving otherwise the my function will kick in.
      if(this->settingTiltPos) {
        if(this->tiltType == tilt_types::integrated) {
          // If this is an integrated tilt mechanism the we will simply let it finish.  If it is not then we will stop it.
          ESP_LOGD(TAG, "Sending My -- tiltTarget: %.2f, tiltDirection: %d", this->tiltTarget, this->tiltDirection);
          if(this->tiltTarget != 0.0 || this->currentPos != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
        else {
          // This is a tilt motor so let it complete if it is going to 0.
          if(this->tiltTarget != 0.0) SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        }
      }
      this->p_tiltDirection(0);
      this->settingTiltPos = false;
      ESP_LOGI(TAG, "Stopping at tilt position");
      if(this->isAtTarget()) this->commitShadePosition();
    }
  }
  if(this->settingMyPos && this->isAtTarget()) {
    delay(200);
    // Set this position before sending the command.  If you don't the processFrame function
    // will send the shade back to its original My position.
    if(this->tiltType != tilt_types::none) {
      if(this->myTiltPos == this->currentTiltPos && this->myPos == this->currentPos) this->myPos = this->myTiltPos = -1;
      else {
        this->p_myPos(this->currentPos);
        this->p_myTiltPos(this->currentTiltPos);
      }
    }
    else {
      this->p_myTiltPos(-1);
      if(this->myPos == this->currentPos) this->p_myPos(-1);
      else this->p_myPos(this->currentPos);
    }
    SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
    this->settingMyPos = false;
    this->commitMyPosition();
    this->emitState();
  }
  else if(currDir != this->direction || currPos != floor(this->currentPos) || currTiltDir != this->tiltDirection || currTiltPos != floor(this->currentTiltPos)) {
    // We need to emit on the socket that our state has changed.
    this->emitState();
  }
}
#ifdef USE_NVS
void SomfyShade::load() { persistence.load(); }
#endif

void SomfyShade::publishState() { mqttPublisher.publishState(); }

void SomfyShade::publishDisco()   { mqttPublisher.publishDisco(); }
void SomfyShade::unpublishDisco() { mqttPublisher.unpublishDisco(); }
void SomfyShade::publish()        { mqttPublisher.publish(); }
void SomfyShade::unpublish()      { mqttPublisher.unpublish(); }
void SomfyShade::unpublish(uint8_t id)                    { SomfyMQTTPublisher::unpublish(id); }
void SomfyShade::unpublish(uint8_t id, const char *topic) { SomfyMQTTPublisher::unpublish(id, topic); }

bool SomfyShade::publish(const char *topic, int8_t val,  bool retain) { return mqttPublisher.publish(topic, val, retain); }
bool SomfyShade::publish(const char *topic, const char *val, bool retain) { return mqttPublisher.publish(topic, val, retain); }
bool SomfyShade::publish(const char *topic, uint8_t val, bool retain) { return mqttPublisher.publish(topic, val, retain); }
bool SomfyShade::publish(const char *topic, uint32_t val, bool retain) { return mqttPublisher.publish(topic, val, retain); }
bool SomfyShade::publish(const char *topic, uint16_t val, bool retain) { return mqttPublisher.publish(topic, val, retain); }
bool SomfyShade::publish(const char *topic, bool val,    bool retain) { return mqttPublisher.publish(topic, val, retain); }


float SomfyShade::p_currentPos(float pos) {
  float old = this->currentPos;
  this->currentPos = pos;
  if(floor(old) != floor(pos)) this->publish("position", this->transformPosition(static_cast<uint8_t>(floor(this->currentPos))));
  return old;
}

float SomfyShade::p_currentTiltPos(float pos) {
  float old = this->currentTiltPos;
  this->currentTiltPos = pos;
  if(floor(old) != floor(pos)) this->publish("tiltPosition", this->transformPosition(static_cast<uint8_t>(floor(this->currentTiltPos))));
  return old;
}

uint16_t SomfyShade::p_lastRollingCode(uint16_t code) {
  uint16_t old = SomfyRemote::p_lastRollingCode(code);
  if(old != code) this->publish("lastRollingCode", code);
  return old;
}

bool SomfyShade::p_flag(somfy_flags_t flag, bool val) {
  bool old = !!(this->flags & static_cast<uint8_t>(flag));
  if(val)
    this->flags |= static_cast<uint8_t>(flag);
  else
    this->flags &= ~(static_cast<uint8_t>(flag));
  return old;
}

bool SomfyShade::p_sunFlag(bool val) {
  bool old = this->p_flag(somfy_flags_t::SunFlag, val);
  if(old != val) this->publish("sunFlag", static_cast<uint8_t>(val));
  return old;
}

bool SomfyShade::p_windy(bool val) {
  bool old = this->p_flag(somfy_flags_t::Windy, val);
  if(old != val) this->publish("windy", static_cast<uint8_t>(val));
  return old;
}

bool SomfyShade::p_sunny(bool val) {
  bool old = this->p_flag(somfy_flags_t::Sunny, val);
  if(old != val) this->publish("sunny", static_cast<uint8_t>(val));
  return old;
}

int8_t SomfyShade::p_direction(int8_t dir) {
  int8_t old = this->direction;
  if(old != dir) {
    this->direction = dir;
    this->publish("direction", this->direction, true);
  }
  return old;
}

int8_t SomfyShade::p_tiltDirection(int8_t dir) {
  int8_t old = this->tiltDirection;
  if(old != dir) {
    this->tiltDirection = dir;
    this->publish("tiltDirection", this->tiltDirection, true);
  }
  return old;
}

float SomfyShade::p_target(float target) {
  float old = this->target;
  if(old != target) {
    this->target = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("target", this->transformPosition(this->target), true);
  }
  return old;
}

float SomfyShade::p_tiltTarget(float target) {
  float old = this->tiltTarget;
  if(old != target) {
    this->tiltTarget = target;
    if(this->transformPosition(old) != this->transformPosition(target))
      this->publish("tiltTarget", this->transformPosition(this->tiltTarget), true);
  }
  return old;
}

float SomfyShade::p_myPos(float pos) {
  float old = this->myPos;
  if(old != pos) {
    //if(this->transformPosition(pos) == 0) ESP_LOGD(TAG, "MyPos = %.2f", pos);
    this->myPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("mypos", this->transformPosition(this->myPos), true);
  }
  return old;
}

float SomfyShade::p_myTiltPos(float pos) {
  float old = this->myTiltPos;
  if(old != pos) {
    this->myTiltPos = pos;
    if(this->transformPosition(old) != this->transformPosition(pos))
      this->publish("myTiltPos", this->transformPosition(this->myTiltPos), true);
  }
  return old;
}


void SomfyShade::emitState(const char *evt) { this->emitState(255, evt); }
void SomfyShade::emitState(uint8_t num, const char *evt) { mqttPublisher.emitState(num, evt); }

void SomfyShade::emitCommand(somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) { this->emitCommand(255, cmd, source, sourceAddress, evt); }
void SomfyShade::emitCommand(uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt) { mqttPublisher.emitCommand(num, cmd, source, sourceAddress, evt); }

int8_t SomfyShade::transformPosition(float fpos) { 
  if(fpos < 0) return -1;
  return static_cast<int8_t>(this->flipPosition && fpos >= 0.00f ? floor(100.0f - fpos) : floor(fpos)); 
}

bool SomfyShade::isIdle() { 
  return this->isAtTarget() && this->direction == 0 && this->tiltDirection == 0; 
}

void SomfyShade::processWaitingFrame() {
  if(this->shadeId == 255) {
    this->lastFrame.await = 0; 
    return;
  }
  if(this->lastFrame.processed) return;
  if(this->lastFrame.await > 0 && (millis() > this->lastFrame.await)) {
    somfy_commands cmd = this->transformCommand(this->lastFrame.cmd);
    switch(cmd) {
      case somfy_commands::StepUp:
          this->lastFrame.processed = true;
          // Simply move the shade up by 1%.
          if(this->currentPos > 0) {
            this->p_target(floor(this->currentPos) - 1);
            this->setMovement(-1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::StepDown:
          this->lastFrame.processed = true;
          // Simply move the shade down by 1%.
          if(this->currentPos < 100) {
            this->p_target(floor(this->currentPos) + 1);
            this->setMovement(1);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          break;
      case somfy_commands::Down:
      case somfy_commands::Up:
        if(this->tiltType == tilt_types::tiltmotor) { // Theoretically this should get here unless it does have a tilt motor.
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100.0f : 0.0f);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            ESP_LOGI(TAG, "%s Processing tilt %s after %d repeats", this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100 : 0);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        else if(this->tiltType == tilt_types::euromode) {
          if(this->lastFrame.repeats >= TILT_REPEATS) {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_target(dir > 0 ? 100.0f : 0.0f);
            this->setMovement(dir);
            this->lastFrame.processed = true;
            ESP_LOGI(TAG, "%s Processing %s after %d repeats", this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          else {
            int8_t dir = this->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
            this->p_tiltTarget(dir > 0 ? 100 : 0);
            this->setTiltMovement(dir);
            this->lastFrame.processed = true;
            ESP_LOGI(TAG, "%s Processing tilt %s after %d repeats", this->name, translateSomfyCommand(this->lastFrame.cmd).c_str(), this->lastFrame.repeats);
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
          if(this->lastFrame.repeats > TILT_REPEATS + 2) {
            this->lastFrame.processed = true;
            this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
          }
        }
        break;
      case somfy_commands::My:
        if(this->lastFrame.repeats >= SETMY_REPEATS && this->isIdle()) {
          if(floor(this->myPos) == floor(this->currentPos)) {
            // We are clearing it.
            this->p_myPos(-1);
            this->p_myTiltPos(-1);
          }
          else {
            this->p_myPos(this->currentPos);
            this->p_myTiltPos(this->currentTiltPos);
          }
          this->commitMyPosition();
          this->lastFrame.processed = true;
          this->emitState();
        }
        else if(this->isIdle()) {
          if(this->simMy())
            this->moveToMyPosition(); // Call out like this (instead of move to target) so that we don't get some of the goofy tilt only problems.
          else {
            if(this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
            if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          }
          this->setMovement(0);
          this->lastFrame.processed = true;
          this->emitCommand(cmd, "remote", this->lastFrame.remoteAddress);
        }
        else {
          this->p_target(this->currentPos);
          this->p_tiltTarget(this->currentTiltPos);
        }
        if(this->lastFrame.repeats > SETMY_REPEATS + 2) this->lastFrame.processed = true;
        if(this->lastFrame.processed) {
          ESP_LOGI(TAG, "%s Processing MY after %d repeats", this->name, this->lastFrame.repeats);
        }
        break;
      default:
        break;
    }
  }
}

void SomfyShade::processSensorCommand(somfy_frame_t &frame, uint64_t curTime) {
  const bool wasSunny = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny));
  const bool wasWindy = !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy));
  const uint16_t status = frame.rollingCode << 4;
  this->p_sunny(status & static_cast<uint8_t>(somfy_flags_t::Sunny));
  this->p_windy(status & static_cast<uint8_t>(somfy_flags_t::Windy));
  this->p_flag(somfy_flags_t::DemoMode,
      !!(frame.rollingCode & static_cast<uint8_t>(somfy_flags_t::DemoMode)));
  this->flagManager.updateTimers(wasSunny, wasWindy,
      !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny)),
      !!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy)),
      curTime, this->shadeId);
  this->emitState();
  somfy.updateGroupFlags();
}

void SomfyShade::processFlagCommand(bool internal, somfy_frame_t &frame) {
  this->p_sunFlag(false);
  somfy.isDirty = true;
  this->emitState();
  this->emitCommand(frame.cmd, internal ? "internal" : "remote", frame.remoteAddress);
  somfy.updateGroupFlags();
}

void SomfyShade::processSunFlagCommand(bool internal, somfy_frame_t &frame) {
  this->p_sunFlag(true);
  if(!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy))) {
    const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
    if(isSunny && this->flagManager.sunDone) {
      if(this->tiltType == tilt_types::tiltonly)
        this->p_tiltTarget(this->myTiltPos >= 0 ? this->myTiltPos : 100.0f);
      else
        this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
    }
    else if(!isSunny && this->flagManager.noSunDone) {
      if(this->tiltType == tilt_types::tiltonly)
        this->p_tiltTarget(0.0f);
      else
        this->p_target(0.0f);
    }
  }
  somfy.isDirty = true;
  this->emitState();
  this->emitCommand(frame.cmd, internal ? "internal" : "remote", frame.remoteAddress);
  somfy.updateGroupFlags();
}

void SomfyShade::processMyCommand(bool internal, somfy_frame_t &frame, uint64_t curTime) {
  const somfy_commands cmd = frame.cmd;
  if(this->isToggle()) {
    if(this->lastFrame.processed) return;
    this->lastFrame.processed = true;
    if(!this->isIdle())               this->p_target(this->currentPos);
    else if(this->currentPos == 100.0f) this->p_target(0.0f);
    else if(this->currentPos == 0.0f)   this->p_target(100.0f);
    else                               this->p_target(this->lastMovement == -1 ? 100 : 0);
    this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
    return;
  }
  if(this->shadeType == shade_types::drycontact) {
    if(this->lastFrame.processed) return;
    this->lastFrame.processed = true;
    if(this->currentPos == 100.0f)    this->p_target(0);
    else if(this->currentPos == 0.0f) this->p_target(100);
    else                              this->p_target(this->lastMovement == -1 ? 100 : 0);
    this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
    return;
  }
  if(this->isIdle()) {
    if(!internal) {
      this->lastFrame.await = curTime + 500;
    }
    else {
      if(this->lastFrame.processed) return;
      ESP_LOGI(TAG, "Moving to My target");
      this->lastFrame.processed = true;
      if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
      if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
    }
  }
  else {
    if(this->lastFrame.processed) return;
    this->lastFrame.processed = true;
    if(!internal) {
      if(this->tiltType != tilt_types::tiltonly) this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);
    }
    this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
  }
}

// moveDir: -1 = Up (target 0), +1 = Down (target 100)
void SomfyShade::processUpDownCommand(somfy_commands cmd, int8_t moveDir, bool internal, somfy_frame_t &frame, uint64_t curTime) {
  const float endpoint = moveDir < 0 ? 0.0f : 100.0f;
  if(this->shadeType == shade_types::drycontact) {
    this->lastFrame.processed = true;
    return;
  }
  if(this->shadeType == shade_types::drycontact2) {
    if(this->lastFrame.processed) return;
    this->lastFrame.processed = true;
    if(this->currentPos != endpoint) this->p_target(endpoint);
    this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
    return;
  }
  // Down is suppressed for a period after a wind event.
  if(moveDir > 0 && this->flagManager.windLast && (curTime - this->flagManager.windLast) < SOMFY_NO_WIND_REMOTE_TIMEOUT) return;

  if(this->tiltType == tilt_types::tiltmotor || this->tiltType == tilt_types::euromode) {
    if(!internal) this->lastFrame.await = curTime + 500;
    else          this->lastFrame.processed = true;
    // Up: emitCommand is deferred to processWaitingFrame when await is set.
    // Down: emitCommand fires immediately so the motor starts moving.
    if(moveDir < 0 && !internal) return;
  }
  else {
    if(!internal) {
      if(moveDir < 0) {
        // Up: tiltonly only moves tilt; everything else moves both
        if(this->tiltType == tilt_types::tiltonly) this->p_tiltTarget(endpoint);
        else { this->p_target(endpoint); this->p_tiltTarget(endpoint); }
      }
      else {
        // Down: move lift unless tiltonly; move tilt unless none
        if(this->tiltType != tilt_types::tiltonly) this->p_target(endpoint);
        if(this->tiltType != tilt_types::none)     this->p_tiltTarget(endpoint);
      }
    }
    this->lastFrame.processed = true;
  }
  this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
}

// stepDir: -1 = StepUp (decrease position), +1 = StepDown (increase position)
void SomfyShade::processStepCommand(somfy_commands cmd, int8_t stepDir, bool internal, somfy_frame_t &frame) {
  if(this->lastFrame.processed) return;
  this->lastFrame.processed = true;
  if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
  if(this->stepSize == 0) return;
  if(this->lastFrame.stepSize == 0) this->lastFrame.stepSize = 1;

  const float steps = static_cast<float>(this->stepSize * this->lastFrame.stepSize);

  if(this->tiltType == tilt_types::integrated) {
    const bool goingUp    = stepDir < 0;
    const bool tiltAtEnd  = goingUp ? (this->currentTiltPos <= 0.0f) : (this->currentTiltPos >= 100.0f);
    const bool liftAtEnd  = goingUp ? (this->currentPos    <= 0.0f) : (this->currentPos    >= 100.0f);
    if(tiltAtEnd && liftAtEnd) return;
    if(!tiltAtEnd) {
      this->p_target(this->currentPos);
      if(this->tiltTime == 0) return;
      float newTilt = this->currentTiltPos + stepDir * (100.0f / (this->tiltTime / steps));
      this->p_tiltTarget(goingUp ? max(0.0f, newTilt) : min(100.0f, newTilt));
    }
    else {
      this->p_tiltTarget(this->currentTiltPos);
      const float time = goingUp ? static_cast<float>(this->upTime) : static_cast<float>(this->downTime);
      if(time == 0) return;
      float newPos = this->currentPos + stepDir * (100.0f / (time / steps));
      this->p_target(goingUp ? max(0.0f, newPos) : min(100.0f, newPos));
    }
  }
  else if(this->tiltType == tilt_types::tiltonly) {
    if(this->tiltTime == 0) return;
    float newTilt = this->currentTiltPos + stepDir * (100.0f / (this->tiltTime / steps));
    this->p_tiltTarget(stepDir < 0 ? max(0.0f, newTilt) : min(100.0f, newTilt));
  }
  else {
    const bool canMove = stepDir < 0 ? (this->currentPos > 0.0f) : (this->currentPos < 100.0f);
    if(!canMove) return;
    const float time = stepDir < 0 ? static_cast<float>(this->upTime) : static_cast<float>(this->downTime);
    if(time == 0) return;
    float newPos = this->currentPos + stepDir * (100.0f / (time / steps));
    this->p_target(stepDir < 0 ? max(0.0f, newPos) : min(100.0f, newPos));
  }
  this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
}

void SomfyShade::processFrame(somfy_frame_t &frame, bool internal) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  bool hasRemote = this->getRemoteAddress() == frame.remoteAddress;
  if(!hasRemote) {
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
      if(this->linkedRemotes[i].getRemoteAddress() == frame.remoteAddress) {
        if(frame.cmd != somfy_commands::Sensor) this->linkedRemotes[i].setRollingCode(frame.rollingCode);
        hasRemote = true;
        break;      
      }
    }
  }
  if(!hasRemote) return;
  const uint64_t curTime = millis();
  this->lastFrame.copy(frame);
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  if(!internal) this->settingMyPos = this->settingPos = this->settingTiltPos = false;
  somfy_commands cmd = this->transformCommand(frame.cmd);
  switch(cmd) {
    case somfy_commands::Sensor:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      this->processSensorCommand(frame, curTime);
      break;
    case somfy_commands::Prog:
    case somfy_commands::MyUp:
    case somfy_commands::MyDown:
    case somfy_commands::MyUpDown:
    case somfy_commands::UpDown:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
      
    case somfy_commands::Flag:
      this->lastFrame.processed = true;
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      this->processFlagCommand(internal, frame);
      break;
    case somfy_commands::SunFlag:
      if(this->shadeType == shade_types::drycontact || this->shadeType == shade_types::drycontact2) return;
      if(this->lastFrame.rollingCode & 0x8000) return; // Some sensors send bogus frames with a rollingCode >= 32768 that cause them to change the state.
      this->processSunFlagCommand(internal, frame);
      break;
    case somfy_commands::Up:
      this->processUpDownCommand(cmd, -1, internal, frame, curTime);
      break;
    case somfy_commands::Down:
      this->processUpDownCommand(cmd, +1, internal, frame, curTime);
      break;
    case somfy_commands::My:
      if(this->shadeType == shade_types::drycontact2) return;
      this->processMyCommand(internal, frame, curTime);
      break;
    case somfy_commands::StepUp:
      this->processStepCommand(cmd, -1, internal, frame);
      break;
    case somfy_commands::StepDown:
      this->processStepCommand(cmd, +1, internal, frame);
      break;
    case somfy_commands::Toggle:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(!this->isIdle()) this->p_target(this->currentPos);
      else if(this->currentPos == 100.0f) this->p_target(0);
      else if(this->currentPos == 0.0f) this->p_target(100);
      else this->p_target(this->lastMovement == -1 ? 100 : 0);
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Stop:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      this->p_target(this->currentPos);
      this->p_tiltTarget(this->currentTiltPos);      
      this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      break;
    case somfy_commands::Favorite:
      if(this->lastFrame.processed) return;
      this->lastFrame.processed = true;
      if(this->simMy()) {
        this->moveToMyPosition();
      }
      else {
        if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
        if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        this->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
      }
      break;
    default:                      // LCOV_EXCL_LINE — scoped enum; all values covered above
      dir = 0;                    // LCOV_EXCL_LINE
      break;                      // LCOV_EXCL_LINE
  }
  this->setMovement(dir);
}

void SomfyShade::processInternalCommand(somfy_commands cmd, uint8_t repeat) {
  // The reason why we are processing all frames here is so
  // any linked remotes that may happen to be on the same ESPSomfy RTS
  // device can trigger the appropriate actions.
  if(this->shadeId == 255) return; 
  const uint64_t curTime = millis();
  int8_t dir = 0;
  this->moveStart = this->tiltStart = curTime;
  this->startPos = this->currentPos;
  this->startTiltPos = this->currentTiltPos;
  // If the command is coming from a remote then we are aborting all these positioning operations.
  switch(cmd) {
    case somfy_commands::Up:
      if(this->tiltType == tilt_types::tiltmotor) {
        if(repeat >= TILT_REPEATS)
          this->p_tiltTarget(0.0f);
        else
          this->p_target(0.0f);
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        this->p_target(100.0f);
        this->p_currentPos(100.0f);
        this->p_tiltTarget(0.0f);
      }
      else {
        this->p_target(0.0f);
        this->p_tiltTarget(0.0f);
      }
      break;
    case somfy_commands::Down:
      if (!this->flagManager.windLast || (curTime - this->flagManager.windLast) >= SOMFY_NO_WIND_REMOTE_TIMEOUT) {
        if(this->tiltType == tilt_types::tiltmotor) {
          if(repeat >= TILT_REPEATS)
            this->p_tiltTarget(100.0f);
          else
            this->p_target(100.0f);
        }
        else if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
          this->p_currentPos(100.0f);
          this->p_tiltTarget(100.0f);
        }
        else {
            this->p_target(100.0f);
            if(this->tiltType != tilt_types::none) this->p_tiltTarget(100.0f);
        }
      }
      break;
    case somfy_commands::My:
      if(this->isIdle()) {
        ESP_LOGI(TAG, "Shade #%d is idle", this->getShadeId());
        if(this->simMy()) {
          this->moveToMyPosition();
        }
        else {
          if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
          if(this->myPos >= 0.0f && this->myPos <= 100.0f && this->tiltType != tilt_types::tiltonly) this->p_target(this->myPos);
        }
      }
      else {
        if(this->tiltType == tilt_types::tiltonly) {
          this->p_target(100.0f);
        }
        else this->p_target(this->currentPos);
        this->p_tiltTarget(this->currentTiltPos);
      }
      break;
    case somfy_commands::StepUp:
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 0% do nothing
        // 2. If the tilt position is not currently at the top then shift the tilt.
        // 3. If the tilt position is not currently at the top then shift the lift.
        if(this->currentTiltPos <= 0.0f && this->currentPos <= 0.0f) return; // Do nothing
        else if(this->currentTiltPos > 0.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
        }
        else {
          // We only have the lift to move.
          if(this->upTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(this->currentTiltPos);
          this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->currentTiltPos <= 0.0f) return;
        this->p_tiltTarget(max(0.0f, this->currentTiltPos - (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
      }
      else if(this->currentPos > 0.0f) {
        if(this->upTime == 0) return;
        this->p_target(max(0.0f, this->currentPos - (100.0f/(static_cast<float>(this->upTime/static_cast<float>(this->stepSize))))));
      }
      break;
    case somfy_commands::StepDown:
      dir = 1;
      // With the step commands and integrated shades
      // the motor must tilt in the direction first then move
      // so we have to calculate the target with this in mind.
      if(this->stepSize == 0) return; // Avoid divide by 0.
      if(this->tiltType == tilt_types::integrated) {
        // With integrated tilt this is more involved than ne would think because the step command can be moving not just the tilt
        // but the lift.  So a determination needs to be made as to whether we are currently moving and it should stop.
        // Conditions:
        // 1. If both the tilt and lift are at 100% do nothing
        // 2. If the tilt position is not currently at the bottom then shift the tilt.
        // 3. If the tilt position is add the bottom then shift the lift.
        if(this->currentTiltPos >= 100.0f && this->currentPos >= 100.0f) return; // Do nothing
        else if(this->currentTiltPos < 100.0f) {
          // Set the tilt position.  This should stop the lift movement.
          this->p_target(this->currentPos);
          if(this->tiltTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
        }
        else {
          // We only have the lift to move.
          if(this->downTime == 0) return; // Avoid divide by 0.
          this->p_tiltTarget(this->currentTiltPos);
          this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize))))));
        }
      }
      else if(this->tiltType == tilt_types::tiltonly) {
        if(this->tiltTime == 0 || this->stepSize == 0 || this->currentTiltPos >= 100.0f) return;
        this->p_tiltTarget(min(100.0f, this->currentTiltPos + (100.0f/(static_cast<float>(this->tiltTime/static_cast<float>(this->stepSize))))));
      }
      else if(this->currentPos < 100.0f) {
        if(this->downTime == 0 || this->stepSize == 0) return;
        this->p_target(min(100.0f, this->currentPos + (100.0f/(static_cast<float>(this->downTime/static_cast<float>(this->stepSize))))));
      }
      break;
    case somfy_commands::Flag:
      this->p_sunFlag(false);
      if(this->hasSunSensor()) {
        somfy.isDirty = true;
        this->emitState();
      }
      else {
        ESP_LOGI(TAG, "Shade does not have sensor %d", this->flags);
      }
      break;    
    case somfy_commands::SunFlag:
      if(this->hasSunSensor()) {
        this->p_sunFlag(true);
        if (!(this->flags & static_cast<uint8_t>(somfy_flags_t::Windy)))
        {
          const bool isSunny = this->flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
          if (isSunny && this->flagManager.sunDone)
            this->p_target(this->myPos >= 0 ? this->myPos : 100.0f);
          else if (!isSunny && this->flagManager.noSunDone)
            this->p_target(0.0f);
        }
        somfy.isDirty = true;
        this->emitState();
      }
      else
        ESP_LOGI(TAG, "Shade does not have sensor %d", this->flags);
      break;
    default:
      dir = 0;
      break;
  }
  this->setMovement(dir);
}

void SomfyShade::setTiltMovement(int8_t dir) {
  int8_t currDir = this->tiltDirection;
  if(dir == 0) {
    // The shade tilt is stopped.
    this->startTiltPos = this->currentTiltPos;
    this->tiltStart = 0;
    this->p_tiltDirection(dir);
    if(currDir != dir) {
      this->commitTiltPosition();
    }
  }
  else if(this->tiltDirection != dir) {
    this->tiltStart = millis();
    this->startTiltPos = this->currentTiltPos;
    this->p_tiltDirection(dir);
  }
  if(this->tiltDirection != currDir) {
    this->emitState();
  }
}

void SomfyShade::setMovement(int8_t dir) {
  int8_t currDir = this->direction;
  int8_t currTiltDir = this->tiltDirection;
  if(dir == 0) {
    if(currDir != dir || currTiltDir != dir) this->commitShadePosition();
  }
  else {
    this->tiltStart = this->moveStart = millis();
    this->startPos = this->currentPos;
    this->startTiltPos = this->currentTiltPos;
  }
  if(this->direction != currDir || currTiltDir != this->tiltDirection) {
    this->emitState();
  }
}

void SomfyShade::setMyPosition(int8_t pos, int8_t tilt) {
  if(!this->isIdle()) return; // Don't do this if it is moving.
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_myPos(-1.0f);    
    if(tilt != floor(this->currentTiltPos)) {
      this->settingMyPos = true;
      if(tilt == floor(this->myTiltPos))
        this->moveToMyPosition();
      else 
        this->moveToTarget(100, tilt);
    }
    else if(tilt == floor(this->myTiltPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->currentTiltPos != this->myTiltPos) {
        this->settingMyPos = true;
        this->moveToMyPosition();      
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else {
      SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      this->p_myTiltPos(this->currentTiltPos);
    }
    this->commitMyPosition();
    this->emitState();
  }
  else if(this->tiltType != tilt_types::none) {
      if(tilt < 0) tilt = 0;
      if(pos != floor(this->currentPos) || tilt != floor(this->currentTiltPos)) {
        this->settingMyPos = true;
        if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos))
          this->moveToMyPosition();
        else
          this->moveToTarget(pos, tilt);
      }
      else if(pos == floor(this->myPos) && tilt == floor(this->myTiltPos)) {
        // Of so we need to clear the my position. These motors are finicky so send
        // a my command to ensure we are actually at the my position then send the clear
        // command.  There really is no other way to do this.
        if(this->currentPos != this->myPos || this->currentTiltPos != this->myTiltPos) {
          this->settingMyPos = true;
          this->moveToMyPosition();      
        }
        else {
          SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
          this->settingPos = false;
          this->settingMyPos = true;
        }
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
        this->p_myPos(this->currentPos);
        this->p_myTiltPos(this->currentTiltPos);
      }
      this->commitMyPosition();
      this->emitState();
  }
  else {
    if(pos != floor(this->currentPos)) {
      this->settingMyPos = true;
      if(pos == floor(this->myPos))
        this->moveToMyPosition();
      else
        this->moveToTarget(pos);
    }
    else if(pos == floor(this->myPos)) {
      // Of so we need to clear the my position. These motors are finicky so send
      // a my command to ensure we are actually at the my position then send the clear
      // command.  There really is no other way to do this.
      if(this->myPos != this->currentPos) {
        this->settingMyPos = true;
        this->moveToMyPosition();      
      }
      else {
        SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
        this->settingPos = false;
        this->settingMyPos = true;
      }
    }
    else {
      SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      this->p_myPos(currentPos);
      this->p_myTiltPos(-1);
      this->commitMyPosition();
      this->emitState();
    }
  }
}

void SomfyShade::moveToMyPosition() {
  if(!this->isIdle()) return;
  ESP_LOGI(TAG, "Moving to My Position");
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_currentPos(100.0f);
    this->p_myPos(-1.0f);
  }
  if(this->currentPos == this->myPos) {
    if(this->tiltType != tilt_types::none) {
      if(this->currentTiltPos == this->myTiltPos) return; // Nothing to see here since we are already here.
    }
    else
      return;
  }
  if(this->myPos == -1 && (this->tiltType == tilt_types::none || this->myTiltPos == -1)) return;
  if(this->tiltType != tilt_types::tiltonly && this->myPos >= 0.0f && this->myPos <= 100.0f) this->p_target(this->myPos);
  if(this->myTiltPos >= 0.0f && this->myTiltPos <= 100.0f) this->p_tiltTarget(this->myTiltPos);
  this->settingPos = false;
  if(this->simMy()) {
    ESP_LOGI(TAG, "Moving to simulated favorite position");
    this->moveToTarget(this->myPos, this->myTiltPos);
  }
  else
    SomfyRemote::sendCommand(somfy_commands::My, this->repeats);
}

void SomfyShade::sendCommand(somfy_commands cmd) { commandTransmitter.sendCommand(cmd); }
void SomfyShade::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) { commandTransmitter.sendCommand(cmd, repeat, stepSize); }
void SomfyShade::sendTiltCommand(somfy_commands cmd) { commandTransmitter.sendTiltCommand(cmd); }

void SomfyShade::moveToTiltTarget(float target) {
  somfy_commands cmd = somfy_commands::My;
  if(target < this->currentTiltPos)
    cmd = somfy_commands::Up;
  else if(target > this->currentTiltPos)
    cmd = somfy_commands::Down;
  if(target >= 0.0f && target <= 100.0f) {
    // Only send a command if the lift is not moving.
    if(this->currentPos == this->target || this->tiltType == tilt_types::tiltmotor) {
      if(cmd != somfy_commands::My) {
        ESP_LOGI(TAG, "Moving Tilt to %f%% from %f%% using %s", target, this->currentTiltPos, translateSomfyCommand(cmd));
        SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : this->repeats);
      }
      // If the blind is currently moving then the command to stop it
      // will occur on its own when the tilt target is set.
    }
    this->p_tiltTarget(target);
  }
  if(cmd != somfy_commands::My) this->settingTiltPos = true;
}

void SomfyShade::moveToTarget(float pos, float tilt) {
  somfy_commands cmd = somfy_commands::My;
  if(this->isToggle()) {
    // Overload this as we cannot seek a position on a garage door or single button device.
    this->p_target(pos);
    this->p_currentPos(pos);
    this->emitState();
    return;
  }
  if(this->tiltType == tilt_types::tiltonly) {
    this->p_target(100.0f);
    this->p_myPos(-1.0f);
    this->p_currentPos(100.0f);
    pos = 100;
    if(tilt < this->currentTiltPos) cmd = somfy_commands::Up;
    else if(tilt > this->currentTiltPos) cmd = somfy_commands::Down;
  }
  else {
    if(pos < this->currentPos)
      cmd = somfy_commands::Up;
    else if(pos > this->currentPos)
      cmd = somfy_commands::Down;
    else if(tilt >= 0 && tilt < this->currentTiltPos)
      cmd = somfy_commands::Up;
    else if(tilt >= 0 && tilt > this->currentTiltPos)
      cmd = somfy_commands::Down;
  }
  if(cmd != somfy_commands::My) {
    ESP_LOGI(TAG, "Moving to %f%% from %f%%", pos, this->currentPos);
    if(tilt >= 0) {
      ESP_LOGI(TAG, " tilt %f%% from %f%%", tilt, this->currentTiltPos);
    }
    ESP_LOGI(TAG, " using %s", translateSomfyCommand(cmd).c_str());
    SomfyRemote::sendCommand(cmd, this->tiltType == tilt_types::euromode ? TILT_REPEATS : this->repeats);
    this->settingPos = true;
    this->p_target(pos);
    if(tilt >= 0) {
      this->p_tiltTarget(tilt);
      this->settingTiltPos = true;
    }
  }
}

bool SomfyShade::save() { return persistence.save(); }

bool SomfyShade::isToggle() {
  switch(this->shadeType) {
    case shade_types::garage1:
    case shade_types::lgate1:
    case shade_types::cgate1:
    case shade_types::rgate1:
      return true;
    default:
      break;
  }
  return false;
}

bool SomfyShade::usesPin(uint8_t pin) {
  return this->gpioControl.usesPin(pin, this->proto, this->shadeType, this->isToggle());
}

int8_t SomfyShade::validateJSON(JsonObject &obj) { return jsonSerializer.validateJSON(obj); }
int8_t SomfyShade::fromJSON(JsonObject &obj)     { return jsonSerializer.fromJSON(obj); }
void SomfyShade::toJSONRef(JsonResponse &json)   { jsonSerializer.toJSONRef(json); }
void SomfyShade::toJSON(JsonResponse &json)      { jsonSerializer.toJSON(json); }
