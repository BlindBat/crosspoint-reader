// The combined X4/X3 ESP32-C3 image is its own compatibility class, tagged
// "x4" and served by the PLAIN firmware.bin release asset (the isX4 special
// case in OtaUpdater::checkForUpdate). The main suite binary is built as
// x4pro; to also pin the x4 naming in the same binary, this TU compiles a
// second copy of FirmwareBoardTag.cpp + OtaUpdater.cpp with FREEINK_DEVICE_X4
// inside a wrapping namespace.
//
// Mechanics: every header those two .cpp files pull in is included FIRST,
// outside the namespace, so the include guards make the in-namespace
// re-includes no-ops — the wrapped code shares ::HttpDownloader (the fake),
// ::firmware_flash, the esp stubs and the std library, while its OtaUpdater
// and board_tag symbols land in x4build:: without colliding with the x4pro
// TUs. Only OtaUpdater.h and FirmwareBoardTag.h are deliberately NOT
// pre-included: they must declare fresh types inside the namespace.

#undef FREEINK_DEVICE_X4PRO
#define FREEINK_DEVICE_X4 1

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

namespace x4build {
#include "network/FirmwareBoardTag.cpp"  // NOLINT(bugprone-suspicious-include)
#include "network/OtaUpdater.cpp"        // NOLINT(bugprone-suspicious-include)
}  // namespace x4build

namespace {

class X4VariantTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FakeHttp::instance().reset();
    EspOtaRecorder::instance().reset();
    g_testChipId = 0x0005;
    g_testCurrentVersion = "1.6.0";
  }
};

TEST_F(X4VariantTest, CombinedC3BinaryIsTaggedX4) {
  EXPECT_STREQ(x4build::board_tag::TAG, "CROSSPOINT-BOARD-V1:x4;");
  EXPECT_EQ(x4build::board_tag::boardNameLen(), 2u);
  EXPECT_EQ(std::string(x4build::board_tag::boardName(), x4build::board_tag::boardNameLen()), "x4");
}

TEST_F(X4VariantTest, X4SelectsPlainFirmwareBin) {
  // The pre-existing release layout: plain firmware.bin for the C3 binary,
  // firmware-<board>.bin for everything else. x4 must take the plain asset
  // and ignore the board-suffixed ones.
  x4build::OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 5555},
                                {"firmware.bin", "https://cdn.example/c3.bin", 4444},
                                {"firmware-sticky.bin", "https://cdn.example/sticky.bin", 6666}}));
  ASSERT_EQ(updater.checkForUpdate(), x4build::OtaUpdater::OK);
  EXPECT_EQ(updater.getLatestVersion(), "1.7.0");
  EXPECT_EQ(updater.getOtaSize(), 4444u);  // proves firmware.bin won
}

TEST_F(X4VariantTest, X4WithoutPlainAssetSeesNoUpdate) {
  x4build::OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 5555},
                                {"firmware-x4.bin", "https://cdn.example/suffixed.bin", 7777}}));
  // Even a firmware-x4.bin asset is not what the x4 build asks for.
  EXPECT_EQ(updater.checkForUpdate(), x4build::OtaUpdater::NO_UPDATE);
}

TEST_F(X4VariantTest, X4ScannerRejectsX4proTag) {
  x4build::board_tag::Scanner scanner;
  const std::string data = "CROSSPOINT-BOARD-V1:x4pro;";
  scanner.feed(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  EXPECT_TRUE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "x4pro");
}

TEST_F(X4VariantTest, X4ScannerRejectsSameLengthForeignName) {
  // "m5" is a real board name exactly as long as "x4"; content, not just
  // length, must decide. "x5" additionally differs only in the last byte.
  for (const char* data : {"CROSSPOINT-BOARD-V1:m5;", "CROSSPOINT-BOARD-V1:x5;"}) {
    x4build::board_tag::Scanner scanner;
    scanner.feed(reinterpret_cast<const uint8_t*>(data), std::strlen(data));
    EXPECT_TRUE(scanner.mismatch()) << data;
  }
}

TEST_F(X4VariantTest, X4ScannerAcceptsOwnTag) {
  x4build::board_tag::Scanner scanner;
  const std::string data = "...CROSSPOINT-BOARD-V1:x4;...";
  scanner.feed(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  EXPECT_FALSE(scanner.mismatch());
}

}  // namespace
