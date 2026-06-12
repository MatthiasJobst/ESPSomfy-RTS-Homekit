// test_somfyShadeController.cpp — unit tests for SomfyShadeController.
// Covers: ID allocation, max-order tracking, counting, lookup,
// remote-address allocation, repeater management, add/delete lifecycle,
// processFrame, commit/writeBackup lock guard, loop auto-commit,
// and command queue enqueue/drain.
#include "test_controller_access.h"   // extern SomfyShadeController somfy
#include "SomfyFrame.h"
#include "SomfyCommandQueue.h"
#include "ShadeConfigFile.h"          // for stub counter externs
#include "GitOTA.h"
#include <cstring>
#include <gtest/gtest.h>

// ── extern globals defined in globals_stub.cpp ──────────────────────────────
extern uint64_t     test_clock_ms;
extern GitUpdater   git;
extern bool         stub_shadeconfig_exists;
extern int          stub_save_call_count;
extern int          stub_backup_call_count;
extern bool         transceiver_stub_tx_busy;
extern int          transceiver_stub_begin_tx_count;

// ── Fixture ──────────────────────────────────────────────────────────────────

class ControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (auto &s : somfy.shades) s.clear();
        for (uint8_t i = 0; i < SOMFY_MAX_GROUPS; i++) somfy.groupController.groupSlot(i).clear();
        for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++) somfy.roomController.roomSlot(i).clear();
        for (uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++) somfy.repeaterController.repeaterSlot(i) = 0;
        somfy.isDirty       = false;
        somfy.startingAddress = 0x100000;
        git.lockFS          = false;
        test_clock_ms       = 0;
        stub_save_call_count   = 0;
        stub_backup_call_count = 0;
        stub_shadeconfig_exists = false;
        somfy.commandDispatcher.cmdQueue   = SomfyCommandQueue{};
        somfy.lastCommit = 0;
        transceiver_stub_tx_busy = false;
        transceiver_stub_begin_tx_count = 0;
    }
};

// CommandQueueTest pre-populates one shade and one group so queue dispatch tests
// have a real target without having to repeat that setup in every test.
class CommandQueueTest : public ControllerTest {
protected:
    void SetUp() override {
        ControllerTest::SetUp();
        somfy.shades[0].setShadeId(1);
        somfy.shades[0].setRemoteAddress(0xABCDEF);
        somfy.shades[0].bitLength = 56;
        somfy.shades[0].repeats   = 1;
        somfy.groupController.groupSlot(0).setGroupId(1);
        somfy.groupController.groupSlot(0).setRemoteAddress(0x111111);
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// A  getNextShadeId
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, GetNextShadeId_EmptyArray_Returns1) {
    EXPECT_EQ(somfy.getNextShadeId(), 1);
}

TEST_F(ControllerTest, GetNextShadeId_FirstSlotTaken_Returns2) {
    somfy.shades[0].setShadeId(1);
    EXPECT_EQ(somfy.getNextShadeId(), 2);
}

TEST_F(ControllerTest, GetNextShadeId_GapInMiddle_ReturnsGap) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[1].setShadeId(3);   // gap at 2
    EXPECT_EQ(somfy.getNextShadeId(), 2);
}

TEST_F(ControllerTest, GetNextShadeId_AllSlotsUsed_Returns255) {
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES - 2; i++)
        somfy.shades[i].setShadeId(i + 1);
    EXPECT_EQ(somfy.getNextShadeId(), 255);
}

// ── getNextGroupId ────────────────────────────────────────────────────────────

TEST_F(ControllerTest, GetNextGroupId_EmptyArray_Returns1) {
    EXPECT_EQ(somfy.groupController.getNextGroupId(), 1);
}

TEST_F(ControllerTest, GetNextGroupId_AllUsed_Returns255) {
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS - 2; i++)
        somfy.groupController.groupSlot(i).setGroupId(i + 1);
    EXPECT_EQ(somfy.groupController.getNextGroupId(), 255);
}

// ── getNextRoomId ─────────────────────────────────────────────────────────────

TEST_F(ControllerTest, GetNextRoomId_EmptyArray_Returns1) {
    EXPECT_EQ(somfy.roomController.getNextRoomId(), 1);
}

