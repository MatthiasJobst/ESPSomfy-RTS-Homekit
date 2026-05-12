#include "HomeKit.h"

#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_mac.h>
#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
#include "ConfigSettings.h"
#include "SomfyShadeController.h"
#include "WResp.h"

static const char *s_TAG = "HomeKit";

#define HAP_SETUP_ID "SMFY"

extern ConfigSettings settings;
extern SomfyShadeController somfy;

// HAP position state values (HAP spec §9.87)
#define HAP_POS_STATE_DECREASING 0
#define HAP_POS_STATE_INCREASING 1
#define HAP_POS_STATE_STOPPED 2

/**
 * @brief Convert a shade movement direction to a HAP position state value.
 *
 * @param direction Positive for opening, negative for closing, zero for stopped.
 * @return HAP_POS_STATE_INCREASING, HAP_POS_STATE_DECREASING, or HAP_POS_STATE_STOPPED.
 */
static uint8_t directionToPositionState(int8_t direction)
{
    if (direction > 0) return HAP_POS_STATE_INCREASING;
    if (direction < 0) return HAP_POS_STATE_DECREASING;
    return HAP_POS_STATE_STOPPED;
}

/**
 * @brief Retrieve the Window Covering service from a bridged accessory.
 *
 * @param acc HAP accessory to search.
 * @return Pointer to the service, or NULL if not found.
 */
static hap_serv_t *getWindowCoveringService(hap_acc_t *acc)
{
    return hap_acc_get_serv_by_uuid(acc, HAP_SERV_UUID_WINDOW_COVERING);
}

/**
 * @brief HAP identify callback for the bridge accessory.
 *
 * @param ha HAP accessory handle (unused).
 * @return HAP_SUCCESS.
 */
static int __attribute__((used)) bridge_identify(hap_acc_t *ha)
{
    ESP_LOGI(s_TAG, "Bridge identified");
    return HAP_SUCCESS;
}

/**
 * @brief HAP identify callback for a bridged shade accessory.
 *
 * Logs the shade's display name.
 *
 * @param ha HAP accessory handle.
 * @return HAP_SUCCESS.
 */
static int __attribute__((used)) shade_identify(hap_acc_t *ha)
{
    hap_serv_t *hs = hap_acc_get_serv_by_uuid(ha, HAP_SERV_UUID_ACCESSORY_INFORMATION);
    hap_char_t *hc = hap_serv_get_char_by_uuid(hs, HAP_CHAR_UUID_NAME);
    const hap_val_t *val = hap_char_get_val(hc);
    ESP_LOGI(s_TAG, "Shade '%s' identified", val ? val->s : "?");
    return HAP_SUCCESS;
}

/**
 * @brief HAP write callback — handles TargetPosition and HoldPosition writes from HomeKit.
 *
 * @param write_data Array of write requests from the HAP stack.
 * @param count      Number of entries in @p write_data.
 * @param serv_priv  Service private data; points to the SomfyShade for this accessory.
 * @param write_priv Write private data (unused).
 * @return HAP_SUCCESS on success, HAP_FAIL if @p serv_priv is NULL.
 */
static int shade_write(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    SomfyShade *shade = (SomfyShade *)serv_priv;

    if (!shade) {
        ESP_LOGW(s_TAG, "Write with null shade pointer");
        for (int i = 0; i < count; i++)
            *(write_data[i].status) = HAP_STATUS_RES_ABSENT;
        return HAP_FAIL;
    }
    uint8_t shadeId = shade->getShadeId();

    for (int i = 0; i < count; i++) {
        hap_write_data_t *w = &write_data[i];
        const char *uuid = hap_char_get_type_uuid(w->hc);

        if (!strcmp(uuid, HAP_CHAR_UUID_TARGET_POSITION)) {
            // HomeKit: 0=fully closed, 100=fully open — same convention as
            // SomfyShade (with flipPosition already applied by transformPosition).
            // We need to undo transformPosition to get the internal 0-100 float.
            float target;
            if (shade->getFlipPosition())
                target = 100.0f - (float)w->val.u;
            else
                target = (float)w->val.u;
            ESP_LOGI(s_TAG, "TargetPosition write: hap=%u -> somfy=%.0f (currentPos=%.0f flip=%d)", w->val.u, target,
                     shade->currentPos, shade->getFlipPosition());
            somfy.enqueueShadeTargetForced(shadeId, target);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;

        } else if (!strcmp(uuid, HAP_CHAR_UUID_HOLD_POSITION)) {
            // Match the boosted repeat count used for HomeKit Up/Down so a
            // single HomeKit tap is delivered reliably with no app retry.
            if (w->val.b) somfy.enqueueShadeCommand(shadeId, somfy_commands::Stop, MOVE_REPEATS);
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;

        } else {
            *(w->status) = HAP_STATUS_RES_ABSENT;
        }
    }
    return HAP_SUCCESS;
}

