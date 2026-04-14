// SomfyPersistence.cpp — Position-commit and save/publish implementations for SomfyShade.
#include "SomfyShade.h"
#include "SomfyController.h"
#include "SomfyPersistence.h"
#include "compat/preferences.h"
#include "esp_log.h"
#include <cmath>

extern SomfyShadeController somfy;
extern Preferences          pref;

// ── Individual position commits ───────────────────────────────────────────────

void SomfyPersistence::commit() { somfy.commit(); }

void SomfyPersistence::commitShadePosition() {
  somfy.isDirty = true;
}

void SomfyPersistence::commitMyPosition() {
  somfy.isDirty = true;
}

void SomfyPersistence::commitTiltPosition() {
  somfy.isDirty = true;
}

// ── Full save ─────────────────────────────────────────────────────────────────

bool SomfyPersistence::save() {
  shade->commit();
  shade->publish();
  return true;
}