TEST_F(ControllerTest, GetNextRoomId_AllUsed_Returns0) {
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS - 2; i++)
        somfy.roomController.roomSlot(i).roomId = i + 1;
    EXPECT_EQ(somfy.roomController.getNextRoomId(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// B  getMax*Order
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, GetMaxShadeOrder_Empty_ReturnsMinus1) {
    EXPECT_EQ(somfy.getMaxShadeOrder(), -1);
}

TEST_F(ControllerTest, GetMaxShadeOrder_ThreeShades_ReturnsHighest) {
    somfy.shades[0].setShadeId(1); somfy.shades[0].sortOrder = 0;
    somfy.shades[1].setShadeId(2); somfy.shades[1].sortOrder = 5;
    somfy.shades[2].setShadeId(3); somfy.shades[2].sortOrder = 3;
    EXPECT_EQ(somfy.getMaxShadeOrder(), 5);
}

TEST_F(ControllerTest, GetMaxGroupOrder_Empty_ReturnsMinus1) {
    EXPECT_EQ(somfy.groupController.getMaxGroupOrder(), -1);
}

TEST_F(ControllerTest, GetMaxRoomOrder_Empty_ReturnsMinus1) {
    EXPECT_EQ(somfy.roomController.getMaxRoomOrder(), -1);
}

TEST_F(ControllerTest, GetMaxRoomOrder_TwoRooms_ReturnsHighest) {
    somfy.roomController.roomSlot(0).roomId = 1; somfy.roomController.roomSlot(0).sortOrder = 2;
    somfy.roomController.roomSlot(1).roomId = 2; somfy.roomController.roomSlot(1).sortOrder = 7;
    EXPECT_EQ(somfy.roomController.getMaxRoomOrder(), 7);
}

// ══════════════════════════════════════════════════════════════════════════════
// C  counting
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, ShadeCount_NoShades_Returns0) {
    EXPECT_EQ(somfy.shadeCount(), 0);
}

TEST_F(ControllerTest, ShadeCount_TwoShades_Returns2) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[5].setShadeId(6);
    EXPECT_EQ(somfy.shadeCount(), 2);
}

TEST_F(ControllerTest, GroupCount_OneGroup_Returns1) {
    somfy.groupController.groupSlot(0).setGroupId(1);
    EXPECT_EQ(somfy.groupController.groupCount(), 1);
}

TEST_F(ControllerTest, RoomCount_TwoRooms_Returns2) {
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.roomController.roomSlot(3).roomId = 4;
    EXPECT_EQ(somfy.roomController.roomCount(), 2);
}

TEST_F(ControllerTest, RepeaterCount_TwoNonZero_Returns2) {
    somfy.repeaterController.repeaterSlot(0) = 0x100;
    somfy.repeaterController.repeaterSlot(2) = 0x200;
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 2);
}

TEST_F(ControllerTest, RepeaterCount_AllZero_Returns0) {
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// D  lookup
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, GetShadeById_Exists_ReturnsPointer) {
    somfy.shades[2].setShadeId(3);
    auto *s = somfy.getShadeById(3);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getShadeId(), 3);
}

TEST_F(ControllerTest, GetShadeById_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.getShadeById(99), nullptr);
}

TEST_F(ControllerTest, GetGroupById_Exists_ReturnsPointer) {
    somfy.groupController.groupSlot(1).setGroupId(2);
    auto *g = somfy.groupController.getGroupById(2);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getGroupId(), 2);
}

TEST_F(ControllerTest, GetRoomById_Exists_ReturnsPointer) {
    somfy.roomController.roomSlot(1).roomId = 2;
    auto *r = somfy.roomController.getRoomById(2);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->roomId, 2);
}

TEST_F(ControllerTest, GetRoomById_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.roomController.getRoomById(7), nullptr);
}

TEST_F(ControllerTest, FindShadeByRemoteAddress_PrimaryMatch) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].setRemoteAddress(0x111111);
    auto *s = somfy.findShadeByRemoteAddress(0x111111);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getRemoteAddress(), 0x111111u);
}

