// SomfyLinkedRemotes.h — registry of secondary remotes paired to a SomfyShade:
// stores up to SOMFY_MAX_LINKED_REMOTES addresses and links/unlinks them. Pure
// in-memory state; persistence is the owning SomfyShade's responsibility.
#pragma once
#include "SomfyFrame.h"
#include "SomfyRemote.h"

class SomfyLinkedRemotes {
  public:
    SomfyRemote linkedRemotes[SOMFY_MAX_LINKED_REMOTES];

    SomfyRemote &get(uint8_t i) { return linkedRemotes[i]; }

    // Returns true when the address is linked: either already present (rolling
    // code updated) or newly stored. False when all slots are full.
    bool linkRemote(uint32_t address, uint16_t rollingCode = 0);
    // Returns true when the address was found and its slot cleared.
    bool unlinkRemote(uint32_t address);

    // Zero every slot.
    void clear();
};
