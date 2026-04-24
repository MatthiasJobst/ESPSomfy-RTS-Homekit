// SomfyTargetSequencer.cpp — Programmatic position-seek implementations.
#include "SomfyShade.h"
#include "SomfyTargetSequencer.h"
#include "SomfyTransceiver.h"
#include "esp_log.h"
#include <cmath>

static const char *TAG = "SomfyTargetSequencer";


// ── moveDirection ─────────────────────────────────────────────────────────────

somfy_commands SomfyTargetSequencer::moveDirection(float &pos, float tilt) {
  if(shade->isToggle()) {
    shade->p_target(pos); shade->p_currentPos(pos); shade->emitState();
    return somfy_commands::My;
  }
  if(shade->tiltType == tilt_types::tiltonly) {
    shade->p_target(100.0f); shade->p_myPos(-1.0f); shade->p_currentPos(100.0f);
    pos = 100.0f;
    if(tilt < shade->currentTiltPos) return somfy_commands::Up;
    if(tilt > shade->currentTiltPos) return somfy_commands::Down;
    return somfy_commands::My;
  }
  if(pos < shade->currentPos) return somfy_commands::Up;
  if(pos > shade->currentPos) return somfy_commands::Down;
  if(tilt < 0)                return somfy_commands::My;
  if(tilt < shade->currentTiltPos) return somfy_commands::Up;
  if(tilt > shade->currentTiltPos) return somfy_commands::Down;
  return somfy_commands::My;
}

// ── repeatCount ───────────────────────────────────────────────────────────────

uint8_t SomfyTargetSequencer::repeatCount() const {
  if(shade->tiltType == tilt_types::euromode)  return TILT_REPEATS;
  if(shade->tiltType == tilt_types::tiltonly)  return shade->repeats;
  if(shade->tiltType == tilt_types::tiltmotor) return shade->repeats;
  if(shade->getUpTime() > 0 && shade->getDownTime() > 0) return MOVE_REPEATS;
  return shade->repeats;
}

// ── moveToTarget ─────────────────────────────────────────────────────────────

void SomfyTargetSequencer::moveToTarget(float pos, float tilt) {
  somfy_commands cmd = this->moveDirection(pos, tilt);
  if(cmd == somfy_commands::My) {
    ESP_LOGI(TAG, "No need to move!");
    return;
  }
  ESP_LOGI(TAG, "Moving to %f%% from %f%%", pos, shade->currentPos);
  if(tilt >= 0) ESP_LOGI(TAG, " tilt %f%% from %f%%", tilt, shade->currentTiltPos);
  ESP_LOGI(TAG, " using %s", translateSomfyCommand(cmd).c_str());
  shade->SomfyRemote::sendCommand(cmd, this->repeatCount());
  shade->setSettingPos(true);
  shade->p_target(pos);
  if(tilt >= 0) { shade->p_tiltTarget(tilt); shade->setSettingTiltPos(true); }
}

// ── moveToTargetForced: moves to Target with high repeat to force up or down ─

void SomfyTargetSequencer::moveToTargetForced(float pos, float tilt) {
  somfy_commands cmd = this->moveDirection(pos, tilt);
  if(cmd == somfy_commands::My) {
    ESP_LOGI(TAG, "No need to move!");
    return;
  }
  ESP_LOGI(TAG, "Moving forced to %f%% from %f%%", pos, shade->currentPos);
  ESP_LOGW(TAG, "Tilt is set to %f%% will be ignored in forced move", tilt);
  ESP_LOGI(TAG, " using %s", translateSomfyCommand(cmd).c_str());
  shade->SomfyRemote::sendCommand(cmd, MOVE_REPEATS);
  shade->setSettingPos(true);
  shade->p_target(pos);
}

// ── moveToTiltTarget ─────────────────────────────────────────────────────────