TEST_F(ControllerTest, FindShadeByRemoteAddress_LinkedMatch) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].setRemoteAddress(0x111111);
    somfy.shades[0].getLinkedRemote(0).setRemoteAddress(0x222222);
    auto *s = somfy.findShadeByRemoteAddress(0x222222);
    EXPECT_NE(s, nullptr);
}

TEST_F(ControllerTest, FindShadeByRemoteAddress_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.findShadeByRemoteAddress(0xDEAD00), nullptr);
}

TEST_F(ControllerTest, FindGroupByRemoteAddress_Match) {
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).setRemoteAddress(0x333333);
    auto *g = somfy.groupController.findGroupByRemoteAddress(0x333333);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getRemoteAddress(), 0x333333u);
}

TEST_F(ControllerTest, FindGroupByRemoteAddress_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.groupController.findGroupByRemoteAddress(0xCAFE00), nullptr);
}

// ══════════════════════════════════════════════════════════════════════════════
// E  getNextRemoteAddress
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, GetNextRemoteAddress_NoCollision_ReturnsBaseId) {
    // With no existing shades/groups, address = startingAddress + id
    EXPECT_EQ(somfy.getNextRemoteAddress(1), 0x100001u);
    EXPECT_EQ(somfy.getNextRemoteAddress(5), 0x100005u);
}

TEST_F(ControllerTest, GetNextRemoteAddress_ShadeCollision_SkipsAddress) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].setRemoteAddress(0x100001);
    // id=1 → initial address 0x100001 collides → bumped to 0x100002
    EXPECT_EQ(somfy.getNextRemoteAddress(1), 0x100002u);
}

TEST_F(ControllerTest, GetNextRemoteAddress_GroupCollision_SkipsAddress) {
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).setRemoteAddress(0x100001);
    EXPECT_EQ(somfy.getNextRemoteAddress(1), 0x100002u);
}

TEST_F(ControllerTest, GetNextRemoteAddress_MultipleCollisions_FindsFirst) {
    somfy.shades[0].setShadeId(1); somfy.shades[0].setRemoteAddress(0x100001);
    somfy.shades[1].setShadeId(2); somfy.shades[1].setRemoteAddress(0x100002);
    EXPECT_EQ(somfy.getNextRemoteAddress(1), 0x100003u);
}

// ══════════════════════════════════════════════════════════════════════════════
// F  repeater management
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, LinkRepeater_Empty_AddsAddress) {
    EXPECT_TRUE(somfy.repeaterController.linkRepeater(0xAABB));
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(0), 0xAABBu);
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 1);
}

TEST_F(ControllerTest, LinkRepeater_Duplicate_DoesNotDoubleAdd) {
    somfy.repeaterController.linkRepeater(0xAABB);
    somfy.repeaterController.linkRepeater(0xAABB);
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 1);
}

TEST_F(ControllerTest, UnlinkRepeater_Exists_RemovesIt) {
    somfy.repeaterController.repeaterSlot(0) = 0x1111;
    somfy.repeaterController.repeaterSlot(1) = 0x2222;
    somfy.repeaterController.unlinkRepeater(0x1111);
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(0), 0x2222u);  // compressed into slot 0
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 1);
}

TEST_F(ControllerTest, UnlinkRepeater_NotPresent_NoChange) {
    somfy.repeaterController.repeaterSlot(0) = 0x1111;
    somfy.repeaterController.unlinkRepeater(0xDEAD);
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), 1);
}

TEST_F(ControllerTest, CompressRepeaters_GapsRemoved) {
    somfy.repeaterController.repeaterSlot(0) = 0;
    somfy.repeaterController.repeaterSlot(1) = 0x1111;
    somfy.repeaterController.repeaterSlot(2) = 0;
    somfy.repeaterController.repeaterSlot(3) = 0x2222;
    somfy.repeaterController.compressRepeaters();
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(0), 0x1111u);
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(1), 0x2222u);
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(2), 0u);
    EXPECT_EQ(somfy.repeaterController.repeaterSlot(3), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// G  addShade / addRoom / addGroup
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, AddShade_Empty_ReturnsValidPointerWithId1) {
    auto *s = somfy.addShade();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getShadeId(), 1);
}

