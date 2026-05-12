// SomfyMovementTracker.cpp — position interpolation, direction management and
// movement-stop logic extracted from SomfyShade.  All logic is unchanged; only
// the receiver changes from `this` (SomfyShade) to `shade` (SomfyShade*).
#include "esp_log.h"
#include "SomfyMovementTracker.h"
#include "SomfyShade.h"
#include "SomfyTransceiver.h"

static const char *s_TAG = "SomfyMovementTracker";

bool SomfyMovementTracker::computeDirections()
{
    shade->p_direction(static_cast<int8_t>(shade->currentPos == shade->target  ? 0
                                           : shade->currentPos > shade->target ? -1
                                                                               : 1));
    bool tilt_first =
        shade->tiltType == tilt_types::integrated && ((shade->direction == -1 && shade->currentTiltPos != 0.0f) ||
                                                      (shade->direction == 1 && shade->currentTiltPos != 100.0f));
    shade->p_tiltDirection(static_cast<int8_t>(shade->currentTiltPos == shade->tiltTarget  ? 0
                                               : shade->currentTiltPos > shade->tiltTarget ? -1
                                                                                           : 1));
    if (tilt_first)
        shade->p_tiltDirection(shade->direction);
    else if (shade->direction != 0)
        shade->p_tiltDirection(0);
    return tilt_first;
}

void SomfyMovementTracker::tickFlagTimers(uint64_t curTime)
{
    auto tick = shade->flagManager.tickTimers(shade->flags, curTime, shade->shadeId);
    if (tick.setSunTarget) {
        shade->p_target(shade->getMyPos() >= 0 ? shade->getMyPos() : 100.0f);
    }
    if (tick.setNoSunTarget || tick.setWindTarget) {
        if (shade->tiltType == tilt_types::tiltonly) shade->p_tiltTarget(0.0f);
        shade->p_target(0.0f);
    }
}

float SomfyMovementTracker::calcInterpolatedPos(float startPct, uint64_t elapsed, int32_t totalTime, int8_t dir)
{
    // msFromStart: distance already travelled in ms, clamped to [0, totalTime]
    int32_t msFromStart = (int32_t)floorf((startPct / 100.0f) * static_cast<float>(totalTime));
    if (dir < 0) msFromStart = totalTime - msFromStart; // up: invert so 0 = "just started from 100"
    msFromStart = min(totalTime, msFromStart + (int32_t)elapsed);
    float ratio = min(max(0.0f, (float)msFromStart / (float)totalTime), 1.0f);
    return dir > 0 ? ratio * 100.0f : (1.0f - ratio) * 100.0f;
}

void SomfyMovementTracker::handlePosTargetReached(float endpoint, uint64_t curTime)
{
    shade->p_currentPos(shade->target);
    if (motionState.settingPos) {
        // Boosted moves (e.g. HomeKit's moveToTargetForced) need their auto-stop
        // delivered with the same repeat count as the start command — a missed
        // stop would cause the shade to overshoot the intermediate target.
        const uint8_t stopRepeats = motionState.boostedStop ? MOVE_REPEATS : shade->repeats;
        if (!shade->isAtTarget()) {
            ESP_LOGI(s_TAG, "We are not at our tilt target: %.2f", shade->tiltTarget);
            if (shade->target != endpoint) shade->SomfyRemote::sendCommand(somfy_commands::My, stopRepeats);
            delay(100);
            shade->moveToTiltTarget(shade->tiltTarget);
        } else {
            if (shade->target != endpoint) shade->SomfyRemote::sendCommand(somfy_commands::My, stopRepeats);
        }
        motionState.boostedStop = false;
    }
    shade->p_direction(0);
    tiltStart = curTime;
    startTiltPos = shade->currentTiltPos;
    if (shade->isAtTarget()) shade->commitShadePosition();
}

void SomfyMovementTracker::handleTiltTargetReached(float endpoint)
{
    shade->p_currentTiltPos(shade->tiltTarget);
    if (motionState.settingTiltPos) {
        if (shade->tiltType == tilt_types::integrated) {
            ESP_LOGD(s_TAG, "Sending My -- tiltTarget: %.2f, tiltDirection: %d", shade->tiltTarget, shade->tiltDirection);
            if (shade->tiltTarget != endpoint || shade->currentPos != endpoint)
                shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        } else {
            if (shade->tiltTarget != endpoint) shade->SomfyRemote::sendCommand(somfy_commands::My, shade->repeats);
        }
    }
    shade->p_tiltDirection(0);
    motionState.settingTiltPos = false;
    if (shade->isAtTarget()) shade->commitShadePosition();
}

