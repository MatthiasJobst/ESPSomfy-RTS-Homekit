#include "HomeKit.h"
#include "Somfy.h"
#include "ConfigSettings.h"
#include "WResp.h"

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>

static const char *TAG = "HomeKit";

// Default pairing setup code — override in your settings or via factory_nvs.
// Format: xxx-xx-xxx  (digits only, no letters).
#define HAP_SETUP_CODE  "459-23-871"
#define HAP_SETUP_ID    "SMFY"

extern ConfigSettings settings;
extern SomfyShadeController somfy;

HomeKitClass homekit;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// HAP position state values (HAP spec §9.87)
#define HAP_POS_STATE_DECREASING  0
#define HAP_POS_STATE_INCREASING  1
#define HAP_POS_STATE_STOPPED     2

static uint8_t directionToPositionState(int8_t direction) {
    if(direction > 0) return HAP_POS_STATE_INCREASING;
    if(direction < 0) return HAP_POS_STATE_DECREASING;
    return HAP_POS_STATE_STOPPED;
}

// Retrieve the Window Covering service from a bridged accessory.
static hap_serv_t *getWindowCoveringService(hap_acc_t *acc) {
    return hap_acc_get_serv_by_uuid(acc, HAP_SERV_UUID_WINDOW_COVERING);
}

// ---------------------------------------------------------------------------
// Identify callbacks
// ---------------------------------------------------------------------------

static int __attribute__((used)) bridge_identify(hap_acc_t *ha) {
    ESP_LOGI(TAG, "Bridge identified");
    return HAP_SUCCESS;
}

static int __attribute__((used)) shade_identify(hap_acc_t *ha) {
    hap_serv_t *hs = hap_acc_get_serv_by_uuid(ha, HAP_SERV_UUID_ACCESSORY_INFORMATION);
    hap_char_t *hc = hap_serv_get_char_by_uuid(hs, HAP_CHAR_UUID_NAME);
    const hap_val_t *val = hap_char_get_val(hc);
    ESP_LOGI(TAG, "Shade '%s' identified", val ? val->s : "?");
    return HAP_SUCCESS;
}

// ---------------------------------------------------------------------------
// Write callback — handles TargetPosition and HoldPosition writes from HomeKit
// ---------------------------------------------------------------------------

static int shade_write(hap_write_data_t write_data[], int count,
                       void *serv_priv, void *write_priv) {
    // serv_priv holds the shadeId stored as (void*)(uintptr_t)shadeId
    uint8_t shadeId = (uint8_t)(uintptr_t)serv_priv;
    SomfyShade *shade = somfy.getShadeById(shadeId);

    if(!shade) {
        ESP_LOGW(TAG, "Write for unknown shadeId %d", shadeId);
        for(int i = 0; i < count; i++) *(write_data[i].status) = HAP_STATUS_RES_ABSENT;
        return HAP_FAIL;
    }

    for(int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];
        const char *uuid = hap_char_get_type_uuid(w->hc);

        if(!strcmp(uuid, HAP_CHAR_UUID_TARGET_POSITION)) {
            // HomeKit: 0=fully closed, 100=fully open — same convention as
            // SomfyShade (with flipPosition already applied by transformPosition).
            // We need to undo transformPosition to get the internal 0-100 float.
            float target;
            if(shade->flipPosition)
                target = 100.0f - (float)w->val.u;
            else
                target = (float)w->val.u;
            ESP_LOGI(TAG, "TargetPosition write: hap=%u -> somfy=%.0f (currentPos=%.0f flip=%d)",
                     w->val.u, target, shade->currentPos, shade->flipPosition);
            shade->moveToTarget(target);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;

        } else if(!strcmp(uuid, HAP_CHAR_UUID_HOLD_POSITION)) {
            if(w->val.b) shade->sendCommand(somfy_commands::Stop);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;

        } else {
            *(w->status) = HAP_STATUS_RES_ABSENT;
        }
    }
    return HAP_SUCCESS;
}

// ---------------------------------------------------------------------------
// Create one bridged accessory for a shade
// ---------------------------------------------------------------------------

