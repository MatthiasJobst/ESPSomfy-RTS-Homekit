#include "SomfyFlagManager.h"

#include <esp_log.h>

static const char *s_TAG = "SomfyFlagManager";

static void logTimerDone(uint8_t shadeId, const char *label)
{
    ESP_LOGI(s_TAG, "[%u] %s -> done", shadeId, label);
}

static void logTimerStart(uint8_t shadeId, const char *label)
{
    ESP_LOGI(s_TAG, "[%u] %s -> start", shadeId, label);
}

bool SomfyFlagManager::isSunDone(uint64_t curTime) const
{
    return this->sunDone || (this->sunStart > 0 && (curTime - this->sunStart) >= SOMFY_SUN_TIMEOUT);
}

bool SomfyFlagManager::isNoWindDone(uint64_t curTime) const
{
    return !this->noWindDone && (this->noWindStart > 0) && (curTime - this->noWindStart) >= SOMFY_NO_WIND_TIMEOUT;
}

bool SomfyFlagManager::isNoSunDone(uint64_t curTime) const
{
    return !this->noSunDone && (this->noSunStart > 0) && (curTime - this->noSunStart) >= SOMFY_NO_SUN_TIMEOUT;
}

bool SomfyFlagManager::isWindDone(uint64_t curTime) const
{
    return this->windDone || (this->windStart > 0 && (curTime - this->windStart) >= SOMFY_WIND_TIMEOUT);
}

void SomfyFlagManager::tickSunTimers(SomfyFlag flags, uint64_t curTime, uint8_t shadeId, TimerTick &result)
{
    if (flags.isSunny() && !flags.isWindy()) {
        if (isSunDone(curTime)) {
            this->sunDone = true;
            result.setSunTarget = true;
            logTimerDone(shadeId, "Sun");
        }
        if (isNoWindDone(curTime)) {
            this->noWindDone = true;
            result.setSunTarget = true;
            logTimerDone(shadeId, "No Wind");
        }
    }
    if (!flags.isSunny() && isNoSunDone(curTime)) {
        this->noSunDone = true;
        result.setNoSunTarget = true;
        logTimerDone(shadeId, "No Sun");
    }
}

void SomfyFlagManager::tickWindTimer(uint64_t curTime, uint8_t shadeId, TimerTick &result)
{
    if (isWindDone(curTime)) {
        this->windDone = true;
        result.setWindTarget = true;
        logTimerDone(shadeId, "Wind");
    }
}

SomfyFlagManager::TimerTick SomfyFlagManager::tickTimers(SomfyFlag flags, uint64_t curTime, uint8_t shadeId)
{
    TimerTick result;
    if (flags.hasSunFlag()) {
        tickSunTimers(flags, curTime, shadeId, result);
    }
    if (flags.isWindy()) {
        tickWindTimer(curTime, shadeId, result);
    }
    return result;
}

void SomfyFlagManager::updateTimers(bool wasSunny, bool wasWindy, bool isSunny, bool isWindy, uint64_t curTime,
                                    uint8_t shadeId)
{
    if (isSunny) {
        this->noSunStart = 0;
        this->noSunDone = true;
    } else {
        this->sunStart = 0;
        this->sunDone = true;
    }
    if (isWindy) {
        this->noWindStart = 0;
        this->noWindDone = true;
        this->windLast = curTime;
    } else {
        this->windStart = 0;
        this->windDone = true;
    }

    if (isSunny && !wasSunny) {
        this->sunStart = curTime;
        this->sunDone = false;
        logTimerStart(shadeId, "Sun");
    } else if (!isSunny && wasSunny) {
        this->noSunStart = curTime;
        this->noSunDone = false;
        logTimerStart(shadeId, "No Sun");
    }
    if (isWindy && !wasWindy) {
        this->windStart = curTime;
        this->windDone = false;
        logTimerStart(shadeId, "Wind");
    } else if (!isWindy && wasWindy) {
        this->noWindStart = curTime;
        this->noWindDone = false;
        logTimerStart(shadeId, "No Wind");
    }
}
