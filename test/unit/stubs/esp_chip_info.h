#pragma once
#include <cstdint>

typedef enum {
    CHIP_ESP32   = 1,
    CHIP_ESP32S2 = 2,
    CHIP_ESP32S3 = 9,
    CHIP_ESP32C3 = 5,
} esp_chip_model_t;

typedef struct {
    esp_chip_model_t model;
    uint32_t features;
    uint16_t revision;
    uint8_t  cores;
} esp_chip_info_t;

// Default to vanilla ESP32 so transceiver_config_t::load() uses plain GPIO defaults.
inline void esp_chip_info(esp_chip_info_t *out) {
    out->model    = CHIP_ESP32;
    out->features = 0;
    out->revision = 0;
    out->cores    = 2;
}
