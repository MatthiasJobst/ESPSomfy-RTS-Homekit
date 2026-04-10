#include "SomfyFlagManager.h"
#include "esp_log.h"

static const char *TAG = "SomfyFlagManager";

bool SomfyFlagManager::isSunDone(uint64_t curTime) {
    return this->sunDone || (this->sunStart > 0 && (curTime - this->sunStart) >= SOMFY_SUN_TIMEOUT);
}
bool SomfyFlagManager::isNoWindDone(uint64_t curTime) {
    return !this->noWindDone && (this->noWindStart > 0)
                    && (curTime - this->noWindStart) >= SOMFY_NO_WIND_TIMEOUT;
}
bool SomfyFlagManager::isNoSunDone(uint64_t curTime) {
    return !this->noSunDone && (this->noSunStart > 0)
                && (curTime - this->noSunStart) >= SOMFY_NO_SUN_TIMEOUT;
}
bool SomfyFlagManager::isWindDone(uint64_t curTime) {
    return this->windDone || (this->windStart > 0 && (curTime - this->windStart) >= SOMFY_WIND_TIMEOUT);
}

SomfyFlagManager::TimerTick SomfyFlagManager::tickTimers(SomfyFlag flags, uint64_t curTime, uint8_t shadeId) {
    TimerTick result;

    if (flags.hasSunFlag()) {
        if (flags.isSunny() && !flags.isWindy()) {
            if (isSunDone(curTime)) {
                this->sunDone = true;
                result.setSunTarget = true;
                ESP_LOGI(TAG, "[%u] Sun -> done", shadeId);
            }
            if (isNoWindDone(curTime)) {
                this->noWindDone = true;
                result.setSunTarget = true;
                ESP_LOGI(TAG, "[%u] No Wind -> done", shadeId);
            }
        }
        if (!flags.isSunny() && isNoSunDone(curTime)) {
            this->noSunDone = true;
            result.setNoSunTarget = true;
            ESP_LOGI(TAG, "[%u] No Sun -> done", shadeId);
        }
    }
    if (flags.isWindy()  && isWindDone(curTime)) {
        this->windDone = true;
        result.setWindTarget = true;
        ESP_LOGI(TAG, "[%u] Wind -> done", shadeId);
    }
    return result;
}

void SomfyFlagManager::updateTimers(bool wasSunny, bool wasWindy,
                                     bool isSunny,  bool isWindy,
                                     uint64_t curTime, uint8_t shadeId) {
    if(isSunny) { this->noSunStart = 0; this->noSunDone = true; }
    else         { this->sunStart   = 0; this->sunDone   = true; }
    if(isWindy)  { this->noWindStart = 0; this->noWindDone = true; this->windLast = curTime; }
    else         { this->windStart   = 0; this->windDone   = true; }

    if(isSunny && !wasSunny)        { this->sunStart   = curTime; this->sunDone   = false; ESP_LOGI(TAG, "[%u] Sun -> start",    shadeId); }
    else if(!isSunny && wasSunny)   { this->noSunStart = curTime; this->noSunDone = false; ESP_LOGI(TAG, "[%u] No Sun -> start", shadeId); }
    if(isWindy && !wasWindy)        { this->windStart   = curTime; this->windDone   = false; ESP_LOGI(TAG, "[%u] Wind -> start",    shadeId); }
    else if(!isWindy && wasWindy)   { this->noWindStart = curTime; this->noWindDone = false; ESP_LOGI(TAG, "[%u] No Wind -> start", shadeId); }
}