TEST_F(ControllerTest, AddShade_SetsIsDirty) {
    somfy.addShade();
    EXPECT_TRUE(somfy.isDirty);
}

TEST_F(ControllerTest, AddShade_SortOrderIsMaxPlusOne) {
    somfy.shades[0].setShadeId(1); somfy.shades[0].sortOrder = 3;
    auto *s = somfy.addShade();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->sortOrder, 4);
}

// Regression: SomfyShade::clear() must reset sortOrder. Prior to the fix it
// left a stale (int8_t)255 sentinel that — once the field type widened to
// uint8_t — read as 255 and poisoned getMaxShadeOrder() (whose int8_t return
// then wrapped 255 → -1, giving the next shade sortOrder 0 instead of max+1).
TEST_F(ControllerTest, Clear_ResetsSortOrderToZero) {
    somfy.shades[0].sortOrder = 99;
    somfy.shades[0].clear();
    EXPECT_EQ(somfy.shades[0].sortOrder, 0u);
}

// Boundary: with no shades populated, getMaxShadeOrder() returns -1 and the
// first auto-assigned sortOrder lands at 0.
TEST_F(ControllerTest, AddShade_EmptyArray_FirstSortOrderIsZero) {
    auto *s = somfy.addShade();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->sortOrder, 0u);
}

// Sweep the realistic range of sortOrder values. SOMFY_MAX_SHADES=32, so
// practical sortOrder never exceeds 31 — but we cover the field boundaries
// the controller logic actually traverses.
class AddShadeSortOrderTest
    : public ControllerTest,
      public ::testing::WithParamInterface<uint8_t> {};

TEST_P(AddShadeSortOrderTest, NewShadeIsMaxPlusOne) {
    const uint8_t existing = GetParam();
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].sortOrder = existing;
    auto *s = somfy.addShade();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->sortOrder, existing + 1);
}

INSTANTIATE_TEST_SUITE_P(SortOrderRange, AddShadeSortOrderTest,
    ::testing::Values(uint8_t{0}, uint8_t{1}, uint8_t{5}, uint8_t{30}),
    [](const ::testing::TestParamInfo<uint8_t> &info) {
        return std::string("Existing") + std::to_string(info.param);
    }
);

TEST_F(ControllerTest, AddShade_NoIdAvailable_ReturnsNull) {
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES - 2; i++)
        somfy.shades[i].setShadeId(i + 1);
    EXPECT_EQ(somfy.addShade(), nullptr);
}

TEST_F(ControllerTest, AddRoom_Empty_ReturnsRoomWithId1) {
    auto *r = somfy.roomController.addRoom();
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->roomId, 1);
    EXPECT_TRUE(somfy.isDirty);
}

TEST_F(ControllerTest, AddRoom_NoIdAvailable_Asserts) {
    // addRoom() treats an exhausted id space as a programming error: getNextRoomId()
    // returning 0 should be impossible in practice, so it asserts rather than
    // returning nullptr. Fill every slot so the next id genuinely is 0.
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS; i++)
        somfy.roomController.roomSlot(i).roomId = i + 1;
    EXPECT_DEATH(somfy.roomController.addRoom(), "roomId != 0");
}

TEST_F(ControllerTest, AddGroup_Empty_ReturnsGroupWithId1) {
    auto *g = somfy.groupController.addGroup();
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getGroupId(), 1);
    EXPECT_TRUE(somfy.isDirty);
}

// ══════════════════════════════════════════════════════════════════════════════
// H  deleteShade / deleteRoom / deleteGroup
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, DeleteShade_Exists_ClearsSlotAndCallsCommit) {
    somfy.shades[1].setShadeId(2);
    somfy.deleteShade(2);
    EXPECT_EQ(somfy.shades[1].getShadeId(), 255);
    EXPECT_EQ(stub_save_call_count, 1);
}

TEST_F(ControllerTest, DeleteShade_NotFound_StillCallsCommit) {
    somfy.deleteShade(99);
    EXPECT_EQ(stub_save_call_count, 1);
}

