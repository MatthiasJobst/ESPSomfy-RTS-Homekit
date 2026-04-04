// SomfyTargetSequencer.cpp — Programmatic position-seek implementations.
#include "SomfyShade.h"
#include "SomfyTargetSequencer.h"
#include "SomfyTransceiver.h"
#include "esp_log.h"
#include <cmath>

static const char *TAG = "SomfyTargetSequencer";

// ── moveToTarget ─────────────────────────────────────────────────────────────

void SomfyTargetSequencer::moveToTarget(float pos, float tilt) {
  somfy_commands cmd = somfy_commands::My;
  if(shade->isToggle()) {
    shade->p_target(pos);
    shade->p_currentPos(pos);
    shade->emitState();
    return;
  }
  if(shade->tiltType == tilt_types::tiltonly) {
    shade->p_target(100.0f);
    shade->p_myPos(-1.0f);
    shade->p_currentPos(100.0f);
    pos = 100;
    if(tilt < shade->currentTiltPos) cmd = somfy_commands::Up;
    else if(tilt > shade->currentTiltPos) cmd = somfy_commands::Down;
  }
  else {
    if(pos < shade->currentPos)
      cmd = somfy_commands::Up;
    else if(pos > shade->currentPos)
      cmd = somfy_commands::Down;
    else if(tilt >= 0 && tilt < shade->currentTiltPos)
      cmd = somfy_commands::Up;
    else if(tilt >= 0 && tilt > shade->currentTiltPos)
      cmd = somfy_commands::Down;
  }
  if(cmd != somfy_commands::My) {
    ESP_LOGI(TAG, "Moving to %f%% from %f%%", pos, shade->currentPos);
    if(tilt >= 0) {
      ESP_LOGI(TAG, " tilt %f%% from %f%%", tilt, shade->currentTiltPos);
    }
    ESP_LOGI(TAG, " using %s", translateSomfyCommand(cmd).c_str());
    shade->SomfyRemote::sendCommand(cmd, shade->tiltType == tilt_types::euromode ? TILT_REPEATS : shade->repeats);
    shade->movementTracker.motionState.settingPos = true;
    shade->p_target(pos);
    if(tilt >= 0) {
      shade->p_tiltTarget(tilt);
      shade->movementTracker.motionState.settingTiltPos = true;
    }
  }
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
  if(cmd != somfy_commands::My) shade->movementTracker.motionState.settingTiltPos = true;
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
  shade->movementTracker.motionState.settingPos = false;
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
      shade->movementTracker.motionState.settingMyPos = true;
      if(tilt == floor(myTiltPos))
        moveToMyPosition();
      else
        moveToTarget(100, tilt);
    }
    else if(tilt == floor(myTiltPos)) {
      if(shade->currentTiltPos != myTiltPos) {
        shade->movementTracker.motionState.settingMyPos = true;
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->movementTracker.motionState.settingPos = false;
        shade->movementTracker.motionState.settingMyPos = true;
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
      shade->movementTracker.motionState.settingMyPos = true;
      if(pos == floor(myPos) && tilt == floor(myTiltPos))
        moveToMyPosition();
      else
        moveToTarget(pos, tilt);
    }
    else if(pos == floor(myPos) && tilt == floor(myTiltPos)) {
      if(shade->currentPos != myPos || shade->currentTiltPos != myTiltPos) {
        shade->movementTracker.motionState.settingMyPos = true;
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->movementTracker.motionState.settingPos = false;
        shade->movementTracker.motionState.settingMyPos = true;
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
      shade->movementTracker.motionState.settingMyPos = true;
      if(pos == floor(myPos))
        moveToMyPosition();
      else
        moveToTarget(pos);
    }
    else if(pos == floor(myPos)) {
      if(myPos != shade->currentPos) {
        shade->movementTracker.motionState.settingMyPos = true;
        moveToMyPosition();
      }
      else {
        shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        shade->movementTracker.motionState.settingPos = false;
        shade->movementTracker.motionState.settingMyPos = true;
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
