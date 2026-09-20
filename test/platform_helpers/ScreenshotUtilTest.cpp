// src/util/ScreenshotUtil: the screenshot path built from the on-screen
// reader's state, and the 1-bit BMP dump of the framebuffer (FR-187).

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HostControls.h>
#include <Logging.h>
#include <ScreenshotUtil.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "TestSupport.h"
#include "activities/Activity.h"

namespace {

using ReaderType = ScreenshotInfo::ReaderType;

class ScreenshotUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    clearLastLogs();
    host::setMillis(4242);
    activityManager.info = ScreenshotInfo{};
  }

  // buildFilename into a buffer wide enough to see anything the function emits.
  std::string build(const ScreenshotInfo& info, size_t bufSize = 256) {
    std::vector<char> buf(bufSize + 8, '\xAB');
    ScreenshotUtil::buildFilename(info, buf.data(), bufSize);
    EXPECT_EQ(buf[bufSize + 4], '\xAB') << "wrote past bufSize";
    return {buf.data()};
  }

  static ScreenshotInfo reader(ReaderType type, const char* title, int spineIndex = -1, int page = 1, int pct = 0) {
    ScreenshotInfo info;
    info.readerType = type;
    snprintf(info.title, sizeof(info.title), "%s", title);
    info.spineIndex = spineIndex;
    info.currentPage = page;
    info.progressPercent = pct;
    return info;
  }

  ScopedStorageRoot storage;
};

// Accepts exactly what SdFat's mbToCp accepts, so a path that fails here is a
// path the card's long-name layer will reject.
bool decodesAsUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    size_t n = 0;
    if ((c & 0x80) == 0) {
      n = 1;
    } else if ((c & 0xE0) == 0xC0) {
      n = 2;
    } else if ((c & 0xF0) == 0xE0) {
      n = 3;
    } else if ((c & 0xF8) == 0xF0) {
      n = 4;
    } else {
      return false;
    }
    if (i + n > s.size()) return false;
    for (size_t k = 1; k < n; k++) {
      if ((static_cast<uint8_t>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += n;
  }
  return true;
}

// 16x8 framebuffer: two bytes per row, eight rows.
std::vector<uint8_t> blankFb() { return std::vector<uint8_t>(16, 0); }

void setFbPixel(std::vector<uint8_t>& fb, int width, int x, int y, bool on) {
  const size_t index = static_cast<size_t>(y) * (width / 8) + static_cast<size_t>(x) / 8;
  const uint8_t mask = static_cast<uint8_t>(1u << (7 - (x % 8)));
  if (on) {
    fb[index] |= mask;
  } else {
    fb[index] = static_cast<uint8_t>(fb[index] & ~mask);
  }
}

}  // namespace

/* ---------- buildFilename ---------- */

TEST_F(ScreenshotUtilTest, NoReaderProducesATimestampOnlyName) {
  EXPECT_EQ(build(ScreenshotInfo{}), "/screenshots/screenshot-4242.bmp");
}

TEST_F(ScreenshotUtilTest, EmptyTitleFallsBackToTheTimestampName) {
  EXPECT_EQ(build(reader(ReaderType::Epub, "", 0, 3, 40)), "/screenshots/screenshot-4242.bmp");
}

TEST_F(ScreenshotUtilTest, TimestampComesFromMillis) {
  host::setMillis(0);
  EXPECT_EQ(build(ScreenshotInfo{}), "/screenshots/screenshot-0.bmp");
  host::setMillis(4294967295u);
  EXPECT_EQ(build(ScreenshotInfo{}), "/screenshots/screenshot-4294967295.bmp");
}

TEST_F(ScreenshotUtilTest, EpubNameCarriesAOneBasedChapterNumber) {
  EXPECT_EQ(build(reader(ReaderType::Epub, "Dune", 0, 7, 12)), "/screenshots/Dune/Dune_ch1_p7_12pct_4242.bmp");
  EXPECT_EQ(build(reader(ReaderType::Epub, "Dune", 11, 2, 99)), "/screenshots/Dune/Dune_ch12_p2_99pct_4242.bmp");
}

TEST_F(ScreenshotUtilTest, EpubWithoutASpineIndexDropsTheChapterSegment) {
  EXPECT_EQ(build(reader(ReaderType::Epub, "Dune", -1, 4, 5)), "/screenshots/Dune/Dune_p4_5pct_4242.bmp");
}

