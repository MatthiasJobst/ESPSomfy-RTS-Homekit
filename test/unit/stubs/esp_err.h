#pragma once
#include <cstdint>
#include <cstddef>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)

#define IRAM_ATTR
#define DRAM_ATTR

inline const char *esp_err_to_name(esp_err_t)
{
    return "ESP_ERR_STUB";
}
#define ESP_ERROR_CHECK(x) (void)(x)

// __containerof is a GCC/clang extension available on host; define a portable fallback.
#ifndef __containerof
#define __containerof(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

// ESP32 hardware register write macros — no-op on host.
#define REG_WRITE(reg, val)                                                                                            \
    do {                                                                                                               \
        (void)(reg);                                                                                                   \
        (void)(val);                                                                                                   \
    } while (0)
#define GPIO_OUT_W1TS_REG 0
#define GPIO_OUT_W1TC_REG 0
