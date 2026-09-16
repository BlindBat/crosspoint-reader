// The sticky build is its own compatibility class, tagged "sticky" and served by
// the firmware-sticky.bin release asset (legacy layout) or crosspoint-<tag>-sticky.bin
// (current upstream layout). The main suite binary is built as x4pro; this TU
// compiles the same production sources again as sticky inside a namespace, using
// the mechanics documented in X4VariantTest.cpp.

#undef FREEINK_DEVICE_X4PRO
#define FREEINK_DEVICE_STICKY 1

// clang-format off
#include <BoardConfig.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"
// clang-format on

#include <gtest/gtest.h>

#include "FakeHttp.h"

namespace stickybuild {
#include "network/FirmwareBoardTag.cpp"  // NOLINT(bugprone-suspicious-include)
#include "network/OtaUpdater.cpp"        // NOLINT(bugprone-suspicious-include)
}  // namespace stickybuild

namespace {

class StickyVariantTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FakeHttp::instance().reset();
    EspOtaRecorder::instance().reset();
    g_testChipId = 0x0009;  // ESP32-S3
    g_testCurrentVersion = "1.6.0";
  }
};

TEST_F(StickyVariantTest, BinaryIsTaggedSticky) {
  EXPECT_STREQ(stickybuild::board_tag::TAG, "CROSSPOINT-BOARD-V1:sticky;");
  EXPECT_EQ(std::string(stickybuild::board_tag::boardName(), stickybuild::board_tag::boardNameLen()), "sticky");
}

TEST_F(StickyVariantTest, SelectsLegacyBoardSuffixedAsset) {
  stickybuild::OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"firmware.bin", "https://cdn.example/c3.bin", 1111},
                                {"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 2222},
                                {"firmware-sticky.bin", "https://cdn.example/sticky.bin", 3333}}));
  ASSERT_EQ(updater.checkForUpdate(), stickybuild::OtaUpdater::OK);
  EXPECT_EQ(updater.getOtaSize(), 3333u);
}

TEST_F(StickyVariantTest, SelectsTaggedUpstreamAsset) {
  stickybuild::OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"crosspoint-1.7.0-x3-x4.bin", "https://cdn.example/c3.bin", 1111},
                                {"crosspoint-1.7.0-sticky.bin", "https://cdn.example/sticky.bin", 3333}}));
  ASSERT_EQ(updater.checkForUpdate(), stickybuild::OtaUpdater::OK);
  EXPECT_EQ(updater.getOtaSize(), 3333u);
}

TEST_F(StickyVariantTest, PlainFirmwareBinIsNotAFallback) {
  stickybuild::OtaUpdater updater;
  FakeHttp::instance().setBody(makeReleaseJson("1.7.0", {{"firmware.bin", "https://cdn.example/c3.bin", 1111}}));
  EXPECT_EQ(updater.checkForUpdate(), stickybuild::OtaUpdater::NO_UPDATE);
}

TEST_F(StickyVariantTest, ScannerRejectsForeignTagAndAcceptsOwn) {
  stickybuild::board_tag::Scanner foreign;
  const std::string other = "CROSSPOINT-BOARD-V1:x4pro;";
  foreign.feed(reinterpret_cast<const uint8_t*>(other.data()), other.size());
  EXPECT_TRUE(foreign.mismatch());
  EXPECT_STREQ(foreign.foundName(), "x4pro");

  stickybuild::board_tag::Scanner own;
  const std::string mine = "...CROSSPOINT-BOARD-V1:sticky;...";
  own.feed(reinterpret_cast<const uint8_t*>(mine.data()), mine.size());
  EXPECT_FALSE(own.mismatch());
}

}  // namespace
