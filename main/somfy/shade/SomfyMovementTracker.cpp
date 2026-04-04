// SomfyMovementTracker.cpp — position interpolation, direction management and
// movement-stop logic extracted from SomfyShade.  All logic is unchanged; only
// the receiver changes from `this` (SomfyShade) to `shade` (SomfyShade*).
#include "esp_log.h"
#include "SomfyMovementTracker.h"
#include "SomfyShade.h"
#include "SomfyTransceiver.h"

static const char *TAG = "SomfyMovementTracker";

void SomfyMovementTracker::checkMovement() {
  const uint64_t curTime = millis();
  int32_t downTime = (int32_t)shade->downTime;
  int32_t upTime   = (int32_t)shade->upTime;
  int32_t tiltTime = (int32_t)shade->tiltTime;
  if(shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2)
    downTime = upTime = tiltTime = 1;

  int8_t currDir     = shade->direction;
  int8_t currTiltDir = shade->tiltDirection;
  shade->p_direction(shade->currentPos == shade->target ? 0 : shade->currentPos > shade->target ? -1 : 1);
  bool tilt_first = shade->tiltType == tilt_types::integrated &&
                    ((shade->direction == -1 && shade->currentTiltPos != 0.0f) ||
                     (shade->direction ==  1 && shade->currentTiltPos != 100.0f));

  shade->p_tiltDirection(shade->currentTiltPos == shade->tiltTarget ? 0 : shade->currentTiltPos > shade->tiltTarget ? -1 : 1);
  if(tilt_first) shade->p_tiltDirection(shade->direction);
  else if(shade->direction != 0) shade->p_tiltDirection(0);

  uint8_t currPos     = floor(shade->currentPos);
  uint8_t currTiltPos = floor(shade->currentTiltPos);
  if(shade->direction != 0) shade->lastMovement = shade->direction;
  {
    auto tick = shade->flagManager.tickTimers(shade->flags, curTime, shade->shadeId);
    if(tick.setSunTarget)
      shade->p_target(shade->targetSequencer.myPos >= 0 ? shade->targetSequencer.myPos : 100.0f);
    if(tick.setNoSunTarget) {
      if(shade->tiltType == tilt_types::tiltonly) shade->p_tiltTarget(0.0f);
      shade->p_target(0.0f);
    }
    if(tick.setWindTarget) {
      if(shade->tiltType == tilt_types::tiltonly) shade->p_tiltTarget(0.0f);
      shade->p_target(0.0f);
    }
  }

  if(!tilt_first && shade->direction > 0) {
    if(downTime == 0) {
      shade->p_currentPos(100.0);
    }
    else {
      int32_t msFrom0 = (int32_t)floor((startPos/100) * downTime);
      msFrom0 += (curTime - moveStart);
      msFrom0 = min(downTime, msFrom0);
      if(msFrom0 >= downTime) {
        shade->p_currentPos(100.0f);
      }
      else {
        shade->p_currentPos((min(max((float)0.0, (float)msFrom0 / (float)downTime), (float)1.0)) * 100);
      }
    }
    if(shade->currentPos >= shade->target) {
      shade->p_currentPos(shade->target);
      if(motionState.settingPos) {
        if(!shade->isAtTarget()) {
          ESP_LOGI(TAG, "We are not at our tilt target: %.2f", shade->tiltTarget);
          if(shade->target != 100.0) shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
          delay(100);
          shade->moveToTiltTarget(shade->tiltTarget);
        }
        else
          if(shade->target != 100.0) shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
      }
      shade->p_direction(0);
      tiltStart    = curTime;
      startTiltPos = shade->currentTiltPos;
      if(shade->isAtTarget()) shade->commitShadePosition();
    }
  }
  else if(!tilt_first && shade->direction < 0) {
    if(upTime == 0) {
      shade->p_currentPos(0);
    }
    else {
      int32_t msFrom100 = upTime - (int32_t)floor((startPos/100) * upTime);
      msFrom100 += (curTime - moveStart);
      msFrom100 = min(upTime, msFrom100);
      if(msFrom100 >= upTime) {
        shade->p_currentPos(0.0f);
      }
      else {
        float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)upTime), (float)1.0)) * 100;
        shade->p_currentPos(fpos);
      }
    }
    if(shade->currentPos <= shade->target) {
      shade->p_currentPos(shade->target);
      if(motionState.settingPos) {
        if(!shade->isAtTarget()) {
          ESP_LOGI(TAG, "We are not at our tilt target: %.2f", shade->tiltTarget);
          if(shade->target != 0.0) shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
          delay(100);
          shade->moveToTiltTarget(shade->tiltTarget);
        }
        else
          if(shade->target != 0.0) shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
      }
      shade->p_direction(0);
      tiltStart    = curTime;
      startTiltPos = shade->currentTiltPos;
      if(shade->isAtTarget()) shade->commitShadePosition();
    }
  }
  if(shade->tiltDirection > 0) {
    if(tilt_first) moveStart = curTime;
    int32_t msFrom0 = (int32_t)floor((startTiltPos/100) * tiltTime);
    msFrom0 += (curTime - tiltStart);
    msFrom0 = min(tiltTime, msFrom0);
    if(msFrom0 >= tiltTime) {
      shade->p_currentTiltPos(100.0f);
      ESP_LOGD(TAG, "Setting tiltDirection to 0 (not enough time) %.4f %.4f", msFrom0, tiltTime);
    }
    else {
      float fpos = (min(max((float)0.0, (float)msFrom0 / (float)tiltTime), (float)1.0)) * 100;
      shade->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(shade->currentTiltPos >= 100.0f) {
        shade->p_currentTiltPos(100.0f);
        moveStart = curTime;
        startPos  = shade->currentPos;
        ESP_LOGD(TAG, "Setting tiltDirection to 0 (tilt_first)");
      }
    }
    else if(shade->currentTiltPos >= shade->tiltTarget) {
      shade->p_currentTiltPos(shade->tiltTarget);
      if(motionState.settingTiltPos) {
        if(shade->tiltType == tilt_types::integrated) {
          ESP_LOGD(TAG, "Sending My -- tiltTarget: %.2f, tiltDirection: %d", shade->tiltTarget, shade->tiltDirection);
          if(shade->tiltTarget != 100.0f || shade->currentPos != 100.0f)
            shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        }
        else {
          if(shade->tiltTarget != 100.0f)
            shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        }
      }
      shade->p_tiltDirection(0);
      motionState.settingTiltPos = false;
      if(shade->isAtTarget()) shade->commitShadePosition();
    }
  }
  else if(shade->tiltDirection < 0) {
    if(tilt_first) moveStart = curTime;
    if(tiltTime == 0) {
      shade->p_tiltDirection(0);
      shade->p_currentTiltPos(0.0f);
    }
    else {
      int32_t msFrom100 = tiltTime - (int32_t)floor((startTiltPos/100) * tiltTime);
      msFrom100 += (curTime - tiltStart);
      msFrom100 = min(tiltTime, msFrom100);
      if(msFrom100 >= tiltTime) {
        shade->p_currentTiltPos(0.0f);
      }
      float fpos = ((float)1.0 - min(max((float)0.0, (float)msFrom100 / (float)tiltTime), (float)1.0)) * 100;
      if(fpos <= 0.0f) {
        shade->p_currentTiltPos(0.0f);
      }
      else shade->p_currentTiltPos(fpos);
    }
    if(tilt_first) {
      if(shade->currentTiltPos <= 0.0f) {
        shade->p_currentTiltPos(0.0f);
        moveStart = curTime;
        startPos  = shade->currentPos;
      }
    }
    else if(shade->currentTiltPos <= shade->tiltTarget) {
      shade->p_currentTiltPos(shade->tiltTarget);
      if(motionState.settingTiltPos) {
        if(shade->tiltType == tilt_types::integrated) {
          ESP_LOGD(TAG, "Sending My -- tiltTarget: %.2f, tiltDirection: %d", shade->tiltTarget, shade->tiltDirection);
          if(shade->tiltTarget != 0.0 || shade->currentPos != 0.0)
            shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        }
        else {
          if(shade->tiltTarget != 0.0)
            shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        }
      }
      shade->p_tiltDirection(0);
      motionState.settingTiltPos = false;
      ESP_LOGI(TAG, "Stopping at tilt position");
      if(shade->isAtTarget()) shade->commitShadePosition();
    }
  }
  if(motionState.settingMyPos && shade->isAtTarget()) {
    delay(200);
    if(shade->tiltType != tilt_types::none) {
      if(shade->targetSequencer.myTiltPos == shade->currentTiltPos && shade->targetSequencer.myPos == shade->currentPos)
        shade->targetSequencer.myPos = shade->targetSequencer.myTiltPos = -1;
      else {
        shade->p_myPos(shade->currentPos);
        shade->p_myTiltPos(shade->currentTiltPos);
      }
    }
    else {
      shade->p_myTiltPos(-1);
      if(shade->targetSequencer.myPos == shade->currentPos) shade->p_myPos(-1);
      else shade->p_myPos(shade->currentPos);
    }
    shade->SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
    motionState.settingMyPos = false;
    shade->commitMyPosition();
    shade->emitState();
  }
  else if(currDir != shade->direction || currPos != floor(shade->currentPos) ||
          currTiltDir != shade->tiltDirection || currTiltPos != floor(shade->currentTiltPos)) {
    shade->emitState();
  }
}

void SomfyMovementTracker::setTiltMovement(int8_t dir) {
  int8_t currDir = shade->tiltDirection;
  if(dir == 0) {
    startTiltPos = shade->currentTiltPos;
    tiltStart    = 0;
    shade->p_tiltDirection(dir);
    if(currDir != dir) {
      shade->commitTiltPosition();
    }
  }
  else if(shade->tiltDirection != dir) {
    tiltStart    = millis();
    startTiltPos = shade->currentTiltPos;
    shade->p_tiltDirection(dir);
  }
  if(shade->tiltDirection != currDir) {
    shade->emitState();
  }
}

void SomfyMovementTracker::setMovement(int8_t dir) {
  int8_t currDir     = shade->direction;
  int8_t currTiltDir = shade->tiltDirection;
  if(dir == 0) {
    if(currDir != dir || currTiltDir != dir) shade->commitShadePosition();
  }
  else {
    tiltStart = moveStart = millis();
    startPos     = shade->currentPos;
    startTiltPos = shade->currentTiltPos;
  }
}