TEST_F(ControllerTest, DeleteRoom_Exists_ClearsRoomAndResetsAffectedShades) {
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.shades[0].setShadeId(1); somfy.shades[0].roomId = 1;
    somfy.shades[1].setShadeId(2); somfy.shades[1].roomId = 2;  // different room
    somfy.isDirty = false;
    somfy.roomController.deleteRoom(1);
    EXPECT_EQ(somfy.roomController.roomSlot(0).roomId, 0);
    EXPECT_EQ(somfy.shades[0].roomId, 0);  // reset
    EXPECT_EQ(somfy.shades[1].roomId, 2);  // untouched
    EXPECT_TRUE(somfy.isDirty);            // deferred persistence: marked dirty, not written here
}

TEST_F(ControllerTest, DeleteRoom_ResetsAffectedGroups) {
    somfy.roomController.roomSlot(0).roomId = 1;
    somfy.groupController.groupSlot(0).setGroupId(1); somfy.groupController.groupSlot(0).roomId = 1;
    somfy.roomController.deleteRoom(1);
    EXPECT_EQ(somfy.groupController.groupSlot(0).roomId, 0);
}

TEST_F(ControllerTest, DeleteGroup_Exists_ClearsSlot) {
    somfy.groupController.groupSlot(1).setGroupId(2);
    somfy.isDirty = false;
    somfy.groupController.deleteGroup(2);
    EXPECT_EQ(somfy.groupController.groupSlot(1).getGroupId(), 255);
    EXPECT_TRUE(somfy.isDirty);  // deferred persistence: marked dirty, not written here
}

// ══════════════════════════════════════════════════════════════════════════════
// I  commit / writeBackup lock guard
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, Commit_LockFsFalse_SavesAndClearsDirty) {
    git.lockFS   = false;
    somfy.isDirty = true;
    somfy.commit();
    EXPECT_EQ(stub_save_call_count, 1);
    EXPECT_FALSE(somfy.isDirty);
}

TEST_F(ControllerTest, Commit_LockFsTrue_SkipsSave) {
    git.lockFS   = true;
    somfy.isDirty = true;
    somfy.commit();
    EXPECT_EQ(stub_save_call_count, 0);
    EXPECT_TRUE(somfy.isDirty);  // not cleared
}

TEST_F(ControllerTest, WriteBackup_LockFsFalse_CallsBackup) {
    git.lockFS = false;
    somfy.writeBackup();
    EXPECT_EQ(stub_backup_call_count, 1);
}

TEST_F(ControllerTest, WriteBackup_LockFsTrue_SkipsBackup) {
    git.lockFS = true;
    somfy.writeBackup();
    EXPECT_EQ(stub_backup_call_count, 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// J  loop auto-commit
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, Loop_DirtyBelowThreshold_NoCommit) {
    somfy.isDirty = true;
    somfy.lastCommit = 0;
    test_clock_ms = 500;  // < 1000 ms
    stateMachine.loop();
    EXPECT_EQ(stub_save_call_count, 0);
}

TEST_F(ControllerTest, Loop_DirtyAboveThreshold_Commits) {
    somfy.isDirty = true;
    somfy.lastCommit = 0;
    test_clock_ms = 1001;  // > 1000 ms
    stateMachine.loop();
    EXPECT_EQ(stub_save_call_count, 1);
    EXPECT_FALSE(somfy.isDirty);
}

TEST_F(ControllerTest, Loop_NotDirty_NeverCommits) {
    somfy.isDirty = false;
    somfy.lastCommit = 0;
    test_clock_ms = 2000;
    stateMachine.loop();
    EXPECT_EQ(stub_save_call_count, 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// K  command queue — enqueue
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandQueueTest, EnqueueShadeCommand_ValidShade_ReturnsTrue) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
}

TEST_F(CommandQueueTest, EnqueueShadeCommand_QueueFull_ReturnsFalse) {
    for (int i = 0; i < CMD_QUEUE_SIZE; i++)
        somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1);
    EXPECT_FALSE(somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, CMD_QUEUE_SIZE);
}

TEST_F(CommandQueueTest, EnqueueShadeTarget_SetsTargetField) {
    somfy.commandDispatcher.enqueueShadeTarget(somfy.getShadeById(1), 0.75f);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_FLOAT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].target, 0.75f);
}

