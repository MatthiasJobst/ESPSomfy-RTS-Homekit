#include "SomfyGPIOControl.h"
#include "driver/gpio.h"
#include "Arduino.h"
#include "esp_log.h"

static const char *s_TAG = "SomfyGPIOControl";

void SomfyGPIOControl::setGPIOs(radio_proto proto, float currentPos, int8_t direction, int8_t tiltDirection,
                                shade_types shadeType, tilt_types tiltType)
{
    if (proto == radio_proto::GP_Relay) {
        uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
        uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;

        int8_t dir = direction;
        if (tiltType == tilt_types::tiltonly || (dir == 0 && tiltType == tilt_types::integrated)) dir = tiltDirection;

        if (shadeType == shade_types::drycontact) {
            gpio_set_level((gpio_num_t)this->gpioDown, currentPos == 100 ? p_on : p_off);
            this->gpioDir = currentPos == 100 ? 1 : -1;
        } else if (shadeType == shade_types::drycontact2) {
            if (currentPos == 100) {
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                gpio_set_level((gpio_num_t)this->gpioUp, p_on);
            } else {
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
                gpio_set_level((gpio_num_t)this->gpioDown, p_on);
            }
            this->gpioDir = currentPos == 100 ? 1 : -1;
        } else {
            switch (dir) {
            case -1:
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                gpio_set_level((gpio_num_t)this->gpioUp, p_on);
                if (dir != this->gpioDir) ESP_LOGI(s_TAG, "UP: true, DOWN: false");
                this->gpioDir = dir;
                break;
            case 1:
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
                gpio_set_level((gpio_num_t)this->gpioDown, p_on);
                if (dir != this->gpioDir) ESP_LOGI(s_TAG, "UP: false, DOWN: true");
                this->gpioDir = dir;
                break;
            default:
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                if (dir != this->gpioDir) ESP_LOGI(s_TAG, "UP: false, DOWN: false");
                this->gpioDir = dir;
                break;
            }
        }
    } else if (proto == radio_proto::GP_Remote) {
        if (millis() > this->gpioRelease) {
            uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
            gpio_set_level((gpio_num_t)this->gpioUp, p_off);
            gpio_set_level((gpio_num_t)this->gpioDown, p_off);
            gpio_set_level((gpio_num_t)this->gpioMy, p_off);
            this->gpioRelease = 0;
        }
    }
}

void SomfyGPIOControl::triggerGPIOs(somfy_frame_t &frame, radio_proto proto, shade_types shadeType, bool isToggle)
{
    if (proto == radio_proto::GP_Remote) {
        uint8_t p_on = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? HIGH : LOW;
        uint8_t p_off = (this->gpioFlags & (uint8_t)gpio_flags_t::LowLevelTrigger) == 0x00 ? LOW : HIGH;
        int8_t dir = 0;
        switch (frame.cmd) {
        case somfy_commands::My:
            if (shadeType != shade_types::drycontact && !isToggle) {
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                gpio_set_level((gpio_num_t)this->gpioMy, p_on);
                dir = 0;
                if (dir != this->gpioDir) ESP_LOGI(s_TAG, "UP: false, DOWN: false, MY: true");
            }
            break;
        case somfy_commands::Up:
            if (shadeType != shade_types::drycontact && !isToggle && shadeType != shade_types::drycontact2) {
                gpio_set_level((gpio_num_t)this->gpioMy, p_off);
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                gpio_set_level((gpio_num_t)this->gpioUp, p_on);
                dir = -1;
                ESP_LOGI(s_TAG, "UP: true, DOWN: false, MY: false");
            }
            break;
        case somfy_commands::Toggle:
        case somfy_commands::Down:
            if (shadeType != shade_types::drycontact && !isToggle && shadeType != shade_types::drycontact2) {
                gpio_set_level((gpio_num_t)this->gpioMy, p_off);
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
            }
            gpio_set_level((gpio_num_t)this->gpioDown, p_on);
            dir = 1;
            ESP_LOGI(s_TAG, "UP: false, DOWN: true, MY: false");
            break;
        case somfy_commands::MyUp:
            if (shadeType != shade_types::drycontact && !isToggle && shadeType != shade_types::drycontact2) {
                gpio_set_level((gpio_num_t)this->gpioDown, p_off);
                gpio_set_level((gpio_num_t)this->gpioMy, p_on);
                gpio_set_level((gpio_num_t)this->gpioUp, p_on);
                ESP_LOGI(s_TAG, "UP: true, DOWN: false, MY: true");
            }
            break;
        case somfy_commands::MyDown:
            if (shadeType != shade_types::drycontact && !isToggle && shadeType != shade_types::drycontact2) {
                gpio_set_level((gpio_num_t)this->gpioUp, p_off);
                gpio_set_level((gpio_num_t)this->gpioMy, p_on);
                gpio_set_level((gpio_num_t)this->gpioDown, p_on);
                ESP_LOGI(s_TAG, "UP: false, DOWN: true, MY: true");
            }
            break;
        case somfy_commands::MyUpDown:
            if (shadeType != shade_types::drycontact && isToggle && shadeType != shade_types::drycontact2) {
                gpio_set_level((gpio_num_t)this->gpioUp, p_on);
                gpio_set_level((gpio_num_t)this->gpioMy, p_on);
                gpio_set_level((gpio_num_t)this->gpioDown, p_on);
                ESP_LOGI(s_TAG, "UP: true, DOWN: true, MY: true");
            }
            break;
        default:
            break;
        }
        this->gpioRelease = millis() + (frame.repeats * 200);
        this->gpioDir = dir;
    }
}

bool SomfyGPIOControl::usesPin(uint8_t pin, radio_proto proto, shade_types shadeType, bool isToggle) const
{
    if (proto != radio_proto::GP_Remote && proto != radio_proto::GP_Relay) return false;
    if (this->gpioDown == pin)
        return true;
    else if (shadeType == shade_types::drycontact)
        return this->gpioDown == pin;
    else if (isToggle) {
        if (proto == radio_proto::GP_Relay && this->gpioUp == pin) return true;
    } else if (shadeType == shade_types::drycontact2) {
        if (proto == radio_proto::GP_Relay && (this->gpioUp == pin || this->gpioDown == pin)) return true;
    } else {
        if (this->gpioUp == pin || (proto == radio_proto::GP_Remote && this->gpioMy == pin)) return true;
    }
    return false;
}
