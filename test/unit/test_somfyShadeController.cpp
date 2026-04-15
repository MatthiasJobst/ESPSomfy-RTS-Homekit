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

// ── Fixture ──────────────────────────────────────────────────────────────────

class ControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (auto &s : somfy.shades) s.clear();
        for (auto &g : somfy.groups) g.clear();
        for (auto &r : somfy.rooms)  r.clear();
        memset(somfy.repeaters, 0, sizeof(somfy.repeaters));
        somfy.isDirty       = false;
        somfy.startingAddress = 0x100000;
        git.lockFS          = false;
        test_clock_ms       = 0;
        stub_save_call_count   = 0;
        stub_backup_call_count = 0;
        stub_shadeconfig_exists = false;
        somfy.cmdQueue   = SomfyCommandQueue{};
        somfy.lastCommit = 0;
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
        somfy.groups[0].setGroupId(1);
        somfy.groups[0].setRemoteAddress(0x111111);
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
    EXPECT_EQ(somfy.getNextGroupId(), 1);
}

TEST_F(ControllerTest, GetNextGroupId_AllUsed_Returns255) {
    for (uint8_t i = 0; i < SOMFY_MAX_GROUPS - 2; i++)
        somfy.groups[i].setGroupId(i + 1);
    EXPECT_EQ(somfy.getNextGroupId(), 255);
}

// ── getNextRoomId ─────────────────────────────────────────────────────────────

TEST_F(ControllerTest, GetNextRoomId_EmptyArray_Returns1) {
    EXPECT_EQ(somfy.getNextRoomId(), 1);
}

TEST_F(ControllerTest, GetNextRoomId_AllUsed_Returns0) {
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS - 2; i++)
        somfy.rooms[i].roomId = i + 1;
    EXPECT_EQ(somfy.getNextRoomId(), 0);
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
    EXPECT_EQ(somfy.getMaxGroupOrder(), -1);
}

TEST_F(ControllerTest, GetMaxRoomOrder_Empty_ReturnsMinus1) {
    EXPECT_EQ(somfy.getMaxRoomOrder(), -1);
}

TEST_F(ControllerTest, GetMaxRoomOrder_TwoRooms_ReturnsHighest) {
    somfy.rooms[0].roomId = 1; somfy.rooms[0].sortOrder = 2;
    somfy.rooms[1].roomId = 2; somfy.rooms[1].sortOrder = 7;
    EXPECT_EQ(somfy.getMaxRoomOrder(), 7);
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
    somfy.groups[0].setGroupId(1);
    EXPECT_EQ(somfy.groupCount(), 1);
}

TEST_F(ControllerTest, RoomCount_TwoRooms_Returns2) {
    somfy.rooms[0].roomId = 1;
    somfy.rooms[3].roomId = 4;
    EXPECT_EQ(somfy.roomCount(), 2);
}

TEST_F(ControllerTest, RepeaterCount_TwoNonZero_Returns2) {
    somfy.repeaters[0] = 0x100;
    somfy.repeaters[2] = 0x200;
    EXPECT_EQ(somfy.repeaterCount(), 2);
}

TEST_F(ControllerTest, RepeaterCount_AllZero_Returns0) {
    EXPECT_EQ(somfy.repeaterCount(), 0);
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
    somfy.groups[1].setGroupId(2);
    auto *g = somfy.getGroupById(2);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getGroupId(), 2);
}

TEST_F(ControllerTest, GetRoomById_Exists_ReturnsPointer) {
    somfy.rooms[1].roomId = 2;
    auto *r = somfy.getRoomById(2);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->roomId, 2);
}

TEST_F(ControllerTest, GetRoomById_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.getRoomById(7), nullptr);
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
    somfy.groups[0].setGroupId(1);
    somfy.groups[0].setRemoteAddress(0x333333);
    auto *g = somfy.findGroupByRemoteAddress(0x333333);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->getRemoteAddress(), 0x333333u);
}

