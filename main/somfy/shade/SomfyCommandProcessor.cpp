// SomfyCommandProcessor.cpp — frame dispatch, internal commands, and waiting-frame
// resolution extracted from SomfyShade.  All logic is unchanged; only the receiver
// changes from `this` (SomfyShade) to `shade` (SomfyShade*).
#include "esp_log.h"
#include "SomfyCommandProcessor.h"
#include "SomfyShade.h"
#include "SomfyRepeatCounts.h"
#include "SomfyShadeController.h"

static const char *s_TAG = "SomfyCommandProcessor";

extern SomfyShadeController somfy;

void SomfyCommandProcessor::processWaitingFrame()
{
    if (shade->getShadeId() == 255) {
        shade->lastFrame.await = 0;
        return;
    }
    if (shade->lastFrame.processed) return;
    if (shade->lastFrame.await > 0 && (millis() > shade->lastFrame.await)) {
        somfy_commands cmd = shade->transformCommand(shade->lastFrame.cmd);
        switch (cmd) {
        case somfy_commands::StepUp:
            shade->lastFrame.processed = true;
            // Simply move the shade up by 1%.
            if (shade->currentPos > 0) {
                shade->p_target(floorf(shade->currentPos) - 1);
                shade->setMovement(-1);
                shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
            }
            break;
        case somfy_commands::StepDown:
            shade->lastFrame.processed = true;
            // Simply move the shade down by 1%.
            if (shade->currentPos < 100) {
                shade->p_target(floorf(shade->currentPos) + 1);
                shade->setMovement(1);
                shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
            }
            break;
        case somfy_commands::Down:
        case somfy_commands::Up:
            if (shade->tiltType ==
                tilt_types::tiltmotor) { // Theoretically this should get here unless it does have a tilt motor.
                if (shade->lastFrame.repeats >= TILT_REPEATS) {
                    int8_t dir = shade->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
                    shade->p_tiltTarget(dir > 0 ? 100.0f : 0.0f);
                    shade->setTiltMovement(dir);
                    shade->lastFrame.processed = true;
                    ESP_LOGI(s_TAG, "%s Processing tilt %s after %d repeats", shade->name,
                             translateSomfyCommand(shade->lastFrame.cmd).c_str(), shade->lastFrame.repeats);
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                } else {
                    int8_t dir = shade->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
                    shade->p_target(dir > 0 ? 100 : 0);
                    shade->setMovement(dir);
                    shade->lastFrame.processed = true;
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                }
                if (shade->lastFrame.repeats > TILT_REPEATS + 2) {
                    shade->lastFrame.processed = true;
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                }
            } else if (shade->tiltType == tilt_types::euromode) {
                if (shade->lastFrame.repeats >= TILT_REPEATS) {
                    int8_t dir = shade->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
                    shade->p_target(dir > 0 ? 100.0f : 0.0f);
                    shade->setMovement(dir);
                    shade->lastFrame.processed = true;
                    ESP_LOGI(s_TAG, "%s Processing %s after %d repeats", shade->name,
                             translateSomfyCommand(shade->lastFrame.cmd).c_str(), shade->lastFrame.repeats);
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                } else {
                    int8_t dir = shade->lastFrame.cmd == somfy_commands::Up ? -1 : 1;
                    shade->p_tiltTarget(dir > 0 ? 100 : 0);
                    shade->setTiltMovement(dir);
                    shade->lastFrame.processed = true;
                    ESP_LOGI(s_TAG, "%s Processing tilt %s after %d repeats", shade->name,
                             translateSomfyCommand(shade->lastFrame.cmd).c_str(), shade->lastFrame.repeats);
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                }
                if (shade->lastFrame.repeats > TILT_REPEATS + 2) {
                    shade->lastFrame.processed = true;
                    shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
                }
            }
            break;
        case somfy_commands::My:
            if (shade->lastFrame.repeats >= SETMY_REPEATS && shade->isIdle()) {
                if (floorf(shade->getMyPos()) == floorf(shade->currentPos)) {
                    // We are clearing it.
                    shade->p_myPos(-1);
                    shade->p_myTiltPos(-1);
                } else {
                    shade->p_myPos(shade->currentPos);
                    shade->p_myTiltPos(shade->currentTiltPos);
                }
                shade->commitMyPosition();
                shade->lastFrame.processed = true;
                shade->emitState();
            } else if (shade->isIdle()) {
                if (shade->simMy())
                    shade->moveToMyPosition(); // Call out like this (instead of move to target) so that we don't get
                                               // some of the goofy tilt only problems.
                else {
                    if (shade->getMyPos() >= 0.0f && shade->getMyPos() <= 100.0f) shade->p_target(shade->getMyPos());
                    if (shade->getMyTiltPos() >= 0.0f && shade->getMyTiltPos() <= 100.0f)
                        shade->p_tiltTarget(shade->getMyTiltPos());
                }
                shade->setMovement(0);
                shade->lastFrame.processed = true;
                shade->emitCommand(cmd, "remote", shade->lastFrame.remoteAddress);
            } else {
                shade->p_target(shade->currentPos);
                shade->p_tiltTarget(shade->currentTiltPos);
            }
            if (shade->lastFrame.repeats > SETMY_REPEATS + 2) shade->lastFrame.processed = true;
            if (shade->lastFrame.processed) {
                ESP_LOGI(s_TAG, "%s Processing MY after %d repeats", shade->name, shade->lastFrame.repeats);
            }
            break;
        default:
            break;
        }
    }
}