TEST_F(ScreenshotUtilTest, NonEpubReadersNeverGetAChapterSegment) {
  for (auto type : {ReaderType::Txt, ReaderType::Xtc, ReaderType::Fb2}) {
    EXPECT_EQ(build(reader(type, "Book", 3, 9, 50)), "/screenshots/Book/Book_p9_50pct_4242.bmp")
        << static_cast<int>(type);
  }
}

TEST_F(ScreenshotUtilTest, TitleIsSanitisedForFat32InBothPathSegments) {
  EXPECT_EQ(build(reader(ReaderType::Txt, "A/B:C*D?E\"F<G>H|I J", 0, 1, 0)),
            "/screenshots/A-B-C-D-E-F-G-H-I-J/A-B-C-D-E-F-G-H-I-J_p1_0pct_4242.bmp");
}

TEST_F(ScreenshotUtilTest, ControlCharactersInTheTitleBecomeDashes) {
  EXPECT_EQ(build(reader(ReaderType::Txt, "Tab\tNew\nLine", 0, 1, 0)),
            "/screenshots/Tab-New-Line/Tab-New-Line_p1_0pct_4242.bmp");
}

TEST_F(ScreenshotUtilTest, ProgressPercentIsClampedToZeroHundred) {
  EXPECT_NE(build(reader(ReaderType::Txt, "B", 0, 1, -5)).find("_0pct_"), std::string::npos);
  EXPECT_NE(build(reader(ReaderType::Txt, "B", 0, 1, 101)).find("_100pct_"), std::string::npos);
  EXPECT_NE(build(reader(ReaderType::Txt, "B", 0, 1, 100)).find("_100pct_"), std::string::npos);
}

TEST_F(ScreenshotUtilTest, NegativePageNumbersArePassedThroughUnchanged) {
  EXPECT_EQ(build(reader(ReaderType::Txt, "B", 0, -3, 0)), "/screenshots/B/B_p-3_0pct_4242.bmp");
}

TEST_F(ScreenshotUtilTest, LongestPossibleTitleStaysInsideTheFat32PathBudget) {
  // ScreenshotInfo::title is 64 bytes, so the composed path can never reach the
  // 255-byte FAT32 limit the truncation branch below guards against.
  const std::string maxTitle(63, 'T');
  const std::string path = build(reader(ReaderType::Epub, maxTitle.c_str(), 999999, 999999, 100), 512);
  EXPECT_LT(path.size(), 255u);
  EXPECT_EQ(path, "/screenshots/" + maxTitle + "/" + maxTitle + "_ch1000000_p999999_100pct_4242.bmp");
}

// ScreenshotInfo::title is 64 bytes, so any longer title is cut at byte 63 —
// which lands mid-character for most Cyrillic, Greek and CJK titles. The name
// must still decode as UTF-8 or SdFat rejects the mkdir and nothing is saved.
TEST_F(ScreenshotUtilTest, LongMultiByteTitleDoesNotEndMidCharacter) {
  // 35 characters, 66 bytes; byte 62 is the lead byte of a 2-byte character.
  const char* title =
      "\u041c\u0430\u0440\u0441\u0438\u0430\u043d\u0441\u043a\u0438\u0435 "
      "\u0445\u0440\u043e\u043d\u0438\u043a\u0438. "
      "\u041f\u043e\u043b\u043d\u043e\u0435 \u0438\u0437\u0434\u0430\u043d\u0438\u0435";
  const std::string path = build(reader(ReaderType::Fb2, title));
  EXPECT_TRUE(decodesAsUtf8(path)) << path;
  // Still the per-book form, not the timestamp fallback.
  EXPECT_NE(path.find("_p1_0pct_4242.bmp"), std::string::npos) << path;
}

TEST_F(ScreenshotUtilTest, SmallBufferTruncatesInsteadOfOverflowing) {
  EXPECT_EQ(build(reader(ReaderType::Txt, "Book", 0, 1, 0), 20), "/screenshots/Book/B");
  EXPECT_EQ(build(ScreenshotInfo{}, 1), "");
}

/* ---------- saveFramebufferAsBmp ---------- */

TEST_F(ScreenshotUtilTest, RefusesANullFramebuffer) {
  EXPECT_FALSE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", nullptr, 16, 8));
  EXPECT_EQ(Storage.writeOpens, 0);
}

