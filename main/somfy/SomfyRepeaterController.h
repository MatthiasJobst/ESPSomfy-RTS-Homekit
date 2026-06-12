// SomfyRepeaterController.h — Owns the list of hardware repeater addresses:
// link/unlink, compaction, counting and JSON serialisation.
#pragma once

#include <cstdint>
#include <functional>
#include "SomfyFrame.h" // SOMFY_MAX_REPEATERS

class JsonResponse;

/**
 * @brief Manages the fixed-size array of repeater addresses.
 */
class SomfyRepeaterController {
  public:
    /**
     * @brief Construct an empty repeater list.
     * @param markDirty Called when the list changes so the owner can flag the
     *                  config for persistence.
     */
    explicit SomfyRepeaterController(std::function<void()> markDirty);

    /**
     * @brief Direct access to a repeater slot by index.
     * @param i Slot index in [0, SOMFY_MAX_REPEATERS).
     * @return Reference to the address at that slot (0 if empty).
     * @note Low-level seam for persistence (ShadeConfigFile) and the transceiver.
     */
    uint32_t &repeaterSlot(uint8_t i);

    /**
     * @brief Link a repeater address: added to the first free slot if not already
     *        present, and any duplicate entry is cleared.
     * @param address Repeater remote address.
     * @return Always true.
     */
    bool linkRepeater(uint32_t address);

    /**
     * @brief Remove a repeater address and compact the list.
     * @param address Repeater remote address.
     * @return Always true.
     */
    bool unlinkRepeater(uint32_t address);

    /**
     * @brief Compact the list so non-zero entries are contiguous from the front.
     */
    void compressRepeaters();

    /**
     * @brief Count the non-empty repeater slots.
     * @return Number of linked repeaters.
     */
    uint8_t repeaterCount();

    /**
     * @brief Serialise the repeater addresses into a JSON array.
     * @param json Response to append addresses to.
     */
    void toJSONRepeaters(JsonResponse &json);

  private:
    uint32_t repeaters[SOMFY_MAX_REPEATERS] = {0}; /**< Repeater addresses, owned here. */
    std::function<void()> markDirty;               /**< Owner callback, invoked on changes. */
};