void SomfyCommandProcessor::processSensorCommand(somfy_frame_t &frame, uint64_t curTime)
{
    const bool wasSunny = shade->flags.isSunny();
    const bool wasWindy = shade->flags.isWindy();
    const uint16_t status = frame.rollingCode << 4;
    shade->p_sunny(SomfyFlag::isSunny(status));
    shade->p_windy(SomfyFlag::isWindy(status));
    shade->p_demoMode(SomfyFlag::isDemoMode(frame.rollingCode));
    shade->flagManager.updateTimers(wasSunny, wasWindy, shade->flags.isSunny(), shade->flags.isWindy(), curTime,
                                    shade->getShadeId());
    shade->emitState();
    somfy.groupController.updateGroupFlags();
}

void SomfyCommandProcessor::processFlagCommand(bool internal, somfy_frame_t &frame)
{
    shade->p_sunFlag(false);
    somfy.store.markDirty();
    shade->emitState();
    shade->emitCommand(frame.cmd, internal ? "internal" : "remote", frame.remoteAddress);
    somfy.groupController.updateGroupFlags();
}

void SomfyCommandProcessor::processSunFlagCommand(bool internal, somfy_frame_t &frame)
{
    shade->p_sunFlag(true);
    if (!(shade->flags.isWindy())) {
        const bool isSunny = shade->flags.isSunny();
        if (isSunny && shade->flagManager.sunDone) {
            if (shade->tiltType == tilt_types::tiltonly)
                shade->p_tiltTarget(shade->getMyTiltPos() >= 0 ? shade->getMyTiltPos() : 100.0f);
            else
                shade->p_target(shade->getMyPos() >= 0 ? shade->getMyPos() : 100.0f);
        } else if (!isSunny && shade->flagManager.noSunDone) {
            if (shade->tiltType == tilt_types::tiltonly)
                shade->p_tiltTarget(0.0f);
            else
                shade->p_target(0.0f);
        }
    }
    somfy.store.markDirty();
    shade->emitState();
    shade->emitCommand(frame.cmd, internal ? "internal" : "remote", frame.remoteAddress);
    somfy.groupController.updateGroupFlags();
}

