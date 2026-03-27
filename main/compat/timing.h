#pragma once
// ESP-IDF shim for Arduino timing primitives.
//
// Include this instead of Arduino.h when only timing functions are needed.
// When Arduino.h is still present in the translation unit (directly or via
// another header), the ARDUINO macro is defined and these definitions are
// skipped to avoid redefinition errors.
//
// Mapping:
//   millis()            -> esp_timer_get_time() / 1000   (ms since boot)
//   micros()            -> esp_timer_get_time()           (us since boot)
//   delay(ms)           -> vTaskDelay(pdMS_TO_TICKS(ms))
//   delayMicroseconds() -> esp_rom_delay_us()             (busy-wait)

#include <stdint.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

inline uint32_t micros() {
    return (uint32_t)esp_timer_get_time();
}

inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

inline void delayMicroseconds(uint32_t us) {
    esp_rom_delay_us(us);
}