void SomfyTargetSequencer::moveToTiltTarget(float target) {
  somfy_commands cmd = somfy_commands::My;
  if(target < shade->currentTiltPos)
    cmd = somfy_commands::Up;
  else if(target > shade->currentTiltPos)
    cmd = somfy_commands::Down;
  if(target >= 0.0f && target <= 100.0f) {
    // Only send a command if the lift is not moving.
    if(shade->currentPos == shade->target || shade->tiltType == tilt_types::tiltmotor) {
      if(cmd != somfy_commands::My) {
        ESP_LOGI(TAG, "Moving Tilt to %f%% from %f%% using %s", target, shade->currentTiltPos, translateSomfyCommand(cmd));
        shade->SomfyRemote::sendCommand(cmd, shade->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : shade->repeats);
      }
      // If the blind is currently moving then the command to stop it
      // will occur on its own when the tilt target is set.
    }
    shade->p_tiltTarget(target);
  }
  if(cmd != somfy_commands::My) shade->setSettingTiltPos(true);
}

// ── moveToMyPosition ─────────────────────────────────────────────────────────

void SomfyTargetSequencer::moveToMyPosition() {
  if(!shade->isIdle()) return;
  ESP_LOGI(TAG, "Moving to My Position");
  if(shade->tiltType == tilt_types::tiltonly) {
    shade->p_currentPos(100.0f);
    shade->p_myPos(-1.0f);
  }
  if(shade->currentPos == myPos) {
    if(shade->tiltType != tilt_types::none) {
      if(shade->currentTiltPos == myTiltPos) return;
    }
    else
      return;
  }
  if(myPos == -1 && (shade->tiltType == tilt_types::none || myTiltPos == -1)) return;
  if(shade->tiltType != tilt_types::tiltonly && myPos >= 0.0f && myPos <= 100.0f) shade->p_target(myPos);
  if(myTiltPos >= 0.0f && myTiltPos <= 100.0f) shade->p_tiltTarget(myTiltPos);
  shade->setSettingPos(false);
  if(shade->simMy()) {
    ESP_LOGI(TAG, "Moving to simulated favorite position");
    moveToTarget(myPos, myTiltPos);
  }
  else
    shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
}

// ── setMyPosition ─────────────────────────────────────────────────────────────

void SomfyTargetSequencer::setMyPosition(int8_t pos, int8_t tilt) {
  if(!shade->isIdle()) return;
  if(shade->tiltType == tilt_types::tiltonly) {
    shade->p_myPos(-1.0f);
    if(tilt != floor(shade->currentTiltPos)) {
      shade->setSettingMyPos(true);
      if(tilt == floor(myTiltPos))
        moveToMyPosition();
      else
        moveToTarget(100, tilt);
    }
    else if(tilt == floor(myTiltPos)) {
      if(shade->currentTiltPos != myTiltPos) {
        shade->setSettingMyPos(true);
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->setSettingPos(false);
        shade->setSettingMyPos(true);
      }
    }
    else {
      shade->SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      shade->p_myTiltPos(shade->currentTiltPos);
    }
    shade->commitMyPosition();
    shade->emitState();
  }
  else if(shade->tiltType != tilt_types::none) {
    if(tilt < 0) tilt = 0;
    if(pos != floor(shade->currentPos) || tilt != floor(shade->currentTiltPos)) {
      shade->setSettingMyPos(true);
      if(pos == floor(myPos) && tilt == floor(myTiltPos))
        moveToMyPosition();
      else
        moveToTarget(pos, tilt);
    }
    else if(pos == floor(myPos) && tilt == floor(myTiltPos)) {
      if(shade->currentPos != myPos || shade->currentTiltPos != myTiltPos) {
        shade->setSettingMyPos(true);
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->setSettingPos(false);
        shade->setSettingMyPos(true);
      }
    }
    else {
      shade->SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      shade->p_myPos(shade->currentPos);
      shade->p_myTiltPos(shade->currentTiltPos);
    }
    shade->commitMyPosition();
    shade->emitState();
  }
  else {
    if(pos != floor(shade->currentPos)) {
      shade->setSettingMyPos(true);
      if(pos == floor(myPos))
        moveToMyPosition();
      else
        moveToTarget(pos);
    }
    else if(pos == floor(myPos)) {
      if(myPos != shade->currentPos) {
        shade->setSettingMyPos(true);
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->setSettingPos(false);
        shade->setSettingMyPos(true);
      }
    }
    else {
      shade->SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
      shade->p_myPos(shade->currentPos);
      shade->p_myTiltPos(-1);
      shade->commitMyPosition();
      shade->emitState();
    }
  }
}