void SomfyCommandProcessor::processMyCommand(bool internal, somfy_frame_t &frame, uint64_t curTime)
{
    const somfy_commands cmd = frame.cmd;
    if (shade->isToggle()) {
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (!shade->isIdle())
            shade->p_target(shade->currentPos);
        else if (shade->currentPos == 100.0f)
            shade->p_target(0.0f);
        else if (shade->currentPos == 0.0f)
            shade->p_target(100.0f);
        else
            shade->p_target(shade->getLastMovement() == -1 ? 100 : 0);
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
    }
    if (shade->shadeType == shade_types::drycontact) {
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (shade->currentPos == 100.0f)
            shade->p_target(0);
        else if (shade->currentPos == 0.0f)
            shade->p_target(100);
        else
            shade->p_target(shade->getLastMovement() == -1 ? 100 : 0);
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
    }
    if (shade->isIdle()) {
        if (!internal) {
            shade->lastFrame.await = curTime + 500;
        } else {
            if (shade->lastFrame.processed) return;
            ESP_LOGI(s_TAG, "Moving to My target");
            shade->lastFrame.processed = true;
            if (shade->getMyTiltPos() >= 0.0f && shade->getMyTiltPos() <= 100.0f)
                shade->p_tiltTarget(shade->getMyTiltPos());
            if (shade->getMyPos() >= 0.0f && shade->getMyPos() <= 100.0f && shade->tiltType != tilt_types::tiltonly)
                shade->p_target(shade->getMyPos());
            shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        }
    } else {
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (!internal) {
            if (shade->tiltType != tilt_types::tiltonly) shade->p_target(shade->currentPos);
            shade->p_tiltTarget(shade->currentTiltPos);
        }
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
    }
}

// moveDir: -1 = Up (target 0), +1 = Down (target 100)
void SomfyCommandProcessor::processUpDownCommand(somfy_commands cmd, int8_t moveDir, bool internal,
                                                 somfy_frame_t &frame, uint64_t curTime)
{
    const float endpoint = moveDir < 0 ? 0.0f : 100.0f;
    if (shade->shadeType == shade_types::drycontact) {
        shade->lastFrame.processed = true;
        return;
    }
    if (shade->shadeType == shade_types::drycontact2) {
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (shade->currentPos != endpoint) shade->p_target(endpoint);
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        return;
    }
    // Down is suppressed for a period after a wind event.
    if (moveDir > 0 && shade->flagManager.windLast &&
        (curTime - shade->flagManager.windLast) < SOMFY_NO_WIND_REMOTE_TIMEOUT)
        return;

    if (shade->tiltType == tilt_types::tiltmotor || shade->tiltType == tilt_types::euromode) {
        if (!internal)
            shade->lastFrame.await = curTime + 500;
        else
            shade->lastFrame.processed = true;
        // Up: emitCommand is deferred to processWaitingFrame when await is set.
        // Down: emitCommand fires immediately so the motor starts moving.
        if (moveDir < 0 && !internal) return;
    } else {
        if (!internal) {
            if (moveDir < 0) {
                // Up: tiltonly only moves tilt; everything else moves both
                if (shade->tiltType == tilt_types::tiltonly)
                    shade->p_tiltTarget(endpoint);
                else {
                    shade->p_target(endpoint);
                    shade->p_tiltTarget(endpoint);
                }
            } else {
                // Down: move lift unless tiltonly; move tilt unless none
                if (shade->tiltType != tilt_types::tiltonly) shade->p_target(endpoint);
                if (shade->tiltType != tilt_types::none) shade->p_tiltTarget(endpoint);
            }
        }
        shade->lastFrame.processed = true;
    }
    shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
}

