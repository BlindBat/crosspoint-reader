// lib/Logging: "[ms] [LVL] [ORIGIN] message" formatting and the 16-line RTC
// ring buffer guarded by a magic word (FR-185).

#include <HostControls.h>
#include <Logging.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

// RTC_NOINIT globals owned by Logging.cpp; tests corrupt them to simulate a cold boot.
extern char logMessages[16][256];
extern size_t logHead;
extern uint32_t rtcLogMagic;

namespace {

class LoggingRingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    clearLastLogs();
  }
};

constexpr uint32_t kMagic = 0xDEADBEEF;

}  // namespace

TEST_F(LoggingRingTest, FormatsTimestampLevelAndOrigin) {
  host::setMillis(1234);
  LOG_ERR("T", "hello %d", 7);
  EXPECT_EQ(getLastLogs(), "[1234] [ERR] [T] hello 7\n");
  EXPECT_EQ(host::serialOutput(), "[1234] [ERR] [T] hello 7\n");
}

TEST_F(LoggingRingTest, InfoAndDebugLevelsAreTagged) {
  host::setMillis(5);
  LOG_INF("ORIG", "info");
  LOG_DBG("ORIG", "debug");
  EXPECT_EQ(getLastLogs(), "[5] [INF] [ORIG] info\n[5] [DBG] [ORIG] debug\n");
}

TEST_F(LoggingRingTest, PreservesInsertionOrder) {
  LOG_ERR("A", "one");
  LOG_ERR("B", "two");
  LOG_ERR("C", "three");
  EXPECT_EQ(getLastLogs(), "[0] [ERR] [A] one\n[0] [ERR] [B] two\n[0] [ERR] [C] three\n");
}

TEST_F(LoggingRingTest, SixteenLinesFitWithoutLoss) {
  for (int i = 0; i < 16; i++) LOG_ERR("R", "L%02d", i);
  std::string expected;
  for (int i = 0; i < 16; i++) {
    expected += "[0] [ERR] [R] L" + std::string(i < 10 ? "0" : "") + std::to_string(i) + "\n";
  }
  EXPECT_EQ(getLastLogs(), expected);
  EXPECT_EQ(logHead, 0u);  // wrapped exactly to the start
}

TEST_F(LoggingRingTest, RingKeepsOnlyTheLastSixteenLinesInOrder) {
  for (int i = 0; i < 20; i++) LOG_ERR("R", "L%02d", i);
  std::string expected;
  for (int i = 4; i < 20; i++) {
    expected += "[0] [ERR] [R] L" + std::string(i < 10 ? "0" : "") + std::to_string(i) + "\n";
  }
  EXPECT_EQ(getLastLogs(), expected);
  EXPECT_EQ(getLastLogs().find("L03"), std::string::npos);
}

TEST_F(LoggingRingTest, LongMessageIsTruncatedToEntryCapacity) {
  const std::string big(300, 'x');
  LOG_ERR("T", "%s", big.c_str());
  const std::string logs = getLastLogs();
  EXPECT_EQ(logs.size(), 255u);  // 256-byte entry minus the terminator; the newline is cut off
  EXPECT_EQ(logs.rfind("[0] [ERR] [T] ", 0), 0u);
  EXPECT_EQ(logs.back(), 'x');
}

TEST_F(LoggingRingTest, ControlCharactersPassThroughUnmodified) {
  LOG_ERR("T",
          "a\x01"
          "b\x1b[0m");
  EXPECT_EQ(getLastLogs(), std::string("[0] [ERR] [T] a\x01"
                                       "b\x1b[0m\n"));
}

TEST_F(LoggingRingTest, ClearLastLogsEmptiesRingAndStampsMagic) {
  LOG_ERR("T", "x");
  rtcLogMagic = 0;
  clearLastLogs();
  EXPECT_EQ(rtcLogMagic, kMagic);
  EXPECT_EQ(logHead, 0u);
  EXPECT_EQ(getLastLogs(), "");
}

TEST_F(LoggingRingTest, WrongMagicHidesRingContents) {
  LOG_ERR("T", "kept");
  rtcLogMagic = 0x12345678;
  EXPECT_EQ(getLastLogs(), "");
}

TEST_F(LoggingRingTest, SanitizeLogHeadFlagsBadMagicAndResetsHead) {
  LOG_ERR("T", "x");
  LOG_ERR("T", "y");
  rtcLogMagic = 0;
  EXPECT_TRUE(sanitizeLogHead());
  EXPECT_EQ(logHead, 0u);
  EXPECT_NE(rtcLogMagic, kMagic);  // still untrusted until clearLastLogs()
}

TEST_F(LoggingRingTest, SanitizeLogHeadFlagsHeadOutOfRange) {
  logHead = 16;
  EXPECT_TRUE(sanitizeLogHead());
  EXPECT_EQ(logHead, 0u);
}

TEST_F(LoggingRingTest, SanitizeLogHeadAcceptsConsistentState) {
  for (int i = 0; i < 5; i++) LOG_ERR("T", "x");
  EXPECT_FALSE(sanitizeLogHead());
  EXPECT_EQ(logHead, 5u);
}

TEST_F(LoggingRingTest, LoggingAfterColdBootGarbageResetsRing) {
  memset(logMessages, 'G', sizeof(logMessages));  // no terminators anywhere
  logHead = 3;
  rtcLogMagic = 0;
  LOG_ERR("T", "fresh");
  EXPECT_EQ(rtcLogMagic, kMagic);
  EXPECT_EQ(getLastLogs(), "[0] [ERR] [T] fresh\n");
}

TEST_F(LoggingRingTest, LoggingWithHeadOutOfRangeResetsRing) {
  memset(logMessages, 'G', sizeof(logMessages));
  logHead = 200;
  rtcLogMagic = kMagic;
  LOG_ERR("T", "fresh");
  EXPECT_EQ(logHead, 1u);
  EXPECT_EQ(getLastLogs(), "[0] [ERR] [T] fresh\n");
}