TEST_F(ControllerTest, FindGroupByRemoteAddress_NotFound_ReturnsNull) {
    EXPECT_EQ(somfy.findGroupByRemoteAddress(0xCAFE00), nullptr);
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
    somfy.groups[0].setGroupId(1);
    somfy.groups[0].setRemoteAddress(0x100001);
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
    EXPECT_TRUE(somfy.linkRepeater(0xAABB));
    EXPECT_EQ(somfy.repeaters[0], 0xAABBu);
    EXPECT_EQ(somfy.repeaterCount(), 1);
}

TEST_F(ControllerTest, LinkRepeater_Duplicate_DoesNotDoubleAdd) {
    somfy.linkRepeater(0xAABB);
    somfy.linkRepeater(0xAABB);
    EXPECT_EQ(somfy.repeaterCount(), 1);
}

TEST_F(ControllerTest, UnlinkRepeater_Exists_RemovesIt) {
    somfy.repeaters[0] = 0x1111;
    somfy.repeaters[1] = 0x2222;
    somfy.unlinkRepeater(0x1111);
    EXPECT_EQ(somfy.repeaters[0], 0x2222u);  // compressed into slot 0
    EXPECT_EQ(somfy.repeaterCount(), 1);
}

TEST_F(ControllerTest, UnlinkRepeater_NotPresent_NoChange) {
    somfy.repeaters[0] = 0x1111;
    somfy.unlinkRepeater(0xDEAD);
    EXPECT_EQ(somfy.repeaterCount(), 1);
}

TEST_F(ControllerTest, CompressRepeaters_GapsRemoved) {
    somfy.repeaters[0] = 0;
    somfy.repeaters[1] = 0x1111;
    somfy.repeaters[2] = 0;
    somfy.repeaters[3] = 0x2222;
    somfy.compressRepeaters();
    EXPECT_EQ(somfy.repeaters[0], 0x1111u);
    EXPECT_EQ(somfy.repeaters[1], 0x2222u);
    EXPECT_EQ(somfy.repeaters[2], 0u);
    EXPECT_EQ(somfy.repeaters[3], 0u);
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

TEST_F(ControllerTest, AddShade_NoIdAvailable_ReturnsNull) {
    for (uint8_t i = 0; i < SOMFY_MAX_SHADES - 2; i++)
        somfy.shades[i].setShadeId(i + 1);
    EXPECT_EQ(somfy.addShade(), nullptr);
}

TEST_F(ControllerTest, AddRoom_Empty_ReturnsRoomWithId1) {
    auto *r = somfy.addRoom();
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->roomId, 1);
    EXPECT_TRUE(somfy.isDirty);
}

TEST_F(ControllerTest, AddRoom_NoIdAvailable_ReturnsNull) {
    for (uint8_t i = 0; i < SOMFY_MAX_ROOMS - 2; i++)
        somfy.rooms[i].roomId = i + 1;
    EXPECT_EQ(somfy.addRoom(), nullptr);
}

TEST_F(ControllerTest, AddGroup_Empty_ReturnsGroupWithId1) {
    auto *g = somfy.addGroup();
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
    somfy.rooms[0].roomId = 1;
    somfy.shades[0].setShadeId(1); somfy.shades[0].roomId = 1;
    somfy.shades[1].setShadeId(2); somfy.shades[1].roomId = 2;  // different room
    somfy.deleteRoom(1);
    EXPECT_EQ(somfy.rooms[0].roomId, 0);
    EXPECT_EQ(somfy.shades[0].roomId, 0);  // reset
    EXPECT_EQ(somfy.shades[1].roomId, 2);  // untouched
    EXPECT_EQ(stub_save_call_count, 1);
}

TEST_F(ControllerTest, DeleteRoom_ResetsAffectedGroups) {
    somfy.rooms[0].roomId = 1;
    somfy.groups[0].setGroupId(1); somfy.groups[0].roomId = 1;
    somfy.deleteRoom(1);
    EXPECT_EQ(somfy.groups[0].roomId, 0);
}