TEST_F(CommandQueueTest, EnqueueGroupCommand_GroupExists_ReturnsTrue) {
    EXPECT_TRUE(
        somfy.commandDispatcher.enqueueGroupCommand(somfy.groupController.getGroupById(1), somfy_commands::Down, 1));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
}

// ── drain ─────────────────────────────────────────────────────────────────────

TEST_F(CommandQueueTest, Loop_EmptyQueue_DoesNotCrash) {
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    stateMachine.loop();  // must not crash with empty queue
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Loop_QueueNotReady_DoesNotDrain) {
    // Push one entry, but lastDrain = millis() so queue is not ready yet
    somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = test_clock_ms;  // just drained
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);  // not drained
}

TEST_F(CommandQueueTest, Loop_QueueReady_PopsOneEntry) {
    somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Loop_ShadeNotFound_NoCrash) {
    somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(99), somfy_commands::Up, 1);  // id 99 not in array
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);  // entry drained gracefully
}

// ── queue reset on shade/group mutation ─────────────────────────────────────────
// Entries hold a pointer to their target, so the queue is flushed whenever a
// shade or group is added or removed to avoid acting on a stale slot.

TEST_F(CommandQueueTest, DeleteShade_ResetsQueue) {
    somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    somfy.deleteShade(1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, DeleteGroup_ResetsQueue) {
    somfy.commandDispatcher.enqueueGroupCommand(somfy.groupController.getGroupById(1), somfy_commands::Down, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    somfy.groupController.deleteGroup(1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, AddShade_ResetsQueue) {
    somfy.commandDispatcher.enqueueShadeCommand(somfy.getShadeById(1), somfy_commands::Up, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    somfy.addShade();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, AddGroup_ResetsQueue) {
    somfy.commandDispatcher.enqueueGroupCommand(somfy.groupController.getGroupById(1), somfy_commands::Down, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    somfy.groupController.addGroup();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// L  end()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, End_DoesNotCrash) {
    stateMachine.end();  // calls transceiver.disableReceive() (stubbed)
}

// ══════════════════════════════════════════════════════════════════════════════
// M  updateGroupFlags
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, UpdateGroupFlags_ValidGroup_DoesNotCrash) {
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.updateGroupFlags();  // covers lines 61-64
}

TEST_F(ControllerTest, UpdateGroupFlags_FlagsChange_EmitsState) {
    // Pre-set non-zero flags so updateFlags() (which resetFlags() + re-ORs shades) changes them
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).flags.setFlags(0x01);  // group has SunFlag set
    // No linked shades → updateFlags() resets to 0 → oldFlags != new flags → emitState() on line 65
    somfy.groupController.updateGroupFlags();
    EXPECT_EQ(somfy.groupController.groupSlot(0).flags.getFlags(), 0);  // flags were reset
}

// ══════════════════════════════════════════════════════════════════════════════
// N  begin()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, Begin_FileNotExists_StartClean) {
    stub_shadeconfig_exists = false;
    EXPECT_TRUE(stateMachine.begin());
}

TEST_F(ControllerTest, Begin_FileExists_LoadsConfig) {
    stub_shadeconfig_exists = true;
    EXPECT_TRUE(stateMachine.begin());
}

TEST_F(ControllerTest, Begin_ShadeWithZeroBitLength_Commits) {
    stub_shadeconfig_exists = false;
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].bitLength = 0;  // triggers saveFlag path
    EXPECT_TRUE(stateMachine.begin());
    EXPECT_EQ(stub_save_call_count, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// O  processWaitingFrame / emitState / publish
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, ProcessWaitingFrame_WithShade_DoesNotCrash) {
    somfy.shades[0].setShadeId(1);
    somfy.processWaitingFrame();
}

TEST_F(ControllerTest, ProcessWaitingFrame_NoShades_DoesNotCrash) {
    somfy.processWaitingFrame();
}

TEST_F(ControllerTest, EmitState_WithShade_DoesNotCrash) {
    somfy.shades[0].setShadeId(1);
    somfy.emitState();
}

TEST_F(ControllerTest, EmitState_NoShades_DoesNotCrash) {
    somfy.emitState();
}

TEST_F(ControllerTest, Publish_Empty_DoesNotCrash) {
    somfy.publish();
}

TEST_F(ControllerTest, Publish_WithShadeAndGroup_DoesNotCrash) {
    somfy.shades[0].setShadeId(1);
    somfy.shades[0].setRemoteAddress(0x100001);
    somfy.groupController.groupSlot(0).setGroupId(1);
    somfy.groupController.groupSlot(0).setRemoteAddress(0x200001);
    somfy.publish();
}

// ══════════════════════════════════════════════════════════════════════════════
// P  addShade(JsonObject&) / addRoom(JsonObject&) / addGroup(JsonObject&)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, AddShadeFromJSON_ReturnsValidShade) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    SomfyShade *s = somfy.addShade(obj);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getShadeId(), 1);
}

TEST_F(ControllerTest, AddRoomFromJSON_ReturnsValidRoom) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    SomfyRoom *r = somfy.roomController.addRoom(obj);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->roomId, 1);
}

