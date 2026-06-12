#include "SomfyRepeaterController.h"
#include <cassert>
#include "WResp.h" // JsonResponse, used by toJSONRepeaters()

SomfyRepeaterController::SomfyRepeaterController(std::function<void()> markDirty) : markDirty(std::move(markDirty)) {}

uint32_t &SomfyRepeaterController::repeaterSlot(uint8_t i)
{
    assert(i < SOMFY_MAX_REPEATERS);
    return this->repeaters[i];
}

bool SomfyRepeaterController::linkRepeater(uint32_t address)
{
    bool bSet = false;
    for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
        if (!bSet && this->repeaters[i] == address)
            bSet = true;
        else if (bSet && this->repeaters[i] == address)
            this->repeaters[i] = 0;
    }
    if (!bSet) {
        for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
            if (this->repeaters[i] == 0) {
                this->repeaters[i] = address;
                return true;
            }
        }
    }
    return true;
}

bool SomfyRepeaterController::unlinkRepeater(uint32_t address)
{
    for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
        if (this->repeaters[i] == address) this->repeaters[i] = 0;
    }
    this->compressRepeaters();
    if (this->markDirty) this->markDirty();
    return true;
}

void SomfyRepeaterController::compressRepeaters()
{
    for (uint8_t i = 0, j = 0; i < SOMFY_MAX_REPEATERS; i++) {
        if (this->repeaters[i] != 0) {
            if (i != j) {
                this->repeaters[j] = this->repeaters[i];
                this->repeaters[i] = 0;
            }
            j++;
        }
    }
}

uint8_t SomfyRepeaterController::repeaterCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
        if (this->repeaters[i] != 0) count++;
    }
    return count;
}

void SomfyRepeaterController::toJSONRepeaters(JsonResponse &json)
{
    for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) {
        if (this->repeaters[i] != 0) json.addElem(static_cast<uint32_t>(this->repeaters[i]));
    }
}
