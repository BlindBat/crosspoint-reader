// FR-185: on Sticky the log transport is the IDF ROM console, not logSerial.
// The transport is a compile-time switch, so Logging.cpp is compiled a second
// time here inside a namespace under FREEINK_LOG_TRANSPORT_ROM_PRINTF, using
// the variant mechanics of test/ota_asset_selection/StickyVariantTest.cpp.
// LOG_ERR/LOG_INF/LOG_DBG expand to logPrintf(), so the tests call
// romconsole::logPrintf() with the level string the macro would have supplied.

// clang-format off
#include <BoardConfig.h>  // the host default (serial transport) first...
#undef FREEINK_LOG_TRANSPORT
#define FREEINK_LOG_TRANSPORT FREEINK_LOG_TRANSPORT_ROM_PRINTF  // ...then override it for the copy below

#include <Logging.h>
#include <esp_rom_sys.h>
// clang-format on

namespace romconsole {
#include <Logging.cpp>  // NOLINT(bugprone-suspicious-include)
}  // namespace romconsole

#include <HostControls.h>
#include <gtest/gtest.h>

#include <string>

namespace {

class LoggingRomTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    clearLastLogs();
    romconsole::clearLastLogs();
  }
};

}  // namespace

TEST_F(LoggingRomTransportTest, LinesGoToTheRomConsoleAndNotToSerial) {
  host::setMillis(77);
  romconsole::logPrintf("ERR", "STK", "boom %d\n", 3);
  EXPECT_EQ(host::romConsoleOutput(), "[77] [ERR] [STK] boom 3\n");
  EXPECT_EQ(host::serialOutput(), "");
}

TEST_F(LoggingRomTransportTest, TheSerialTransportNeverTouchesTheRomConsole) {
  host::setMillis(77);
  LOG_ERR("STK", "boom %d", 3);  // the globally compiled copy, serial transport
  EXPECT_EQ(host::serialOutput(), "[77] [ERR] [STK] boom 3\n");
  EXPECT_EQ(host::romConsoleOutput(), "");
}

TEST_F(LoggingRomTransportTest, RomTransportStillFillsItsOwnRingBuffer) {
  romconsole::logPrintf("INF", "STK", "kept\n");
  EXPECT_EQ(romconsole::getLastLogs(), "[0] [INF] [STK] kept\n");
  EXPECT_EQ(getLastLogs(), "");  // the two copies keep separate rings
}

TEST_F(LoggingRomTransportTest, RomTransportRingKeepsOnlyTheLastSixteenLines) {
  for (int i = 0; i < 18; i++) romconsole::logPrintf("ERR", "R", "L%02d\n", i);
  const std::string ring = romconsole::getLastLogs();
  EXPECT_EQ(ring.find("L00"), std::string::npos);
  EXPECT_EQ(ring.find("L01"), std::string::npos);
  EXPECT_EQ(ring.rfind("[0] [ERR] [R] L02\n", 0), 0u);
  EXPECT_NE(ring.find("[0] [ERR] [R] L17\n"), std::string::npos);
  // Nothing is dropped from the console itself: all 18 lines were printed.
  EXPECT_NE(host::romConsoleOutput().find("[0] [ERR] [R] L00\n"), std::string::npos);
}

TEST_F(LoggingRomTransportTest, RomTransportTruncatesLongLinesToTheEntryCapacity) {
  const std::string big(300, 'x');
  romconsole::logPrintf("ERR", "T", "%s\n", big.c_str());
  EXPECT_EQ(host::romConsoleOutput().size(), 255u);
  EXPECT_EQ(romconsole::getLastLogs().size(), 255u);
}
