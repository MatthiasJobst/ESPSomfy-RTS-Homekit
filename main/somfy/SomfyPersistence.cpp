// SomfyPersistence.cpp — NVS load/save and position-commit implementations for SomfyShade.
#include "SomfyShade.h"
#include "SomfyController.h"
#include "SomfyPersistence.h"
#include "compat/preferences.h"
#include "esp_log.h"
#include <cmath>

static const char *PERSIST_TAG = "SomfyPersistence";

extern SomfyShadeController somfy;
extern Preferences          pref;

// ── Individual position commits ───────────────────────────────────────────────

void SomfyPersistence::commit() { somfy.commit(); }

void SomfyPersistence::commitShadePosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  char shadeKey[15];
  if(somfy.useNVS()) {
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", shade->getShadeId());
    ESP_LOGI(PERSIST_TAG, "Writing current shade position: %.4f", shade->currentPos);
    pref.begin(shadeKey);
    pref.putFloat("currentPos", shade->currentPos);
    pref.end();
  }
  #endif
}

void SomfyPersistence::commitMyPosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", shade->getShadeId());
    ESP_LOGI(PERSIST_TAG, "Writing my shade position: %d%%", (int)shade->myPos);
    pref.begin(shadeKey);
    pref.putUShort("myPos", shade->myPos);
    pref.end();
  }
  #endif
}

void SomfyPersistence::commitTiltPosition() {
  somfy.isDirty = true;
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", shade->getShadeId());
    ESP_LOGI(PERSIST_TAG, "Writing current shade tilt position: %.4f", shade->currentTiltPos);
    pref.begin(shadeKey);
    pref.putFloat("currentTiltPos", shade->currentTiltPos);
    pref.end();
  }
  #endif
}

// ── Full save ─────────────────────────────────────────────────────────────────

bool SomfyPersistence::save() {
  #ifdef USE_NVS
  if(somfy.useNVS()) {
    char shadeKey[15];
    snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", shade->getShadeId());
    pref.begin(shadeKey);
    pref.clear();
    pref.putChar("shadeType",     static_cast<uint8_t>(shade->shadeType));
    pref.putUInt("remoteAddress", shade->getRemoteAddress());
    pref.putString("name",        shade->name);
    pref.putBool("hasTilt",       shade->tiltType != tilt_types::none);
    pref.putBool("paired",        shade->paired);
    pref.putUInt("upTime",        shade->upTime);
    pref.putUInt("downTime",      shade->downTime);
    pref.putUInt("tiltTime",      shade->tiltTime);
    pref.putFloat("currentPos",    shade->currentPos);
    pref.putFloat("currentTiltPos",shade->currentTiltPos);
    pref.putUShort("myPos",       shade->myPos);
    uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
    memset(linkedAddresses, 0x00, sizeof(linkedAddresses));
    uint8_t j = 0;
    for(uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
      SomfyLinkedRemote lremote = shade->linkedRemotes[i];
      if(lremote.getRemoteAddress() != 0) linkedAddresses[j++] = lremote.getRemoteAddress();
    }
    pref.remove("linkedAddr");
    pref.putBytes("linkedAddr", linkedAddresses, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
    pref.end();
  }
  #endif
  shade->commit();
  shade->publish();
  return true;
}

// ── Full load ─────────────────────────────────────────────────────────────────

#ifdef USE_NVS
void SomfyPersistence::load() {
  char shadeKey[15];
  uint32_t linkedAddresses[SOMFY_MAX_LINKED_REMOTES];
  memset(linkedAddresses, 0x00, sizeof(uint32_t) * SOMFY_MAX_LINKED_REMOTES);
  snprintf(shadeKey, sizeof(shadeKey), "SomfyShade%u", shade->getShadeId());
  ESP_LOGD(PERSIST_TAG, "key: %s", shadeKey);

  pref.begin(shadeKey, !somfy.useNVS());
  pref.getString("name", shade->name, sizeof(shade->name));
  shade->paired = pref.getBool("paired", false);
  if(pref.isKey("upTime") && pref.getType("upTime") != PreferenceType::PT_U32) {
    // Convert legacy u16 timing fields to u32.
    shade->upTime   = static_cast<uint32_t>(pref.getUShort("upTime",   1000));
    shade->downTime = static_cast<uint32_t>(pref.getUShort("downTime", 1000));
    shade->tiltTime = static_cast<uint32_t>(pref.getUShort("tiltTime", 7000));
    if(somfy.useNVS()) {
      pref.remove("upTime");   pref.putUInt("upTime",   shade->upTime);
      pref.remove("downTime"); pref.putUInt("downTime", shade->downTime);
      pref.remove("tiltTime"); pref.putUInt("tiltTime", shade->tiltTime);
    }
  }
  else {
    shade->upTime   = pref.getUInt("upTime",   shade->upTime);
    shade->downTime = pref.getUInt("downTime",  shade->downTime);
    shade->tiltTime = pref.getUInt("tiltTime",  shade->tiltTime);
  }
  shade->setRemoteAddress(pref.getUInt("remoteAddress", 0));
  shade->currentPos    = pref.getFloat("currentPos", 0);
  shade->target        = floor(shade->currentPos);
  shade->myPos         = static_cast<float>(pref.getUShort("myPos", shade->myPos));
  shade->tiltType      = pref.getBool("hasTilt", false) ? tilt_types::none : tilt_types::tiltmotor;
  shade->shadeType     = static_cast<shade_types>(pref.getChar("shadeType", static_cast<uint8_t>(shade->shadeType)));
  shade->currentTiltPos = pref.getFloat("currentTiltPos", 0);
  shade->tiltTarget    = floor(shade->currentTiltPos);
  pref.getBytes("linkedAddr", linkedAddresses, sizeof(linkedAddresses));
  pref.end();

  ESP_LOGI(PERSIST_TAG, "shadeId: %u name: %s address: %u position: %.2f myPos: %.2f",
           shade->getShadeId(), shade->name, shade->getRemoteAddress(),
           shade->currentPos, shade->myPos);

  pref.begin("ShadeCodes");
  shade->lastRollingCode = pref.getUShort(shade->getRemotePrefId(), 0);
  for(uint8_t j = 0; j < SOMFY_MAX_LINKED_REMOTES; j++) {
    SomfyLinkedRemote &lremote = shade->linkedRemotes[j];
    lremote.setRemoteAddress(linkedAddresses[j]);
    lremote.lastRollingCode = pref.getUShort(lremote.getRemotePrefId(), 0);
  }
  pref.end();
}
#endif
