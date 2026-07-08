// SlotArray.h — Generic slot-array helpers shared by the shade/group/room
// controllers. Each controller owns a fixed-size array of records addressed by a
// logical id (1..N) with a per-type empty-slot sentinel exposed as T::NO_ID
// (see SomfyShade::NO_ID / SomfyGroup::NO_ID / SomfyRoom::NO_ID). Persistence
// compacts records into the front slots on load, so the slot index is NOT the
// id; these helpers keep the "lowest free id" and "first empty slot" logic in one
// place so an off-by-one or sentinel change cannot diverge between the three.
//
// The array length N is deduced from the array reference, the sentinel is read
// from T::NO_ID, and the id of a record is read via an ADL customization point:
// each record type provides a free function `uint8_t slotId(T&)` (defined next to
// the type). Callers therefore pass only the array.
#pragma once
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace slots {

/**
 * @brief A record usable with the slot helpers: it exposes an empty-slot
 *        sentinel `T::NO_ID` and an id readable via an ADL `slotId(T&)`.
 *
 * Constraining the helpers on this turns a missing `slotId` overload (or NO_ID)
 * into a clear "constraint not satisfied" diagnostic at the call site rather than
 * a deep template error from inside the loop body.
 */
template <typename T>
concept SlotRecord = requires(T &rec) {
    { slotId(rec) } -> std::convertible_to<uint8_t>;
    { T::NO_ID } -> std::convertible_to<uint8_t>;
};

/**
 * @brief Lowest logical id in [1, N] not currently held by any slot.
 * @param slotArr Record array; its length N is the highest allocatable id.
 * @return A free id, or T::NO_ID if the array is full.
 */
template <SlotRecord T, size_t N>
uint8_t lowestFreeId(T (&slotArr)[N])
{
    static_assert(N <= 254, "id space (1..N) plus the sentinel must fit in uint8_t");
    // Single pass: mark every in-use id, reading each slot's id exactly once, then
    // return the lowest id not marked. The 1..N range check ignores empty slots for
    // both sentinel conventions (NO_ID == 0 for rooms, 255 for shades/groups).
    std::bitset<N + 1> inUse; // indexed by id; bit 0 unused
    for (T &slot : slotArr) {
        uint8_t id = slotId(slot);
        if (id >= 1 && id <= N) inUse.set(id);
    }
    for (size_t id = 1; id <= N; id++) {
        if (!inUse[id]) return static_cast<uint8_t>(id);
    }
    return T::NO_ID;
}

/**
 * @brief First slot whose id equals the empty sentinel T::NO_ID.
 * @param slotArr Record array.
 * @return Pointer to the first empty slot, or nullptr if the array is full.
 */
template <SlotRecord T, size_t N>
T *firstEmptySlot(T (&slotArr)[N])
{
    for (T &slot : slotArr) {
        if (slotId(slot) == T::NO_ID) return &slot;
    }
    return nullptr;
}

} // namespace slots