static hap_acc_t *createShadeAccessory(SomfyShade *shade) {
    int8_t pos = shade->transformPosition(shade->currentPos);
    int8_t tgt = shade->transformPosition(shade->target);
    uint8_t posState = directionToPositionState(shade->direction);

    // Use shade name as serial number (unique within this device)
    char serial[24];
    snprintf(serial, sizeof(serial), "SMF-%03d", shade->getShadeId());

    hap_acc_cfg_t cfg = {
        .name          = shade->name,
        .model         = (char *)"SomfyRTS",
        .manufacturer  = (char *)"ESPSomfy",
        .serial_num    = serial,
        .fw_rev        = settings.fwVersion.name,
        .hw_rev        = NULL,
        .pv            = (char *)"1.1.0",
        .cid           = HAP_CID_BRIDGE,
        .identify_routine = shade_identify,
    };
    hap_acc_t *acc = hap_acc_create(&cfg);
    if(!acc) return NULL;

    // Window Covering service: targ_pos, curr_pos, pos_state (all 0–100 / enum)
    uint8_t currPos = (pos >= 0) ? (uint8_t)pos : 0;
    uint8_t targPos = (tgt >= 0) ? (uint8_t)tgt : 0;
    hap_serv_t *svc = hap_serv_window_covering_create(targPos, currPos, posState);

    // Add optional Name characteristic (visible in Apple Home)
    hap_serv_add_char(svc, hap_char_name_create(shade->name));

    // Add HoldPosition (Stop command)
    hap_serv_add_char(svc, hap_char_hold_position_create(false));

    // Store shadeId as private data so the write callback can find the shade
    hap_serv_set_priv(svc, (void *)(uintptr_t)shade->getShadeId());
    hap_serv_set_write_cb(svc, shade_write);

    hap_acc_add_serv(acc, svc);
    return acc;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void HomeKitClass::begin() {
    if(_started) return;

    ESP_LOGI(TAG, "Initialising HomeKit bridge");
    hap_init(HAP_TRANSPORT_WIFI);

    // --- Primary bridge accessory ---
    hap_acc_cfg_t bridge_cfg = {
        .name          = settings.hostname[0] ? settings.hostname : (char *)"ESPSomfyRTS",
        .model         = (char *)"SomfyRTS-Bridge",
        .manufacturer  = (char *)"ESPSomfy",
        .serial_num    = (char *)settings.serverId,
        .fw_rev        = settings.fwVersion.name,
        .hw_rev        = NULL,
        .pv            = (char *)"1.1.0",
        .cid           = HAP_CID_BRIDGE,
        .identify_routine = bridge_identify,
    };
    hap_acc_t *bridge = hap_acc_create(&bridge_cfg);
    hap_acc_add_wifi_transport_service(bridge, 0);
    hap_add_accessory(bridge);

    // --- One bridged accessory per configured shade ---
    for(uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &somfy.shades[i];
        if(shade->getShadeId() == 255 || shade->name[0] == '\0') continue;
        hap_acc_t *acc = createShadeAccessory(shade);
        if(acc) {
            hap_add_bridged_accessory(acc, hap_get_unique_aid(shade->name));
            ESP_LOGI(TAG, "Added shade '%s' (id=%d)", shade->name, shade->getShadeId());
        }
    }

    // Pairing code — for production, burn via factory_nvs_gen instead.
    hap_set_setup_code(HAP_SETUP_CODE);
    hap_set_setup_id(HAP_SETUP_ID);

    char *payload = esp_hap_get_setup_payload(
        (char *)HAP_SETUP_CODE, (char *)HAP_SETUP_ID, false, HAP_CID_BRIDGE);
    if(payload) {
        ESP_LOGI(TAG, "Pair with HomeKit using setup code: %s", HAP_SETUP_CODE);
        ESP_LOGI(TAG, "Or scan QR payload: %s", payload);
        strncpy(_qrPayload, payload, sizeof(_qrPayload) - 1);
        free(payload);
    }

    hap_start();
    _started = true;
    ESP_LOGI(TAG, "HomeKit bridge started");
}

void HomeKitClass::resetPairings() {
    if(!_started) return;
    hap_reset_pairings();
}

void HomeKitClass::toJSON(JsonResponse &resp) {
    resp.addElem("started", _started);
    resp.addElem("setupCode", (const char *)HAP_SETUP_CODE);
    resp.addElem("qrPayload", (const char *)_qrPayload);
    resp.addElem("pairedCount", (int8_t)(_started ? hap_get_paired_controller_count() : 0));
}

void HomeKitClass::notifyShadeState(SomfyShade *shade) {
    if(!_started) return;

    // Walk bridged accessories to find the one matching this shade.
    hap_acc_t *acc = hap_get_first_acc();
    while(acc) {
        hap_serv_t *svc = getWindowCoveringService(acc);
        if(svc) {
            uint8_t id = (uint8_t)(uintptr_t)hap_serv_get_priv(svc);
            if(id == shade->getShadeId()) {
                int8_t pos = shade->transformPosition(shade->currentPos);
                int8_t tgt = shade->transformPosition(shade->target);
                uint8_t ps  = directionToPositionState(shade->direction);

                hap_val_t val;

                val.u = (pos >= 0) ? (uint8_t)pos : 0;
                hap_char_update_val(
                    hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_CURRENT_POSITION), &val);

                val.u = (tgt >= 0) ? (uint8_t)tgt : 0;
                hap_char_update_val(
                    hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_TARGET_POSITION), &val);

                val.u = ps;
                hap_char_update_val(
                    hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_POSITION_STATE), &val);
                return;
            }
        }
        acc = hap_acc_get_next(acc);
    }
}

void HomeKitClass::addShade(SomfyShade *shade) {
    if(!_started || !shade || shade->getShadeId() == 255 || shade->name[0] == '\0') return;
    hap_acc_t *acc = createShadeAccessory(shade);
    if(acc) {
        hap_add_bridged_accessory(acc, hap_get_unique_aid(shade->name));
        hap_update_config_number();
        ESP_LOGI(TAG, "Dynamically added shade '%s'", shade->name);
    }
}

void HomeKitClass::removeShade(SomfyShade *shade) {
    if(!_started || !shade) return;
    hap_acc_t *acc = hap_get_first_acc();
    while(acc) {
        hap_serv_t *svc = getWindowCoveringService(acc);
        if(svc) {
            uint8_t id = (uint8_t)(uintptr_t)hap_serv_get_priv(svc);
            if(id == shade->getShadeId()) {
                hap_remove_bridged_accessory(acc);
                hap_update_config_number();
                ESP_LOGI(TAG, "Removed shade id=%d from HomeKit", shade->getShadeId());
                return;
            }
        }
        acc = hap_acc_get_next(acc);
    }
}
