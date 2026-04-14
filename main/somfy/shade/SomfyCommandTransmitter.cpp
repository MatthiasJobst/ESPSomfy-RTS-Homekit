// SomfyCommandTransmitter.cpp — Outbound RF command and linked-remote implementations.
#include "SomfyShade.h"
#include "SomfyShadeController.h"
#include "SomfyCommandTransmitter.h"
#include "SomfyTransceiver.h"
#include "compat/preferences.h"
#include "esp_log.h"
#include <cstring>

extern SomfyShadeController somfy;
extern Preferences          pref;

// ── sendCommand ───────────────────────────────────────────────────────────────

void SomfyCommandTransmitter::sendCommand(somfy_commands cmd) {
  this->sendCommand(cmd, shade->repeats);
}

void SomfyCommandTransmitter::sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize) {
  if(shade->bitLength == 0) shade->bitLength = somfy.transceiver.config.type;
  if(cmd == somfy_commands::Up) {
    if(shade->tiltType == tilt_types::euromode) {
      shade->SomfyRemote::sendCommand(cmd, TILT_REPEATS);
      shade->p_target(0.0f);
    }
    else {
      shade->SomfyRemote::sendCommand(cmd, repeat);
      if(shade->tiltType == tilt_types::tiltonly) {
        shade->p_target(100.0f);
        shade->p_tiltTarget(0.0f);
        shade->p_currentPos(100.0f);
      }
      else shade->p_target(0.0f);
      if(shade->tiltType == tilt_types::integrated) shade->p_tiltTarget(0.0f);
    }
  }
  else if(cmd == somfy_commands::Down) {
    if(shade->tiltType == tilt_types::euromode) {
      shade->SomfyRemote::sendCommand(cmd, TILT_REPEATS);
      shade->p_target(100.0f);
    }
    else {
      shade->SomfyRemote::sendCommand(cmd, repeat);
      if(shade->tiltType == tilt_types::tiltonly) {
        shade->p_target(100.0f);
        shade->p_tiltTarget(100.0f);
        shade->p_currentPos(100.0f);
      }
      else shade->p_target(100.0f);
      if(shade->tiltType == tilt_types::integrated) shade->p_tiltTarget(100.0f);
    }
  }
  else if(cmd == somfy_commands::My) {
    if(shade->isToggle() || shade->shadeType == shade_types::drycontact)
      shade->SomfyRemote::sendCommand(cmd, repeat);
    else if(shade->shadeType == shade_types::drycontact2) return;
    else if(shade->isIdle()) {
      shade->moveToMyPosition();
      return;
    }
    else {
      shade->SomfyRemote::sendCommand(cmd, repeat);
      if(shade->tiltType != tilt_types::tiltonly) shade->p_target(shade->currentPos);
      shade->p_tiltTarget(shade->currentTiltPos);
    }
  }
  else if(cmd == somfy_commands::Toggle) {
    if(shade->bitLength != 80) shade->SomfyRemote::sendCommand(somfy_commands::My, repeat, stepSize);
    else                       shade->SomfyRemote::sendCommand(somfy_commands::Toggle, repeat);
  }
  else if(shade->isToggle() && cmd == somfy_commands::Prog) {
    shade->SomfyRemote::sendCommand(somfy_commands::Toggle, repeat, stepSize);
  }
  else {
    shade->SomfyRemote::sendCommand(cmd, repeat, stepSize);
  }
}

void SomfyCommandTransmitter::sendTiltCommand(somfy_commands cmd) {
  uint8_t rpt = shade->tiltType == tilt_types::tiltmotor ? TILT_REPEATS : shade->repeats;
  if(cmd == somfy_commands::Up) {
    shade->SomfyRemote::sendCommand(cmd, rpt);
    shade->p_tiltTarget(0.0f);
  }
  else if(cmd == somfy_commands::Down) {
    shade->SomfyRemote::sendCommand(cmd, rpt);
    shade->p_tiltTarget(100.0f);
  }
  else if(cmd == somfy_commands::My) {
    shade->SomfyRemote::sendCommand(cmd, rpt);
    shade->p_tiltTarget(shade->currentTiltPos);
  }
}

// ── linkRemote / unlinkRemote ────────────────────────────────────────────────

bool SomfyCommandTransmitter::linkRemote(uint32_t address, uint16_t rollingCode) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(linkedRemotes[i].getRemoteAddress() == address) {
      linkedRemotes[i].setRollingCode(rollingCode);
      return true;
    }
  }
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(linkedRemotes[i].getRemoteAddress() == 0) {
      linkedRemotes[i].setRemoteAddress(address);
      linkedRemotes[i].setRollingCode(rollingCode);
      shade->commit();
      return true;
    }
  }
  return false;
}

bool SomfyCommandTransmitter::unlinkRemote(uint32_t address) {
  for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
    if(linkedRemotes[i].getRemoteAddress() == address) {
      linkedRemotes[i].setRemoteAddress(0);
      shade->commit();
      return true;
    }
  }
  return false;
}
