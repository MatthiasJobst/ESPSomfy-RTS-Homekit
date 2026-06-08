// SomfyLinkedRemotes.cpp — linked-remote registry implementation. Mutates only
// in-memory state; the owning SomfyShade marks the config dirty on success.
#include "SomfyLinkedRemotes.h"

bool SomfyLinkedRemotes::linkRemote(uint32_t address, uint16_t rollingCode)
{
    for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
        if (linkedRemotes[i].getRemoteAddress() == address) {
            linkedRemotes[i].setRollingCode(rollingCode);
            return true;
        }
    }
    for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
        if (linkedRemotes[i].getRemoteAddress() == 0) {
            linkedRemotes[i].setRemoteAddress(address);
            linkedRemotes[i].setRollingCode(rollingCode);
            return true;
        }
    }
    return false;
}

bool SomfyLinkedRemotes::unlinkRemote(uint32_t address)
{
    for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++) {
        if (linkedRemotes[i].getRemoteAddress() == address) {
            linkedRemotes[i].setRemoteAddress(0);
            return true;
        }
    }
    return false;
}

void SomfyLinkedRemotes::clear()
{
    for (uint8_t i = 0; i < SOMFY_MAX_LINKED_REMOTES; i++)
        linkedRemotes[i].setRemoteAddress(0);
}
