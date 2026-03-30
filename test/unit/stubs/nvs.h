#pragma once
#include "nvs_flash.h"
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <string>
#include <vector>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
typedef enum {
    NVS_TYPE_U8   = 0x01, NVS_TYPE_I8  = 0x11,
    NVS_TYPE_U16  = 0x02, NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32  = 0x04, NVS_TYPE_I32 = 0x14,
    NVS_TYPE_U64  = 0x08, NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR  = 0x21, NVS_TYPE_BLOB = 0x42,
    NVS_TYPE_ANY  = 0xff
} nvs_type_t;
struct nvs_stats_t { size_t free_entries = 100; size_t total_entries = 100; size_t used_entries = 0; size_t namespace_count = 0; };

struct NvsEntry { nvs_type_t type; std::vector<uint8_t> data; };
struct NvsStore  { std::unordered_map<std::string, NvsEntry> entries; };

extern std::unordered_map<uint32_t, NvsStore> nvs_stub_stores;
extern uint32_t nvs_stub_next_handle;

inline esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *h) {
    *h = nvs_stub_next_handle++;
    nvs_stub_stores[*h] = NvsStore{};
    return ESP_OK;
}
inline void      nvs_close(nvs_handle_t h)              { nvs_stub_stores.erase(h); }
inline esp_err_t nvs_commit(nvs_handle_t)               { return ESP_OK; }
inline esp_err_t nvs_erase_all(nvs_handle_t h)          { nvs_stub_stores[h].entries.clear(); return ESP_OK; }
inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) { nvs_stub_stores[h].entries.erase(k); return ESP_OK; }
inline esp_err_t nvs_get_stats(const char *, nvs_stats_t *s)  { if (s) *s = nvs_stats_t{}; return ESP_OK; }
inline esp_err_t nvs_find_key(nvs_handle_t h, const char *k, nvs_type_t *t) {
    auto it = nvs_stub_stores[h].entries.find(k);
    if (it == nvs_stub_stores[h].entries.end()) return ESP_FAIL;
    if (t) *t = it->second.type;
    return ESP_OK;
}

#define NVS_DEFINE_SET(fn, ctype, nvs_type) \
    inline esp_err_t fn(nvs_handle_t h, const char *k, ctype v) { \
        auto &e = nvs_stub_stores[h].entries[k]; \
        e.type = nvs_type; e.data.resize(sizeof(ctype)); memcpy(e.data.data(), &v, sizeof(ctype)); return ESP_OK; }

#define NVS_DEFINE_GET(fn, ctype) \
    inline esp_err_t fn(nvs_handle_t h, const char *k, ctype *v) { \
        auto it = nvs_stub_stores[h].entries.find(k); \
        if (it == nvs_stub_stores[h].entries.end()) return ESP_FAIL; \
        memcpy(v, it->second.data.data(), sizeof(ctype)); return ESP_OK; }

NVS_DEFINE_SET(nvs_set_u8,  uint8_t,  NVS_TYPE_U8)
NVS_DEFINE_GET(nvs_get_u8,  uint8_t)
NVS_DEFINE_SET(nvs_set_i8,  int8_t,   NVS_TYPE_I8)
NVS_DEFINE_GET(nvs_get_i8,  int8_t)
NVS_DEFINE_SET(nvs_set_u16, uint16_t, NVS_TYPE_U16)
NVS_DEFINE_GET(nvs_get_u16, uint16_t)
NVS_DEFINE_SET(nvs_set_i16, int16_t,  NVS_TYPE_I16)
NVS_DEFINE_GET(nvs_get_i16, int16_t)
NVS_DEFINE_SET(nvs_set_u32, uint32_t, NVS_TYPE_U32)
NVS_DEFINE_GET(nvs_get_u32, uint32_t)
NVS_DEFINE_SET(nvs_set_i32, int32_t,  NVS_TYPE_I32)
NVS_DEFINE_GET(nvs_get_i32, int32_t)

inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) {
    auto &e = nvs_stub_stores[h].entries[k];
    e.type = NVS_TYPE_STR; size_t len = strlen(v) + 1;
    e.data.resize(len); memcpy(e.data.data(), v, len); return ESP_OK;
}
inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *v, size_t *len) {
    auto it = nvs_stub_stores[h].entries.find(k);
    if (it == nvs_stub_stores[h].entries.end()) return ESP_FAIL;
    if (v) memcpy(v, it->second.data.data(), std::min(*len, it->second.data.size()));
    *len = it->second.data.size(); return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t l) {
    auto &e = nvs_stub_stores[h].entries[k];
    e.type = NVS_TYPE_BLOB; e.data.resize(l); memcpy(e.data.data(), v, l); return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *len) {
    auto it = nvs_stub_stores[h].entries.find(k);
    if (it == nvs_stub_stores[h].entries.end()) return ESP_FAIL;
    if (v) memcpy(v, it->second.data.data(), std::min(*len, it->second.data.size()));
    *len = it->second.data.size(); return ESP_OK;
}