// stepDir: -1 = StepUp (decrease position), +1 = StepDown (increase position)
void SomfyCommandProcessor::processStepCommand(somfy_commands cmd, int8_t stepDir, bool internal, somfy_frame_t &frame)
{
    if (shade->lastFrame.processed) return;
    shade->lastFrame.processed = true;
    if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2) return;
    if (shade->getStepSize() == 0) return;
    if (shade->lastFrame.stepSize == 0) shade->lastFrame.stepSize = 1;

    const float steps = static_cast<float>(shade->getStepSize() * shade->lastFrame.stepSize);

    if (shade->tiltType == tilt_types::integrated) {
        const bool goingUp = stepDir < 0;
        const bool tiltAtEnd = goingUp ? (shade->currentTiltPos <= 0.0f) : (shade->currentTiltPos >= 100.0f);
        const bool liftAtEnd = goingUp ? (shade->currentPos <= 0.0f) : (shade->currentPos >= 100.0f);
        if (tiltAtEnd && liftAtEnd) return;
        if (!tiltAtEnd) {
            shade->p_target(shade->currentPos);
            if (shade->getTiltTime() == 0) return;
            float newTilt = shade->currentTiltPos +
                            static_cast<float>(stepDir) * (100.0f / (static_cast<float>(shade->getTiltTime()) / steps));
            shade->p_tiltTarget(goingUp ? max(0.0f, newTilt) : min(100.0f, newTilt));
        } else {
            shade->p_tiltTarget(shade->currentTiltPos);
            const float time =
                goingUp ? static_cast<float>(shade->getUpTime()) : static_cast<float>(shade->getDownTime());
            if (time == 0) return;
            float newPos = shade->currentPos + static_cast<float>(stepDir) * (100.0f / (time / steps));
            shade->p_target(goingUp ? max(0.0f, newPos) : min(100.0f, newPos));
        }
    } else if (shade->tiltType == tilt_types::tiltonly) {
        if (shade->getTiltTime() == 0) return;
        float newTilt = shade->currentTiltPos +
                        static_cast<float>(stepDir) * (100.0f / (static_cast<float>(shade->getTiltTime()) / steps));
        shade->p_tiltTarget(stepDir < 0 ? max(0.0f, newTilt) : min(100.0f, newTilt));
    } else {
        const bool canMove = stepDir < 0 ? (shade->currentPos > 0.0f) : (shade->currentPos < 100.0f);
        if (!canMove) return;
        const float time =
            stepDir < 0 ? static_cast<float>(shade->getUpTime()) : static_cast<float>(shade->getDownTime());
        if (time == 0) return;
        float newPos = shade->currentPos + static_cast<float>(stepDir) * (100.0f / (time / steps));
        shade->p_target(stepDir < 0 ? max(0.0f, newPos) : min(100.0f, newPos));
    }
    shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
}

void SomfyCommandProcessor::processFrame(somfy_frame_t &frame, bool internal)
{
    if (shade->getShadeId() == 255) return;
    bool hasRemote = shade->getRemoteAddress() == frame.remoteAddress;
    if (!hasRemote) {
        for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
            if (shade->getLinkedRemote(i).getRemoteAddress() == frame.remoteAddress) {
                if (frame.cmd != somfy_commands::Sensor) shade->getLinkedRemote(i).setRollingCode(frame.rollingCode);
                hasRemote = true;
                break;
            }
        }
    }
    if (!hasRemote) return;
    const uint64_t curTime = millis();
    shade->lastFrame.copy(frame);
    int8_t dir = 0;
    shade->resetMovement(curTime);
    if (!internal) shade->clearMotionState();
    somfy_commands cmd = shade->transformCommand(frame.cmd);
    switch (cmd) {
    case somfy_commands::Sensor:
        shade->lastFrame.processed = true;
        if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2) return;
        processSensorCommand(frame, curTime);
        break;
    case somfy_commands::Prog:
    case somfy_commands::MyUp:
    case somfy_commands::MyDown:
    case somfy_commands::MyUpDown:
    case somfy_commands::UpDown:
        shade->lastFrame.processed = true;
        if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2) return;
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        break;
    case somfy_commands::Flag:
        shade->lastFrame.processed = true;
        if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2) return;
        if (shade->lastFrame.rollingCode & 0x8000) return;
        processFlagCommand(internal, frame);
        break;
    case somfy_commands::SunFlag:
        if (shade->shadeType == shade_types::drycontact || shade->shadeType == shade_types::drycontact2) return;
        if (shade->lastFrame.rollingCode & 0x8000) return;
        processSunFlagCommand(internal, frame);
        break;
    case somfy_commands::Up:
        processUpDownCommand(cmd, -1, internal, frame, curTime);
        break;
    case somfy_commands::Down:
        processUpDownCommand(cmd, +1, internal, frame, curTime);
        break;
    case somfy_commands::My:
        if (shade->shadeType == shade_types::drycontact2) return;
        processMyCommand(internal, frame, curTime);
        break;
    case somfy_commands::StepUp:
        processStepCommand(cmd, -1, internal, frame);
        break;
    case somfy_commands::StepDown:
        processStepCommand(cmd, +1, internal, frame);
        break;
    case somfy_commands::Toggle:
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (!shade->isIdle())
            shade->p_target(shade->currentPos);
        else if (shade->currentPos == 100.0f)
            shade->p_target(0);
        else if (shade->currentPos == 0.0f)
            shade->p_target(100);
        else
            shade->p_target(shade->getLastMovement() == -1 ? 100 : 0);
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        break;
    case somfy_commands::Stop:
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        shade->p_target(shade->currentPos);
        shade->p_tiltTarget(shade->currentTiltPos);
        shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        break;
    case somfy_commands::Favorite:
        if (shade->lastFrame.processed) return;
        shade->lastFrame.processed = true;
        if (shade->simMy()) {
            shade->moveToMyPosition();
        } else {
            if (shade->getMyTiltPos() >= 0.0f && shade->getMyTiltPos() <= 100.0f)
                shade->p_tiltTarget(shade->getMyTiltPos());
            if (shade->getMyPos() >= 0.0f && shade->getMyPos() <= 100.0f && shade->tiltType != tilt_types::tiltonly)
                shade->p_target(shade->getMyPos());
            shade->emitCommand(cmd, internal ? "internal" : "remote", frame.remoteAddress);
        }
        break;
    default:
        dir = 0;
        break;
    }
    shade->setMovement(dir);
}

