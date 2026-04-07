#pragma once
#include <cstdint>
#include <unordered_map>

typedef int gpio_num_t;
typedef enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_INPUT_OUTPUT } gpio_mode_t;
typedef enum { GPIO_INTR_DISABLE, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE } gpio_int_type_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;

// Recorded by gpio_set_level so tests can assert pin states.
extern std::unordered_map<int, uint32_t> gpio_pin_levels;

inline int gpio_set_level(gpio_num_t pin, uint32_t level) {
    gpio_pin_levels[static_cast<int>(pin)] = level;
    return 0;
}
inline int gpio_get_level(gpio_num_t pin) {
    auto it = gpio_pin_levels.find(static_cast<int>(pin));
    return (it != gpio_pin_levels.end()) ? static_cast<int>(it->second) : 0;
}
inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }
inline int gpio_reset_pin(gpio_num_t) { return 0; }
inline int gpio_pullup_en(gpio_num_t) { return 0; }
inline int gpio_pulldown_en(gpio_num_t) { return 0; }

#define GPIO_NUM_NC  (-1)