void SomfyMovementTracker::checkMovement()
{
    const uint64_t curTime = millis();
    int32_t downTime = (int32_t)shade->getDownTime();
    int32_t upTime = (int32_t)shade->getUpTime();
    int32_t tiltTime = (int32_t)shade->getTiltTime();
    if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2)
        downTime = upTime = tiltTime = 1;

    // Snapshot pre-tick state for the emit-state diff at the bottom.
    int8_t currDir = shade->direction;
    int8_t currTiltDir = shade->tiltDirection;
    uint8_t currPos = static_cast<uint8_t>(floorf(shade->currentPos));
    uint8_t currTiltPos = static_cast<uint8_t>(floorf(shade->currentTiltPos));

    // Compute directions from current pos vs target. Intentionally called before
    // tickFlagTimers so direction is stable for one tick when a flag overrides the target.
    bool tilt_first = computeDirections();

    if (shade->direction != 0) shade->setLastMovement(shade->direction);

    // Advance sun/wind timers; may shift target/tiltTarget for this tick's interpolation.
    tickFlagTimers(curTime);

    // ── Shade position interpolation ──────────────────────────────────────────
    // Skipped entirely when tilt_first: the integrated tilt must reach its
    // endpoint before the shade starts moving. While tilt_first is active the
    // tilt block below continuously resets moveStart so shade starts from t=0.
    if (!tilt_first && shade->direction > 0) {
        shade->p_currentPos(downTime == 0 ? 100.0f : calcInterpolatedPos(startPos, curTime - moveStart, downTime, 1));
        if (shade->currentPos >= shade->target) handlePosTargetReached(100.0f, curTime);
    } else if (!tilt_first && shade->direction < 0) {
        shade->p_currentPos(upTime == 0 ? 0.0f : calcInterpolatedPos(startPos, curTime - moveStart, upTime, -1));
        if (shade->currentPos <= shade->target) handlePosTargetReached(0.0f, curTime);
    }

    // ── Tilt position interpolation ───────────────────────────────────────────
    // Runs regardless of tilt_first. When tilt_first is active, moveStart is
    // reset each tick so that once tilt completes the shade clock starts fresh.
    if (shade->tiltDirection > 0) {
        if (tilt_first) moveStart = curTime;
        if (tiltTime == 0) {
            shade->p_currentTiltPos(100.0f);
        } else {
            shade->p_currentTiltPos(calcInterpolatedPos(startTiltPos, curTime - tiltStart, tiltTime, 1));
        }
        if (tilt_first) {
            if (shade->currentTiltPos >= 100.0f) {
                shade->p_currentTiltPos(100.0f);
                moveStart = curTime;
                startPos = shade->currentPos;
            }
        } else if (shade->currentTiltPos >= shade->tiltTarget) {
            handleTiltTargetReached(100.0f);
        }
    } else if (shade->tiltDirection < 0) {
        if (tilt_first) moveStart = curTime;
        if (tiltTime == 0) {
            shade->p_tiltDirection(0);
            shade->p_currentTiltPos(0.0f);
        } else {
            shade->p_currentTiltPos(calcInterpolatedPos(startTiltPos, curTime - tiltStart, tiltTime, -1));
        }
        if (tilt_first) {
            if (shade->currentTiltPos <= 0.0f) {
                shade->p_currentTiltPos(0.0f);
                moveStart = curTime;
                startPos = shade->currentPos;
            }
        } else if (shade->currentTiltPos <= shade->tiltTarget) {
            handleTiltTargetReached(0.0f);
        }
    }
    // tiltDirection == 0: not tilting, nothing to interpolate.
    if (motionState.settingMyPos && shade->isAtTarget()) {
        delay(200);
        if (shade->tiltType != tilt_types::none) {
            if (shade->getMyTiltPos() == shade->currentTiltPos && shade->getMyPos() == shade->currentPos) {
                shade->p_myPos(-1);
                shade->p_myTiltPos(-1);
            } else {
                shade->p_myPos(shade->currentPos);
                shade->p_myTiltPos(shade->currentTiltPos);
            }
        } else {
            shade->p_myTiltPos(-1);
            if (shade->getMyPos() == shade->currentPos)
                shade->p_myPos(-1);
            else
                shade->p_myPos(shade->currentPos);
        }
        shade->SomfyRemote::sendCommand(somfy_commands::My, SETMY_REPEATS);
        motionState.settingMyPos = false;
        shade->commitMyPosition();
        shade->emitState();
    } else if (currDir != shade->direction || currPos != static_cast<uint8_t>(floorf(shade->currentPos)) ||
               currTiltDir != shade->tiltDirection ||
               currTiltPos != static_cast<uint8_t>(floorf(shade->currentTiltPos))) {
        shade->emitState();
    }
}

void SomfyMovementTracker::setTiltMovement(int8_t dir)
{
    int8_t currDir = shade->tiltDirection;
    if (dir == 0) {
        startTiltPos = shade->currentTiltPos;
        tiltStart = 0;
        shade->p_tiltDirection(dir);
        if (currDir != dir) {
            shade->commitTiltPosition();
        }
    } else if (shade->tiltDirection != dir) {
        tiltStart = millis();
        startTiltPos = shade->currentTiltPos;
        shade->p_tiltDirection(dir);
    }
    if (shade->tiltDirection != currDir) {
        shade->emitState();
    }
}

void SomfyMovementTracker::setMovement(int8_t dir)
{
    int8_t currDir = shade->direction;
    int8_t currTiltDir = shade->tiltDirection;
    if (dir == 0) {
        if (currDir != dir || currTiltDir != dir) shade->commitShadePosition();
    } else {
        tiltStart = moveStart = millis();
        startPos = shade->currentPos;
        startTiltPos = shade->currentTiltPos;
    }
}