/**
 * @brief Create and configure a bridged HAP accessory for a single shade.
 *
 * Allocates the HAP accessory with a Window Covering service, Name characteristic,
 * and HoldPosition characteristic. The shade pointer is stored as service private
 * data so write callbacks can reach it directly.
 *
 * @note Pointers into somfy.shades[] are stable for the device lifetime, so
 *       storing the raw pointer as private data is safe.
 *
 * @param shade Shade to represent. Must not be NULL.
 * @return Newly allocated HAP accessory, or NULL on allocation failure.
 */
static hap_acc_t *createShadeAccessory(SomfyShade *shade)
{
    int8_t pos = shade->transformPosition(shade->currentPos);
    int8_t tgt = shade->transformPosition(shade->target);
    uint8_t posState = directionToPositionState(shade->direction);

    char serial[24];
    snprintf(serial, sizeof(serial), "SMF-%03d", shade->getShadeId());

    hap_acc_cfg_t cfg = {
        .name = shade->name,
        .model = (char *)"SomfyRTS",
        .manufacturer = (char *)"ESPSomfy",
        .serial_num = serial,
        .fw_rev = settings.fwVersion.name,
        .hw_rev = NULL,
        .pv = (char *)"1.1.0",
        .cid = HAP_CID_BRIDGE,
        .identify_routine = shade_identify,
    };
    hap_acc_t *acc = hap_acc_create(&cfg);
    if (!acc) return NULL;

    uint8_t currPos = (pos >= 0) ? (uint8_t)pos : 0;
    uint8_t targPos = (tgt >= 0) ? (uint8_t)tgt : 0;
    hap_serv_t *svc = hap_serv_window_covering_create(targPos, currPos, posState);

    hap_serv_add_char(svc, hap_char_name_create(shade->name));
    hap_serv_add_char(svc, hap_char_hold_position_create(false));

    hap_serv_set_priv(svc, shade);
    hap_serv_set_write_cb(svc, shade_write);

    hap_acc_add_serv(acc, svc);
    return acc;
}

const char *HomeKitClass::prefab()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t n = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    snprintf(_setupCode, sizeof(_setupCode), "%03u-%02u-%03u", (unsigned)(n / 100000u), (unsigned)((n / 1000u) % 100u),
             (unsigned)(n % 1000u));
    ESP_LOGI(s_TAG, "HomeKit setup code (generated): %s", _setupCode);
    return _setupCode;
}

void HomeKitClass::setCode(const char *code)
{
    strncpy(_setupCode, code, sizeof(_setupCode) - 1);
    _setupCode[sizeof(_setupCode) - 1] = '\0';
}