TEST_F(ScreenshotUtilTest, WritesAOneBitBottomUpHeaderForTheRotatedImage) {
  const auto fb = blankFb();
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  const auto bytes = storage.get("/screenshots/a.bmp");
  ASSERT_GE(bytes.size(), sizeof(BmpHeader));

  BmpHeader header;
  memcpy(&header, bytes.data(), sizeof(header));
  // Copied out field by field: BmpHeader is packed, so binding a reference to a
  // member (as EXPECT_EQ does) would be a misaligned access.
  const uint16_t bfType = header.fileHeader.bfType;
  const uint32_t bfSize = header.fileHeader.bfSize;
  const uint32_t bfOffBits = header.fileHeader.bfOffBits;
  const uint32_t biSize = header.infoHeader.biSize;
  const int32_t biWidth = header.infoHeader.biWidth;
  const int32_t biHeight = header.infoHeader.biHeight;
  const uint16_t biPlanes = header.infoHeader.biPlanes;
  const uint16_t biBitCount = header.infoHeader.biBitCount;
  const uint32_t biCompression = header.infoHeader.biCompression;
  const uint32_t biSizeImage = header.infoHeader.biSizeImage;
  const uint32_t biClrUsed = header.infoHeader.biClrUsed;
  const uint8_t black = header.colors[0].rgbBlue;
  const uint8_t white = header.colors[1].rgbBlue;

  EXPECT_EQ(bfType, 0x4D42);
  EXPECT_EQ(bfOffBits, sizeof(BmpHeader));
  EXPECT_EQ(biSize, 40u);
  EXPECT_EQ(biWidth, 8);    // rotated 90 CCW: source height
  EXPECT_EQ(biHeight, 16);  // positive => bottom-up rows
  EXPECT_EQ(biPlanes, 1);
  EXPECT_EQ(biBitCount, 1);
  EXPECT_EQ(biCompression, 0u);
  EXPECT_EQ(biClrUsed, 2u);
  EXPECT_EQ(biSizeImage, 4u * 16u);  // 4-byte padded rows
  EXPECT_EQ(bfSize, sizeof(BmpHeader) + 4u * 16u);
  EXPECT_EQ(bytes.size(), sizeof(BmpHeader) + 4u * 16u);
  EXPECT_EQ(black, 0);
  EXPECT_EQ(white, 255);
}

TEST_F(ScreenshotUtilTest, AllZeroFramebufferProducesAllZeroRows) {
  const auto fb = blankFb();
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  const auto bytes = storage.get("/screenshots/a.bmp");
  for (size_t i = sizeof(BmpHeader); i < bytes.size(); i++) EXPECT_EQ(bytes[i], 0) << i;
}

TEST_F(ScreenshotUtilTest, RotatesNinetyDegreesCounterClockwise) {
  // Source (0,0) is the top-left; a 90 CCW rotation into bottom-up BMP rows
  // sends it to output column 7 of the last written row.
  auto fb = blankFb();
  setFbPixel(fb, 16, 0, 0, true);
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  auto bytes = storage.get("/screenshots/a.bmp");
  for (size_t row = 0; row < 16; row++) {
    const uint8_t b = bytes[sizeof(BmpHeader) + row * 4];
    EXPECT_EQ(b, row == 15 ? 0x01 : 0x00) << row;
  }

  // The opposite corner (15,7) lands in the first written row, top bit.
  fb = blankFb();
  setFbPixel(fb, 16, 15, 7, true);
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/b.bmp", fb.data(), 16, 8));
  bytes = storage.get("/screenshots/b.bmp");
  for (size_t row = 0; row < 16; row++) {
    const uint8_t b = bytes[sizeof(BmpHeader) + row * 4];
    EXPECT_EQ(b, row == 0 ? 0x80 : 0x00) << row;
  }
}

TEST_F(ScreenshotUtilTest, PaddingBytesBeyondTheImageWidthStayZero) {
  auto fb = blankFb();
  for (size_t i = 0; i < fb.size(); i++) fb[i] = 0xFF;
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  const auto bytes = storage.get("/screenshots/a.bmp");
  for (size_t row = 0; row < 16; row++) {
    const size_t base = sizeof(BmpHeader) + row * 4;
    EXPECT_EQ(bytes[base], 0xFF) << row;  // 8 image pixels
    EXPECT_EQ(bytes[base + 1], 0x00) << row;
    EXPECT_EQ(bytes[base + 2], 0x00) << row;
    EXPECT_EQ(bytes[base + 3], 0x00) << row;
  }
}

TEST_F(ScreenshotUtilTest, CreatesTheParentDirectory) {
  const auto fb = blankFb();
  EXPECT_FALSE(storage.exists("/screenshots/Book"));
  ASSERT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/Book/a.bmp", fb.data(), 16, 8));
  EXPECT_TRUE(storage.exists("/screenshots/Book/a.bmp"));
}

