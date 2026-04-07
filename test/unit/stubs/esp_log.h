#pragma once
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) do {} while(0)
#define ESP_LOGD(tag, fmt, ...) do {} while(0)
#define ESP_LOGE(tag, fmt, ...) do { printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do {} while(0)
#define ESP_LOGV(tag, fmt, ...) do {} while(0)