void HomeKitClass::begin()
{
    if (_started) return;

    ESP_LOGI(s_TAG, "Initialising HomeKit bridge");
    hap_init(HAP_TRANSPORT_WIFI);

    hap_acc_cfg_t bridge_cfg = {
        .name = settings.hostname[0] ? settings.hostname : (char *)"ESPSomfyRTS",
        .model = (char *)"SomfyRTS-Bridge",
        .manufacturer = (char *)"ESPSomfy",
        .serial_num = (char *)settings.serverId,
        .fw_rev = settings.fwVersion.name,
        .hw_rev = NULL,
        .pv = (char *)"1.1.0",
        .cid = HAP_CID_BRIDGE,
        .identify_routine = bridge_identify,
    };
    hap_acc_t *bridge = hap_acc_create(&bridge_cfg);
    hap_acc_add_wifi_transport_service(bridge, 0);
    hap_add_accessory(bridge);

    for (uint8_t i = 0; i < SOMFY_MAX_SHADES; i++) {
        SomfyShade *shade = &somfy.shades[i];
        if (shade->getShadeId() == 255 || shade->name[0] == '\0') continue;
        hap_acc_t *acc = createShadeAccessory(shade);
        if (acc) {
            hap_add_bridged_accessory(acc, hap_get_unique_aid(shade->name));
            ESP_LOGI(s_TAG, "Added shade '%s' (id=%d)", shade->name, shade->getShadeId());
        }
    }

    hap_set_setup_code(_setupCode);
    hap_set_setup_id(HAP_SETUP_ID);

    char *payload = esp_hap_get_setup_payload(_setupCode, (char *)HAP_SETUP_ID, false, HAP_CID_BRIDGE);
    if (payload) {
        ESP_LOGI(s_TAG, "Pair with HomeKit using setup code: %s", _setupCode);
        ESP_LOGI(s_TAG, "Or scan QR payload: %s", payload);
        strncpy(_qrPayload, payload, sizeof(_qrPayload) - 1);
        free(payload);
    }

    // Lower hap-loop task priority below our main loop (10) so HAP processing
    // never starves sockServer.loop() / web server polling on the same core.
    // Default is HAP_MAIN_THREAD_PRIORITY=7; we drop it to 5 (same as httpd).
    {
        hap_cfg_t cfg;
        hap_get_config(&cfg);
        cfg.task_priority = 5;
        hap_set_config(&cfg);
    }

    hap_start();
    _started = true;
    ESP_LOGI(s_TAG, "HomeKit bridge started");
}

void HomeKitClass::resetPairings()
{
    if (!_started) return;
    hap_reset_pairings();
}

void HomeKitClass::toJSON(JsonResponse &resp)
{
    resp.addElem("started", _started);
    resp.addElem("setupCode", (const char *)_setupCode);
    resp.addElem("qrPayload", (const char *)_qrPayload);
    resp.addElem("pairedCount", (int8_t)(_started ? hap_get_paired_controller_count() : 0));
}

void HomeKitClass::notifyShadeState(SomfyShade *shade)
{
    if (!_started) return;

    hap_acc_t *acc = hap_get_first_acc();
    while (acc) {
        hap_serv_t *svc = getWindowCoveringService(acc);
        if (svc) {
            if (hap_serv_get_priv(svc) == shade) {
                int8_t pos = shade->transformPosition(shade->currentPos);
                int8_t tgt = shade->transformPosition(shade->target);
                uint8_t ps = directionToPositionState(shade->direction);

                hap_val_t val;

                val.u = (pos >= 0) ? (uint8_t)pos : 0;
                hap_char_update_val(hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_CURRENT_POSITION), &val);

                val.u = (tgt >= 0) ? (uint8_t)tgt : 0;
                hap_char_update_val(hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_TARGET_POSITION), &val);

                val.u = ps;
                hap_char_update_val(hap_serv_get_char_by_uuid(svc, HAP_CHAR_UUID_POSITION_STATE), &val);
                return;
            }
        }
        acc = hap_acc_get_next(acc);
    }
}

void HomeKitClass::addShade(SomfyShade *shade)
{
    if (!_started || !shade || shade->getShadeId() == 255 || shade->name[0] == '\0') return;
    hap_acc_t *acc = createShadeAccessory(shade);
    if (acc) {
        hap_add_bridged_accessory(acc, hap_get_unique_aid(shade->name));
        hap_update_config_number();
        ESP_LOGI(s_TAG, "Dynamically added shade '%s'", shade->name);
    }
}

void HomeKitClass::removeShade(SomfyShade *shade)
{
    if (!_started || !shade) return;
    hap_acc_t *acc = hap_get_first_acc();
    while (acc) {
        hap_serv_t *svc = getWindowCoveringService(acc);
        if (svc) {
            if (hap_serv_get_priv(svc) == shade) {
                hap_remove_bridged_accessory(acc);
                hap_update_config_number();
                ESP_LOGI(s_TAG, "Removed shade id=%d from HomeKit", shade->getShadeId());
                return;
            }
        }
        acc = hap_acc_get_next(acc);
    }
}
