// compat/preferences.h — NVS-backed Preferences class replacing Arduino's Preferences.
// Drop-in replacement: all call sites use identical API.
#pragma once
#include <cstring>
#include <cstdint>
#include <WString.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *PREF_TAG = "Preferences";

// Mirrors the Arduino PreferenceType enum — only values used in this codebase.
enum class PreferenceType : uint8_t {
    PT_INVALID = 0,
    PT_I8,
    PT_U8,
    PT_I16,
    PT_U16,
    PT_I32,
    PT_U32,
    PT_I64,
    PT_U64,
    PT_STR,
    PT_BLOB,
};

class Preferences {
  private:
    nvs_handle_t _handle = 0;
    bool         _started = false;

  public:
    bool begin(const char* ns, bool readOnly = false) {
        nvs_open_mode_t mode = readOnly ? NVS_READONLY : NVS_READWRITE;
        esp_err_t err = nvs_open(ns, mode, &_handle);
        if(err != ESP_OK) {
            ESP_LOGE(PREF_TAG, "nvs_open(\"%s\") failed: %s", ns, esp_err_to_name(err));
            return false;
        }
        _started = true;
        return true;
    }

    void end() {
        if(_started) {
            nvs_close(_handle);
            _started = false;
        }
    }

    // --- bool ---
    bool putBool(const char* key, bool val) {
        return nvs_set_u8(_handle, key, val ? 1 : 0) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    bool getBool(const char* key, bool def = false) {
        uint8_t v = 0;
        return nvs_get_u8(_handle, key, &v) == ESP_OK ? (v != 0) : def;
    }

    // --- int8 / char ---
    bool putChar(const char* key, int8_t val) {
        return nvs_set_i8(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    int8_t getChar(const char* key, int8_t def = 0) {
        int8_t v = 0;
        return nvs_get_i8(_handle, key, &v) == ESP_OK ? v : def;
    }

    // --- uint8 ---
    bool putUChar(const char* key, uint8_t val) {
        return nvs_set_u8(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    uint8_t getUChar(const char* key, uint8_t def = 0) {
        uint8_t v = 0;
        return nvs_get_u8(_handle, key, &v) == ESP_OK ? v : def;
    }

    // --- int16 ---
    bool putShort(const char* key, int16_t val) {
        return nvs_set_i16(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    int16_t getShort(const char* key, int16_t def = 0) {
        int16_t v = 0;
        return nvs_get_i16(_handle, key, &v) == ESP_OK ? v : def;
    }

    // --- uint16 ---
    bool putUShort(const char* key, uint16_t val) {
        return nvs_set_u16(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    uint16_t getUShort(const char* key, uint16_t def = 0) {
        uint16_t v = 0;
        return nvs_get_u16(_handle, key, &v) == ESP_OK ? v : def;
    }

    // --- uint32 ---
    bool putUInt(const char* key, uint32_t val) {
        return nvs_set_u32(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    uint32_t getUInt(const char* key, uint32_t def = 0) {
        uint32_t v = 0;
        return nvs_get_u32(_handle, key, &v) == ESP_OK ? v : def;
    }

    // --- float (bit-cast via uint32) ---
    bool putFloat(const char* key, float val) {
        uint32_t bits;
        memcpy(&bits, &val, sizeof(bits));
        return nvs_set_u32(_handle, key, bits) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    float getFloat(const char* key, float def = 0.0f) {
        uint32_t bits = 0;
        if(nvs_get_u32(_handle, key, &bits) != ESP_OK) return def;
        float v;
        memcpy(&v, &bits, sizeof(v));
        return v;
    }

    // --- string ---
    bool putString(const char* key, const char* val) {
        return nvs_set_str(_handle, key, val) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    bool putString(const char* key, const String& val) {
        return putString(key, val.c_str());
    }
    size_t getString(const char* key, char* buf, size_t maxLen) {
        size_t len = maxLen;
        if(nvs_get_str(_handle, key, buf, &len) != ESP_OK) {
            if(maxLen > 0) buf[0] = '\0';
            return 0;
        }
        return len;
    }

    // --- bytes / blob ---
    bool putBytes(const char* key, const void* buf, size_t len) {
        return nvs_set_blob(_handle, key, buf, len) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        size_t len = maxLen;
        return nvs_get_blob(_handle, key, buf, &len) == ESP_OK ? len : 0;
    }
    size_t getBytesLength(const char* key) {
        size_t len = 0;
        nvs_get_blob(_handle, key, nullptr, &len);
        return len;
    }

    // --- key management ---
    bool isKey(const char* key) {
        nvs_type_t type;
        return nvs_find_key(_handle, key, &type) == ESP_OK;
    }
    bool remove(const char* key) {
        return nvs_erase_key(_handle, key) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    bool clear() {
        return nvs_erase_all(_handle) == ESP_OK && nvs_commit(_handle) == ESP_OK;
    }
    size_t freeEntries() {
        nvs_stats_t stats = {};
        nvs_get_stats(NULL, &stats);
        return stats.free_entries;
    }

    // --- type query (used for legacy migration in SomfyShade.cpp) ---
    PreferenceType getType(const char* key) {
        nvs_type_t type;
        if(nvs_find_key(_handle, key, &type) != ESP_OK) return PreferenceType::PT_INVALID;
        switch(type) {
            case NVS_TYPE_I8:   return PreferenceType::PT_I8;
            case NVS_TYPE_U8:   return PreferenceType::PT_U8;
            case NVS_TYPE_U16:  return PreferenceType::PT_U16;
            case NVS_TYPE_I16:  return PreferenceType::PT_I16;
            case NVS_TYPE_I32:  return PreferenceType::PT_I32;
            case NVS_TYPE_U32:  return PreferenceType::PT_U32;
            case NVS_TYPE_I64:  return PreferenceType::PT_I64;
            case NVS_TYPE_U64:  return PreferenceType::PT_U64;
            case NVS_TYPE_STR:  return PreferenceType::PT_STR;
            case NVS_TYPE_BLOB: return PreferenceType::PT_BLOB;
            default:            return PreferenceType::PT_INVALID;
        }
    }
};
