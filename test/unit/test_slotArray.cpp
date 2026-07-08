// test_slotArray.cpp — Direct unit tests for the generic slot-array helpers.
// Tested in isolation (no controller) with tiny record types so the "lowest free
// id" and "first empty slot" logic is verified once, not only via the three
// controllers that delegate to it. The array length is deduced, the sentinel is
// read from T::NO_ID, and the id via an ADL slotId(T&), so the helpers take only
// the array.
#include <gtest/gtest.h>
#include <set>
#include <vector>
#include "SlotArray.h"

namespace {

// Shade/group-style record: sentinel 255 (0xFF erased-record convention).
struct Rec {
    static constexpr uint8_t NO_ID = 255;
    uint8_t id = NO_ID;
};

// Room-style record: sentinel 0, which doubles as the "full" return value.
struct RecZero {
    static constexpr uint8_t NO_ID = 0;
    uint8_t id = NO_ID;
};

// ADL id accessors, mirroring the slotId(Somfy*&) overloads on the real records.
uint8_t slotId(Rec &r) { return r.id; }
uint8_t slotId(RecZero &r) { return r.id; }

constexpr uint8_t CAP = 4;

template <typename R, size_t N>
void fill(R (&arr)[N], std::initializer_list<uint8_t> ids)
{
    size_t i = 0;
    for (uint8_t id : ids) arr[i++].id = id;
    for (; i < N; i++) arr[i].id = R::NO_ID;
}

// ── lowestFreeId (parameterized over occupancy scenarios) ─────────────────────
// Each scenario runs against BOTH sentinel conventions (Rec=255, RecZero=0) so the
// range guard that ignores empty slots is pinned for both NO_ID values. Occupied
// ids are placed in the front slots (slot != id-1) to mimic post-compaction state.
struct FreeIdCase {
    const char *name;
    std::vector<uint8_t> occupied;
    int expectedFree; // >= 1: expected free id; -1: array full -> expect T::NO_ID
};

class LowestFreeIdTest : public ::testing::TestWithParam<FreeIdCase> {
  protected:
    static constexpr size_t kSize = 6;

    template <typename R>
    uint8_t run(const std::vector<uint8_t> &occupied)
    {
        R arr[kSize];
        for (auto &slot : arr) slot.id = R::NO_ID;
        size_t i = 0;
        for (uint8_t id : occupied) arr[i++].id = id;
        return slots::lowestFreeId(arr);
    }
};

TEST_P(LowestFreeIdTest, MatchesForBothSentinels)
{
    const auto &c = GetParam();
    if (c.expectedFree < 0) {
        EXPECT_EQ(run<Rec>(c.occupied), Rec::NO_ID);
        EXPECT_EQ(run<RecZero>(c.occupied), RecZero::NO_ID);
    } else {
        EXPECT_EQ(run<Rec>(c.occupied), static_cast<uint8_t>(c.expectedFree));
        EXPECT_EQ(run<RecZero>(c.occupied), static_cast<uint8_t>(c.expectedFree));
    }
}

INSTANTIATE_TEST_SUITE_P(
    Scenarios, LowestFreeIdTest,
    ::testing::Values(FreeIdCase{"Empty_ReturnsOne", {}, 1},
                      FreeIdCase{"SkipsUsedPrefix", {1, 2}, 3},
                      FreeIdCase{"ReusesLowGap", {2, 3}, 1},
                      FreeIdCase{"ReusesMiddleGap", {1, 3, 4}, 2},
                      FreeIdCase{"UsesFullCapacity", {1, 2, 3, 4, 5}, 6},
                      FreeIdCase{"Full_ReturnsSentinel", {1, 2, 3, 4, 5, 6}, -1}),
    [](const ::testing::TestParamInfo<FreeIdCase> &info) { return info.param.name; });

// ── firstEmptySlot ────────────────────────────────────────────────────────────

TEST(SlotArrayTest, FirstEmptySlot_EmptyArray_ReturnsFirst)
{
    Rec arr[CAP];
    fill(arr, {});
    EXPECT_EQ(slots::firstEmptySlot(arr), &arr[0]);
}

TEST(SlotArrayTest, FirstEmptySlot_ReturnsFirstHole)
{
    Rec arr[CAP];
    fill(arr, {1, 3}); // slots 0,1 used; slot 2 is the first hole
    EXPECT_EQ(slots::firstEmptySlot(arr), &arr[2]);
}

TEST(SlotArrayTest, FirstEmptySlot_FullArray_ReturnsNull)
{
    Rec arr[CAP];
    fill(arr, {1, 2, 3, 4});
    EXPECT_EQ(slots::firstEmptySlot(arr), nullptr);
}

// ── allocate-until-full cycle ─────────────────────────────────────────────────
// Drives both helpers the way a controller's add*() does: for each of CAP
// allocations, take the lowest free id, place it in the first empty slot, and
// confirm exactly CAP distinct ids (1..CAP) fit before the array reports full.
TEST(SlotArrayTest, AllocateUntilFull_FillsEverySlotThenReportsFull)
{
    Rec arr[CAP];
    fill(arr, {});
    std::set<uint8_t> seen;
    for (uint8_t n = 0; n < CAP; n++) {
        uint8_t id = slots::lowestFreeId(arr);
        ASSERT_NE(id, Rec::NO_ID) << "ran out of ids after " << static_cast<int>(n) << " allocations";
        Rec *slot = slots::firstEmptySlot(arr);
        ASSERT_NE(slot, nullptr) << "ran out of slots after " << static_cast<int>(n) << " allocations";
        slot->id = id;
        EXPECT_TRUE(seen.insert(id).second) << "duplicate id " << static_cast<int>(id);
    }
    EXPECT_EQ(seen.size(), static_cast<size_t>(CAP));
    // Array is now full: both helpers must report exhaustion.
    EXPECT_EQ(slots::lowestFreeId(arr), Rec::NO_ID);
    EXPECT_EQ(slots::firstEmptySlot(arr), nullptr);
}

} // namespace