TEST_F(ControllerTest, DeleteGroup_Exists_ClearsSlot) {
    somfy.groups[1].setGroupId(2);
    somfy.deleteGroup(2);
    EXPECT_EQ(somfy.groups[1].getGroupId(), 255);
    EXPECT_EQ(stub_save_call_count, 1);
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
    somfy.loop();
    EXPECT_EQ(stub_save_call_count, 0);
}

TEST_F(ControllerTest, Loop_DirtyAboveThreshold_Commits) {
    somfy.isDirty = true;
    somfy.lastCommit = 0;
    test_clock_ms = 1001;  // > 1000 ms
    somfy.loop();
    EXPECT_EQ(stub_save_call_count, 1);
    EXPECT_FALSE(somfy.isDirty);
}

TEST_F(ControllerTest, Loop_NotDirty_NeverCommits) {
    somfy.isDirty = false;
    somfy.lastCommit = 0;
    test_clock_ms = 2000;
    somfy.loop();
    EXPECT_EQ(stub_save_call_count, 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// K  command queue — enqueue
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(CommandQueueTest, EnqueueShadeCommand_ValidShade_ReturnsTrue) {
    EXPECT_TRUE(somfy.enqueueShadeCommand(1, somfy_commands::Up, 1));
    EXPECT_EQ(somfy.cmdQueue.count, 1);
}

TEST_F(CommandQueueTest, EnqueueShadeCommand_QueueFull_ReturnsFalse) {
    for (int i = 0; i < CMD_QUEUE_SIZE; i++)
        somfy.enqueueShadeCommand(1, somfy_commands::Up, 1);
    EXPECT_FALSE(somfy.enqueueShadeCommand(1, somfy_commands::Up, 1));
    EXPECT_EQ(somfy.cmdQueue.count, CMD_QUEUE_SIZE);
}

TEST_F(CommandQueueTest, EnqueueShadeTarget_SetsTargetField) {
    somfy.enqueueShadeTarget(1, 0.75f);
    EXPECT_EQ(somfy.cmdQueue.count, 1);
    EXPECT_FLOAT_EQ(somfy.cmdQueue.entries[0].target, 0.75f);
}

TEST_F(CommandQueueTest, EnqueueGroupCommand_GroupExists_ReturnsTrue) {
    EXPECT_TRUE(somfy.enqueueGroupCommand(1, somfy_commands::Down, 1));
    EXPECT_EQ(somfy.cmdQueue.count, 1);
}

// ── drain ─────────────────────────────────────────────────────────────────────

TEST_F(CommandQueueTest, Loop_EmptyQueue_DoesNotCrash) {
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.loop();  // must not crash with empty queue
    EXPECT_EQ(somfy.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Loop_QueueNotReady_DoesNotDrain) {
    // Push one entry, but lastDrain = millis() so queue is not ready yet
    somfy.enqueueShadeCommand(1, somfy_commands::Up, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.cmdQueue.lastDrain = test_clock_ms;  // just drained
    somfy.loop();
    EXPECT_EQ(somfy.cmdQueue.count, 1);  // not drained
}

TEST_F(CommandQueueTest, Loop_QueueReady_PopsOneEntry) {
    somfy.enqueueShadeCommand(1, somfy_commands::Up, 1);
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.cmdQueue.lastDrain = 0;
    somfy.loop();
    EXPECT_EQ(somfy.cmdQueue.count, 0);
}

TEST_F(CommandQueueTest, Loop_ShadeNotFound_NoCrash) {
    somfy.enqueueShadeCommand(99, somfy_commands::Up, 1);  // id 99 not in array
    test_clock_ms = CMD_QUEUE_DRAIN_MS + 1;
    somfy.cmdQueue.lastDrain = 0;
    somfy.loop();
    EXPECT_EQ(somfy.cmdQueue.count, 0);  // entry drained gracefully
}
