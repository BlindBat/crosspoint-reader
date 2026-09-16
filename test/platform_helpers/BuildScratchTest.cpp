// lib/Memory/BuildScratch: exclusive lend/claim/release/reclaim of the
// framebuffer bytes during a build phase.

#include <gtest/gtest.h>

#include <BuildScratch.h>
#include <HostControls.h>
#include <Logging.h>

#include <cstdint>
#include <string>

namespace {

class BuildScratchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    clearLastLogs();
    buildscratch::reclaim();  // known-empty registry
    clearLastLogs();
  }
  void TearDown() override { buildscratch::reclaim(); }
  uint8_t block[1024] = {};
  uint8_t other[512] = {};
};

}  // namespace

TEST_F(BuildScratchTest, ClaimWithNothingLentReturnsNull) {
  size_t len = 99;
  EXPECT_EQ(buildscratch::claim(1, &len), nullptr);
  EXPECT_EQ(len, 99u);  // untouched
}

TEST_F(BuildScratchTest, LendThenClaimHandsOutWholeBlock) {
  buildscratch::lend(block, sizeof(block));
  size_t len = 0;
  EXPECT_EQ(buildscratch::claim(100, &len), block);
  EXPECT_EQ(len, sizeof(block));
}

TEST_F(BuildScratchTest, ClaimWithoutLenOutIsAllowed) {
  buildscratch::lend(block, sizeof(block));
  EXPECT_EQ(buildscratch::claim(sizeof(block)), block);
}

TEST_F(BuildScratchTest, ClaimLargerThanBlockFallsBackToHeap) {
  buildscratch::lend(block, sizeof(block));
  EXPECT_EQ(buildscratch::claim(sizeof(block) + 1), nullptr);
  EXPECT_EQ(buildscratch::claim(sizeof(block)), block);  // still unclaimed
}

TEST_F(BuildScratchTest, SecondClaimWhileHeldIsRefused) {
  buildscratch::lend(block, sizeof(block));
  ASSERT_EQ(buildscratch::claim(1), block);
  EXPECT_EQ(buildscratch::claim(1), nullptr);
  buildscratch::release(block);
  EXPECT_EQ(buildscratch::claim(1), block);
}

TEST_F(BuildScratchTest, ReleaseWithForeignPointerKeepsClaim) {
  buildscratch::lend(block, sizeof(block));
  ASSERT_EQ(buildscratch::claim(1), block);
  buildscratch::release(other);
  buildscratch::release(nullptr);
  EXPECT_EQ(buildscratch::claim(1), nullptr);
}

TEST_F(BuildScratchTest, ReclaimRemovesBlock) {
  buildscratch::lend(block, sizeof(block));
  buildscratch::reclaim();
  EXPECT_EQ(buildscratch::claim(1), nullptr);
  EXPECT_EQ(getLastLogs(), "");
}

TEST_F(BuildScratchTest, SecondLendIsIgnoredAndLogged) {
  buildscratch::lend(block, sizeof(block));
  buildscratch::lend(other, sizeof(other));
  size_t len = 0;
  EXPECT_EQ(buildscratch::claim(1, &len), block);
  EXPECT_EQ(len, sizeof(block));
  EXPECT_NE(getLastLogs().find("[ERR] [SCR] Build scratch lent twice"), std::string::npos);
}

TEST_F(BuildScratchTest, ReclaimWhileClaimedLogsAndResets) {
  buildscratch::lend(block, sizeof(block));
  ASSERT_EQ(buildscratch::claim(1), block);
  buildscratch::reclaim();
  EXPECT_NE(getLastLogs().find("[ERR] [SCR] Build scratch reclaimed while still claimed"), std::string::npos);
  buildscratch::lend(other, sizeof(other));
  EXPECT_EQ(buildscratch::claim(1), other);  // claimed flag was cleared
}

TEST_F(BuildScratchTest, ReleaseAfterReclaimDoesNotResurrectBlock) {
  buildscratch::lend(block, sizeof(block));
  ASSERT_EQ(buildscratch::claim(1), block);
  buildscratch::reclaim();
  buildscratch::release(block);
  EXPECT_EQ(buildscratch::claim(1), nullptr);
}

TEST_F(BuildScratchTest, NullLendLeavesTheRegistryEmptyAndUnguarded) {
  buildscratch::lend(nullptr, sizeof(block));
  EXPECT_EQ(buildscratch::claim(1), nullptr);
  EXPECT_EQ(getLastLogs(), "");
  buildscratch::lend(block, sizeof(block));  // a null lend does not arm the lent-twice guard
  EXPECT_EQ(buildscratch::claim(1), block);
}

TEST_F(BuildScratchTest, ZeroLengthClaimStillNeedsALentBlock) {
  EXPECT_EQ(buildscratch::claim(0), nullptr);
  buildscratch::lend(block, sizeof(block));
  size_t len = 0;
  EXPECT_EQ(buildscratch::claim(0, &len), block);
  EXPECT_EQ(len, sizeof(block));
}