TEST_F(ControllerTest, AddGroupFromJSON_ReturnsValidGroup) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    SomfyGroup *g = somfy.groupController.addGroup(obj);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getGroupId(), 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// Q  linkRepeater — all-slots-full path
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, LinkRepeater_AllSlotsFull_ReturnsTrueWithoutAdding) {
    for(uint8_t i = 0; i < SOMFY_MAX_REPEATERS; i++)
        somfy.repeaterController.repeaterSlot(i) = 0x1000 + i;  // fill all slots with distinct non-zero values
    // Address not present and no empty slot — falls through to final return true
    bool result = somfy.repeaterController.linkRepeater(0xDEAD);
    EXPECT_TRUE(result);
    EXPECT_EQ(somfy.repeaterController.repeaterCount(), SOMFY_MAX_REPEATERS);  // unchanged
}

// ══════════════════════════════════════════════════════════════════════════════
// R  loadShadesFile / toJSONRooms / toJSONShades / toJSONGroups / toJSONRepeaters
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(ControllerTest, LoadShadesFile_ReturnsTrue) {
    EXPECT_TRUE(somfy.loadShadesFile("/shades.cfg"));
}

TEST_F(ControllerTest, ToJSONRooms_WithRoom_DoesNotCrash) {
    somfy.roomController.roomSlot(0).roomId = 1;
    JsonResponse json;
    somfy.roomController.toJSONRooms(json);
}

TEST_F(ControllerTest, ToJSONRooms_Empty_DoesNotCrash) {
    JsonResponse json;
    somfy.roomController.toJSONRooms(json);
}

TEST_F(ControllerTest, ToJSONShades_WithShade_DoesNotCrash) {
    somfy.shades[0].setShadeId(1);
    JsonResponse json;
    somfy.toJSONShades(json);
}

TEST_F(ControllerTest, ToJSONShades_Empty_DoesNotCrash) {
    JsonResponse json;
    somfy.toJSONShades(json);
}

TEST_F(ControllerTest, ToJSONGroups_WithGroup_DoesNotCrash) {
    somfy.groupController.groupSlot(0).setGroupId(1);
    JsonResponse json;
    somfy.groupController.toJSONGroups(json);
}

TEST_F(ControllerTest, ToJSONGroups_Empty_DoesNotCrash) {
    JsonResponse json;
    somfy.groupController.toJSONGroups(json);
}

TEST_F(ControllerTest, ToJSONRepeaters_WithRepeater_DoesNotCrash) {
    somfy.repeaterController.repeaterSlot(0) = 0x1234;
    JsonResponse json;
    somfy.repeaterController.toJSONRepeaters(json);
}

TEST_F(ControllerTest, ToJSONRepeaters_Empty_DoesNotCrash) {
    JsonResponse json;
    somfy.repeaterController.toJSONRepeaters(json);
}

// ══════════════════════════════════════════════════════════════════════════════
// S  enqueue variants
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandQueueTest, EnqueueShadeTiltTarget_SetsFields) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueShadeTiltTarget(somfy.getShadeById(1), 0.5f));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].type, cmd_queue_type_t::ShadeTiltTarget);
    EXPECT_FLOAT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].target, 0.5f);
}

