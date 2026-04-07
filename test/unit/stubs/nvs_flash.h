#pragma once
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK    0
#define ESP_FAIL  -1

inline const char *esp_err_to_name(esp_err_t e) { return e == 0 ? "ESP_OK" : "ESP_FAIL"; }
inline esp_err_t nvs_flash_init() { return ESP_OK; }
inline esp_err_t nvs_flash_erase() { return ESP_OK; }