TEST_F(ScreenshotUtilTest, RejectsAFramebufferTallerThanTheFixedRowBuffer) {
  // phyWidth == source height; 545 rows need a 72-byte padded row.
  const std::vector<uint8_t> fb(8 * 545, 0);
  EXPECT_FALSE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 8, 545));
  EXPECT_NE(getLastLogs().find("exceeds buffer capacity"), std::string::npos);
  EXPECT_FALSE(storage.exists("/screenshots/a.bmp"));  // the partial file is removed
  EXPECT_TRUE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/b.bmp", fb.data(), 8, 544));
}

TEST_F(ScreenshotUtilTest, OpenFailureIsReportedAndWritesNothing) {
  const auto fb = blankFb();
  Storage.failNextOpen = true;
  EXPECT_FALSE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  EXPECT_FALSE(storage.exists("/screenshots/a.bmp"));
  EXPECT_NE(getLastLogs().find("Failed to save screenshot"), std::string::npos);
}

TEST_F(ScreenshotUtilTest, ShortHeaderWriteRemovesThePartialFile) {
  const auto fb = blankFb();
  Storage.writeCap = 10;
  EXPECT_FALSE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  EXPECT_FALSE(storage.exists("/screenshots/a.bmp"));
}

TEST_F(ScreenshotUtilTest, ShortRowWriteRemovesThePartialFile) {
  const auto fb = blankFb();
  Storage.writeCap = sizeof(BmpHeader) + 2;  // header plus half of the first row
  EXPECT_FALSE(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/a.bmp", fb.data(), 16, 8));
  EXPECT_FALSE(storage.exists("/screenshots/a.bmp"));
}

/* ---------- takeScreenshot ---------- */

TEST_F(ScreenshotUtilTest, TakeScreenshotWithoutAFramebufferOnlyLogs) {
  GfxRenderer renderer;
  ScreenshotUtil::takeScreenshot(renderer);
  EXPECT_NE(getLastLogs().find("Framebuffer not available"), std::string::npos);
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(renderer.displayCalls, 0);
}

TEST_F(ScreenshotUtilTest, TakeScreenshotNamesTheFileFromTheOnScreenReader) {
  auto fb = blankFb();
  GfxRenderer renderer;
  renderer.framebuffer = fb.data();
  renderer.panelWidth = 16;
  renderer.panelHeight = 8;
  renderer.storeBwBufferResult = true;
  activityManager.info = reader(ReaderType::Epub, "My Book", 2, 5, 33);

  ScreenshotUtil::takeScreenshot(renderer);
  EXPECT_EQ(Storage.lastWritePath, "/screenshots/My-Book/My-Book_ch3_p5_33pct_4242.bmp");
  EXPECT_TRUE(storage.exists("/screenshots/My-Book/My-Book_ch3_p5_33pct_4242.bmp"));
  EXPECT_NE(getLastLogs().find("Screenshot saved to /screenshots/My-Book/"), std::string::npos);
  EXPECT_EQ(renderer.displayCalls, 2);  // border flash, then restore
  EXPECT_EQ(host::delayedMs(), 1000u);
}

TEST_F(ScreenshotUtilTest, TakeScreenshotSkipsTheBorderFlashWhenNoScratchBufferIsFree) {
  auto fb = blankFb();
  GfxRenderer renderer;
  renderer.framebuffer = fb.data();
  renderer.panelWidth = 16;
  renderer.panelHeight = 8;
  renderer.storeBwBufferResult = false;

  ScreenshotUtil::takeScreenshot(renderer);
  EXPECT_EQ(Storage.lastWritePath, "/screenshots/screenshot-4242.bmp");
  EXPECT_EQ(renderer.displayCalls, 0);
  EXPECT_EQ(host::delayedMs(), 0u);
}

TEST_F(ScreenshotUtilTest, TakeScreenshotStopsWhenTheSaveFails) {
  auto fb = blankFb();
  GfxRenderer renderer;
  renderer.framebuffer = fb.data();
  renderer.panelWidth = 16;
  renderer.panelHeight = 8;
  renderer.storeBwBufferResult = true;
  Storage.failNextOpen = true;

  ScreenshotUtil::takeScreenshot(renderer);
  EXPECT_EQ(renderer.displayCalls, 0);
  EXPECT_EQ(host::delayedMs(), 0u);
}
