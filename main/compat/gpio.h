#pragma once
// ESP-IDF shim for Arduino GPIO primitives.
//
// Include this instead of Arduino.h when only GPIO functions are needed.
// When Arduino.h is still present in the translation unit (directly or via
// another header), the ARDUINO macro is defined and these definitions are
// skipped to avoid redefinition errors.
//
// Mapping:
//   pinMode(pin, OUTPUT)    -> gpio_set_direction(pin, GPIO_MODE_OUTPUT)
//   pinMode(pin, INPUT)     -> gpio_set_direction(pin, GPIO_MODE_INPUT)
//   digitalWrite(pin, val)  -> gpio_set_level(pin, val)
//   digitalRead(pin)        -> gpio_get_level(pin)

#ifndef ARDUINO
#include <stdint.h>
#include "driver/gpio.h"

#define INPUT  0
#define OUTPUT 1
#define LOW    0
#define HIGH   1

inline void pinMode(uint8_t pin, uint8_t mode) {
    gpio_set_direction((gpio_num_t)pin,
        mode == OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
}
inline void digitalWrite(uint8_t pin, uint8_t val) {
    gpio_set_level((gpio_num_t)pin, val);
}
inline int digitalRead(uint8_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}
#endif