void SomfyCommandProcessor::processInternalCommand(somfy_commands cmd, uint8_t repeat)
{
    if (shade->getShadeId() == 255) return;
    const uint64_t curTime = millis();
    int8_t dir = 0;
    shade->resetMovement(curTime);
    switch (cmd) {
    case somfy_commands::Up:
        if (shade->tiltType == tilt_types::tiltmotor) {
            if (repeat >= TILT_REPEATS)
                shade->p_tiltTarget(0.0f);
            else
                shade->p_target(0.0f);
        } else if (shade->tiltType == tilt_types::tiltonly) {
            shade->p_target(100.0f);
            shade->p_currentPos(100.0f);
            shade->p_tiltTarget(0.0f);
        } else {
            shade->p_target(0.0f);
            shade->p_tiltTarget(0.0f);
        }
        break;
    case somfy_commands::Down:
        if (!shade->flagManager.windLast || (curTime - shade->flagManager.windLast) >= SOMFY_NO_WIND_REMOTE_TIMEOUT) {
            if (shade->tiltType == tilt_types::tiltmotor) {
                if (repeat >= TILT_REPEATS)
                    shade->p_tiltTarget(100.0f);
                else
                    shade->p_target(100.0f);
            } else if (shade->tiltType == tilt_types::tiltonly) {
                shade->p_target(100.0f);
                shade->p_currentPos(100.0f);
                shade->p_tiltTarget(100.0f);
            } else {
                shade->p_target(100.0f);
                if (shade->tiltType != tilt_types::none) shade->p_tiltTarget(100.0f);
            }
        }
        break;
    case somfy_commands::My:
        if (shade->isIdle()) {
            ESP_LOGI(s_TAG, "Shade #%d is idle", shade->getShadeId());
            if (shade->simMy()) {
                shade->moveToMyPosition();
            } else {
                if (shade->getMyTiltPos() >= 0.0f && shade->getMyTiltPos() <= 100.0f)
                    shade->p_tiltTarget(shade->getMyTiltPos());
                if (shade->getMyPos() >= 0.0f && shade->getMyPos() <= 100.0f && shade->tiltType != tilt_types::tiltonly)
                    shade->p_target(shade->getMyPos());
            }
        } else {
            if (shade->tiltType == tilt_types::tiltonly) {
                shade->p_target(100.0f);
            } else
                shade->p_target(shade->currentPos);
            shade->p_tiltTarget(shade->currentTiltPos);
        }
        break;
    case somfy_commands::StepUp:
        if (shade->getStepSize() == 0) return; // Avoid divide by 0.
        if (shade->tiltType == tilt_types::integrated) {
            if (shade->currentTiltPos <= 0.0f && shade->currentPos <= 0.0f)
                return;
            else if (shade->currentTiltPos > 0.0f) {
                shade->p_target(shade->currentPos);
                if (shade->getTiltTime() == 0) return;
                shade->p_tiltTarget(
                    max(0.0f, shade->currentTiltPos - (100.0f / (static_cast<float>(shade->getTiltTime()) /
                                                                 static_cast<float>(shade->getStepSize())))));
            } else {
                if (shade->getUpTime() == 0) return;
                shade->p_tiltTarget(shade->currentTiltPos);
                shade->p_target(max(0.0f, shade->currentPos - (100.0f / (static_cast<float>(shade->getUpTime()) /
                                                                         static_cast<float>(shade->getStepSize())))));
            }
        } else if (shade->tiltType == tilt_types::tiltonly) {
            if (shade->getTiltTime() == 0 || shade->currentTiltPos <= 0.0f) return;
            shade->p_tiltTarget(
                max(0.0f, shade->currentTiltPos - (100.0f / (static_cast<float>(shade->getTiltTime()) /
                                                             static_cast<float>(shade->getStepSize())))));
        } else if (shade->currentPos > 0.0f) {
            if (shade->getUpTime() == 0) return;
            shade->p_target(max(0.0f, shade->currentPos - (100.0f / (static_cast<float>(shade->getUpTime()) /
                                                                     static_cast<float>(shade->getStepSize())))));
        }
        break;
    case somfy_commands::StepDown:
        dir = 1;
        if (shade->getStepSize() == 0) return; // Avoid divide by 0.
        if (shade->tiltType == tilt_types::integrated) {
            if (shade->currentTiltPos >= 100.0f && shade->currentPos >= 100.0f)
                return;
            else if (shade->currentTiltPos < 100.0f) {
                shade->p_target(shade->currentPos);
                if (shade->getTiltTime() == 0) return;
                shade->p_tiltTarget(
                    min(100.0f, shade->currentTiltPos + (100.0f / (static_cast<float>(shade->getTiltTime()) /
                                                                   static_cast<float>(shade->getStepSize())))));
            } else {
                if (shade->getDownTime() == 0) return;
                shade->p_tiltTarget(shade->currentTiltPos);
                shade->p_target(min(100.0f, shade->currentPos + (100.0f / (static_cast<float>(shade->getDownTime()) /
                                                                           static_cast<float>(shade->getStepSize())))));
            }
        } else if (shade->tiltType == tilt_types::tiltonly) {
            if (shade->getTiltTime() == 0 || shade->getStepSize() == 0 || shade->currentTiltPos >= 100.0f) return;
            shade->p_tiltTarget(
                min(100.0f, shade->currentTiltPos + (100.0f / (static_cast<float>(shade->getTiltTime()) /
                                                               static_cast<float>(shade->getStepSize())))));
        } else if (shade->currentPos < 100.0f) {
            if (shade->getDownTime() == 0 || shade->getStepSize() == 0) return;
            shade->p_target(min(100.0f, shade->currentPos + (100.0f / (static_cast<float>(shade->getDownTime()) /
                                                                       static_cast<float>(shade->getStepSize())))));
        }
        break;
    case somfy_commands::Flag:
        shade->p_sunFlag(false);
        if (shade->hasSunSensor()) {
            somfy.store.markDirty();
            shade->emitState();
        } else {
            ESP_LOGI(s_TAG, "Shade does not have sensor %d", shade->flags);
        }
        break;
    case somfy_commands::SunFlag:
        if (shade->hasSunSensor()) {
            shade->p_sunFlag(true);
            if (!(shade->flags.isWindy())) {
                const bool isSunny = shade->flags.isSunny();
                if (isSunny && shade->flagManager.sunDone)
                    shade->p_target(shade->getMyPos() >= 0 ? shade->getMyPos() : 100.0f);
                else if (!isSunny && shade->flagManager.noSunDone)
                    shade->p_target(0.0f);
            }
            somfy.store.markDirty();
            shade->emitState();
        } else
            ESP_LOGI(s_TAG, "Shade does not have sensor %d", shade->flags);
        break;
    default:
        dir = 0;
        break;
    }
    shade->setMovement(dir);
}
