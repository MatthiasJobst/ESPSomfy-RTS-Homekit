#include "SomfyFlagManager.h"
#include "esp_log.h"

static const char *TAG = "SomfyFlagManager";

SomfyFlagManager::TimerTick SomfyFlagManager::tickTimers(uint8_t flags, uint64_t curTime, uint8_t shadeId) {
    TimerTick result;
    const bool isSunny = flags & static_cast<uint8_t>(somfy_flags_t::Sunny);
    const bool isWindy = flags & static_cast<uint8_t>(somfy_flags_t::Windy);
    const bool sunFlag = flags & static_cast<uint8_t>(somfy_flags_t::SunFlag);

    if (sunFlag) {
        if (isSunny && !isWindy) {
            if (this->noWindDone && !this->sunDone && this->sunStart
                    && (curTime - this->sunStart) >= SOMFY_SUN_TIMEOUT) {
                this->sunDone = true;
                result.setSunTarget = true;
                ESP_LOGI(TAG, "[%u] Sun -> done", shadeId);
            }
            if (!this->noWindDone && this->noWindStart
                    && (curTime - this->noWindStart) >= SOMFY_NO_WIND_TIMEOUT) {
                this->noWindDone = true;
                result.setSunTarget = true;
                ESP_LOGI(TAG, "[%u] No Wind -> done", shadeId);
            }
        }
        if (!isSunny && !this->noSunDone && this->noSunStart
                && (curTime - this->noSunStart) >= SOMFY_NO_SUN_TIMEOUT) {
            this->noSunDone = true;
            result.setNoSunTarget = true;
            ESP_LOGI(TAG, "[%u] No Sun -> done", shadeId);
        }
    }
    if (isWindy && !this->windDone && this->windStart
            && (curTime - this->windStart) >= SOMFY_WIND_TIMEOUT) {
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