TEST_F(CommandQueueTest, EnqueueShadeTargetForced_SetsFields) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueShadeTargetForced(somfy.getShadeById(1), 0.42f));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].type, cmd_queue_type_t::ShadeTargetForced);
    EXPECT_FLOAT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].target, 0.42f);
}

TEST_F(CommandQueueTest, EnqueueShadeTargetForced_QueueFull_ReturnsFalse) {
    for (int i = 0; i < CMD_QUEUE_SIZE; i++)
        somfy.commandDispatcher.enqueueShadeTargetForced(somfy.getShadeById(1), 0.42f);
    EXPECT_FALSE(somfy.commandDispatcher.enqueueShadeTargetForced(somfy.getShadeById(1), 0.42f));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, CMD_QUEUE_SIZE);
}

TEST_F(CommandQueueTest, Drain_ShadeTargetForced_Dispatches) {
    somfy.commandDispatcher.enqueueShadeTargetForced(somfy.getShadeById(1), 0.6f);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();  // drains the queue → dispatches the ShadeTargetForced case
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, SendFrame_TxBusy_SkipsTransmit) {
    transceiver_stub_tx_busy = true;
    transceiver_stub_begin_tx_count = 0;
    somfy_frame_t frame{};
    somfy.commandDispatcher.sendFrame(frame, 0);
    EXPECT_EQ(transceiver_stub_begin_tx_count, 0);  // TX busy → frame skipped, no transmit
}

TEST_F(CommandQueueTest, EnqueueShadeTiltCommand_SetsFields) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueShadeTiltCommand(somfy.getShadeById(1), somfy_commands::Up));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].type, cmd_queue_type_t::ShadeTiltCommand);
}

TEST_F(CommandQueueTest, EnqueueShadeSensor_SetsFields) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueShadeSensor(somfy.getShadeById(1), 1, 0, 2));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].type, cmd_queue_type_t::ShadeSensor);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].isWindy, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].isSunny, 0);
}

TEST_F(CommandQueueTest, EnqueueGroupSensor_SetsFields) {
    EXPECT_TRUE(somfy.commandDispatcher.enqueueGroupSensor(somfy.groupController.getGroupById(1), 0, 1, 1));
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 1);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].type, cmd_queue_type_t::GroupSensor);
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.entries[0].isSunny, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// T  drain queue — one test per cmd_queue_type_t branch
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandQueueTest, Drain_ShadeTarget_DoesNotCrash) {
    somfy.commandDispatcher.enqueueShadeTarget(somfy.getShadeById(1), 0.8f);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_ShadeTiltTarget_DoesNotCrash) {
    somfy.commandDispatcher.enqueueShadeTiltTarget(somfy.getShadeById(1), 0.3f);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_ShadeTiltCommand_DoesNotCrash) {
    somfy.commandDispatcher.enqueueShadeTiltCommand(somfy.getShadeById(1), somfy_commands::My);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_ShadeSensor_DoesNotCrash) {
    somfy.commandDispatcher.enqueueShadeSensor(somfy.getShadeById(1), 1, 0, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_GroupCommand_DoesNotCrash) {
    somfy.commandDispatcher.enqueueGroupCommand(somfy.groupController.getGroupById(1), somfy_commands::Down, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_GroupSensor_DoesNotCrash) {
    somfy.commandDispatcher.enqueueGroupSensor(somfy.groupController.getGroupById(1), 0, 1, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_ShadeTargetNotFound_DoesNotCrash) {
    somfy.commandDispatcher.enqueueShadeTarget(somfy.getShadeById(99), 0.5f);  // shade 99 doesn't exist
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Drain_GroupCommandNotFound_DoesNotCrash) {
    somfy.commandDispatcher.enqueueGroupCommand(somfy.groupController.getGroupById(99), somfy_commands::Up, 1);  // group 99 doesn't exist
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.commandDispatcher.cmdQueue.lastDrain = 0;
    stateMachine.loop();
    EXPECT_EQ(somfy.commandDispatcher.cmdQueue.count, 0);
}
