#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformHost.h"
#include "Xtc.h"
#include "XtcFixture.h"
#include "XtcReaderMath.h"

// Cover and thumbnail generation for XTC/XTCH (FR-102): page 0 becomes a
// top-down BMP (1-bit for XTC, 4-grey palette for XTCH); thumbnails are
// 1-bit, area-averaged and dithered, and never upscaled. Containers come off
// the SD card untrusted, so lying page dimensions, truncated planes and
// zero/oversize pages must fail cleanly without leaving a partial BMP.

namespace {

using xtcfix::Bmp;
using xtcfix::page;
using xtcfix::TempDir;
using xtcfix::XtcSpec;

constexpr uint16_t kFullW = 480;
constexpr uint16_t kFullH = 800;

struct Book {
  TempDir tmp;
  std::string path;
  std::unique_ptr<Xtc> xtc;

  explicit Book(const XtcSpec& spec, const char* name = "book.xtc") {
    path = tmp.path() + "/" + name;
    EXPECT_TRUE(xtcfix::writeFile(path, xtcfix::buildXtc(spec)));
    xtc.reset(new Xtc(path, tmp.path()));
  }
};

XtcSpec oneBit(uint16_t w, uint16_t h, const std::function<bool(int, int)>& white) {
  XtcSpec spec;
  spec.pages.push_back(page(w, h, xtcfix::xtgBitmap(w, h, white)));
  return spec;
}

XtcSpec twoBit(uint16_t w, uint16_t h, const std::function<int(int, int)>& value) {
  XtcSpec spec;
  spec.twoBit = true;
  spec.pages.push_back(page(w, h, xtcfix::xthBitmap(w, h, value)));
  return spec;
}

// Collects streamed chunks behind an xtc::PageChunkFn (function pointer + context).
struct ChunkSink {
  std::vector<uint8_t> assembled;
  size_t expectedOffset = 0;
  bool offsetsContiguous = true;

  xtc::PageChunkFn fn() { return {&ChunkSink::append, this}; }

  static void append(void* ctx, const uint8_t* data, const size_t size, const size_t offset) {
    auto* self = static_cast<ChunkSink*>(ctx);
    if (offset != self->expectedOffset) self->offsetsContiguous = false;
    self->assembled.insert(self->assembled.end(), data, data + size);
    self->expectedOffset += size;
  }
};

bool loadCover(const Xtc& xtc, Bmp& bmp) { return xtcfix::parseBmp(xtcfix::readFile(xtc.getCoverBmpPath()), bmp); }
bool loadThumb(const Xtc& xtc, int height, Bmp& bmp) {
  return xtcfix::parseBmp(xtcfix::readFile(xtc.getThumbBmpPath(height)), bmp);
}

int countWhite(const Bmp& bmp) {
  int white = 0;
  for (int y = 0; y < bmp.absHeight(); ++y)
    for (int x = 0; x < bmp.width; ++x) white += bmp.pixel(x, y) == 1;
  return white;
}

}  // namespace

// ---------------------------------------------------------------- loading

TEST(XtcBook, LoadExposesMetadataAndPageGeometry) {
  Book book(oneBit(16, 4, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  EXPECT_TRUE(book.xtc->isLoaded());
  EXPECT_EQ(book.xtc->getTitle(), "Fixture Book");
  EXPECT_EQ(book.xtc->getAuthor(), "QA");
  EXPECT_EQ(book.xtc->getPageCount(), 1u);
  EXPECT_EQ(book.xtc->getPageWidth(), 16);
  EXPECT_EQ(book.xtc->getPageHeight(), 4);
  EXPECT_EQ(book.xtc->getBitDepth(), 1);
  EXPECT_FALSE(book.xtc->hasChapters());
  EXPECT_TRUE(book.xtc->getChapters().empty());
}

TEST(XtcBook, XtchReportsTwoBitDepth) {
  Book book(twoBit(16, 8, [](int, int) { return 0; }));
  ASSERT_TRUE(book.xtc->load());
  EXPECT_EQ(book.xtc->getBitDepth(), 2);
}

TEST(XtcBook, UnloadedBookHasSafeDefaults) {
  TempDir tmp;
  Xtc xtc(tmp.path() + "/missing.xtc", tmp.path());
  EXPECT_FALSE(xtc.load());
  EXPECT_FALSE(xtc.isLoaded());
  EXPECT_EQ(xtc.getPageCount(), 0u);
  EXPECT_EQ(xtc.getPageWidth(), 0);
  EXPECT_EQ(xtc.getBitDepth(), 1);
  EXPECT_EQ(xtc.getTitle(), "");
  EXPECT_EQ(xtc.calculateProgress(0), 0);
  EXPECT_EQ(xtc.getLastError(), xtc::XtcError::FILE_NOT_FOUND);
  uint8_t buf[8];
  EXPECT_EQ(xtc.loadPage(0, buf, sizeof(buf)), 0u);
  EXPECT_FALSE(xtc.generateCoverBmp());
  EXPECT_FALSE(xtc.generateThumbBmp(100));
}

TEST(XtcBook, TitleFallsBackToFileNameWithoutExtension) {
  XtcSpec spec = oneBit(8, 1, [](int, int) { return true; });
  spec.metadata = false;
  Book book(spec, "My Book.xtc");
  ASSERT_TRUE(book.xtc->load());
  EXPECT_EQ(book.xtc->getTitle(), "My Book");
  EXPECT_EQ(book.xtc->getAuthor(), "");
}

TEST(XtcBook, TitleFallbackIgnoresDotInDirectoryName) {
  TempDir tmp;
  const std::string dir = tmp.path() + "/d.ir";
  ASSERT_EQ(::mkdir(dir.c_str(), 0755), 0);
  XtcSpec spec = oneBit(8, 1, [](int, int) { return true; });
  spec.metadata = false;
  const std::string path = dir + "/book";
  ASSERT_TRUE(xtcfix::writeFile(path, xtcfix::buildXtc(spec)));
  Xtc xtc(path, tmp.path());
  ASSERT_TRUE(xtc.load());
  EXPECT_EQ(xtc.getTitle(), "book");
}

TEST(XtcBook, CachePathIsXtcPrefixedHashUnderCacheDir) {
  TempDir tmp;
  const std::string path = tmp.path() + "/a.xtc";
  Xtc xtc(path, tmp.path());
  const std::string expected = tmp.path() + "/xtc_" + std::to_string(std::hash<std::string>{}(path));
  EXPECT_EQ(xtc.getCachePath(), expected);
  EXPECT_EQ(xtc.getCoverBmpPath(), expected + "/cover.bmp");
  EXPECT_EQ(xtc.getThumbBmpPath(), expected + "/thumb_[HEIGHT].bmp");
  EXPECT_EQ(xtc.getThumbBmpPath(120), expected + "/thumb_120.bmp");
}

TEST(XtcBook, ProgressPercentIsOneBasedPageOverCount) {
  XtcSpec spec;
  for (int i = 0; i < 4; ++i) spec.pages.push_back(page(8, 1, {0xFF}));
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_EQ(book.xtc->calculateProgress(0), 25);
  EXPECT_EQ(book.xtc->calculateProgress(1), 50);
  EXPECT_EQ(book.xtc->calculateProgress(3), 100);
}

TEST(XtcBook, SetupAndClearCacheRoundTrip) {
  Book book(oneBit(8, 1, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  EXPECT_TRUE(book.xtc->clearCache());  // nothing there yet
  book.xtc->setupCacheDir();
  EXPECT_TRUE(xtcfix::fileExists(book.xtc->getCachePath()));
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  EXPECT_TRUE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
  EXPECT_TRUE(book.xtc->clearCache());
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCachePath()));
}

// ---------------------------------------------------------------- 1-bit cover

TEST(XtcCover, OneBitCoverIsTopDownOneBitBmpWithPaddedRows) {
  // 20 px wide: 3 source bytes per row, BMP rows padded to 4.
  Book book(oneBit(20, 3, [](int x, int) { return x % 2 == 0; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());

  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.width, 20);
  EXPECT_EQ(bmp.heightRaw, -3);
  EXPECT_EQ(bmp.offBits, 62u);
  EXPECT_EQ(bmp.colorsUsed, 2u);
  ASSERT_EQ(bmp.palette.size(), 2u);
  EXPECT_EQ(bmp.palette[0], 0x000000u);
  EXPECT_EQ(bmp.palette[1], 0xFFFFFFu);
  EXPECT_EQ(bmp.rowBytes, 4u);
  for (int y = 0; y < 3; ++y) {
    EXPECT_EQ(bmp.row(y)[3], 0) << "padding byte row " << y;
    for (int x = 0; x < 20; ++x) EXPECT_EQ(bmp.pixel(x, y), x % 2 == 0 ? 1 : 0) << x << "," << y;
  }
}

TEST(XtcCover, OneBitCoverKeepsSourcePolarity) {
  // Source 0 = black, 1 = white; the BMP palette has the same order.
  Book book(oneBit(8, 2, [](int, int y) { return y == 1; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.row(0)[0], 0x00);
  EXPECT_EQ(bmp.row(1)[0], 0xFF);
}

// ---------------------------------------------------------------- 2-bit cover

TEST(XtcCover, TwoBitCoverWritesFourGreyPaletteBmp) {
  Book book(twoBit(8, 8, [](int, int) { return 0; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());

  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.bitCount, 2);
  EXPECT_EQ(bmp.offBits, 70u);
  EXPECT_EQ(bmp.width, 8);
  EXPECT_EQ(bmp.heightRaw, -8);
  ASSERT_EQ(bmp.palette.size(), 4u);
  EXPECT_EQ(bmp.palette[0], 0x000000u);
  EXPECT_EQ(bmp.palette[1], 0x555555u);
  EXPECT_EQ(bmp.palette[2], 0xAAAAAAu);
  EXPECT_EQ(bmp.palette[3], 0xFFFFFFu);
  EXPECT_EQ(bmp.imageSize, 4u * 8u);
}

TEST(XtcCover, TwoBitCoverMapsXthValuesToPaletteIndices) {
  // XTH 0=white,1=dark,2=light,3=black -> palette 3,1,2,0. A (x+y)%4 pattern
  // hits every value and every column/row parity of the column-major planes;
  // 13 px is an odd width so the 2-bit rows also need padding.
  constexpr uint8_t kXthToBmp[4] = {3, 1, 2, 0};
  ASSERT_TRUE(xtcfix::xthLayoutIsAddressable(13, 16));
  Book book(twoBit(13, 16, [](int x, int y) { return (x + y) % 4; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.rowBytes, 4u);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 13; ++x) EXPECT_EQ(bmp.pixel(x, y), kXthToBmp[(x + y) % 4]) << x << "," << y;
}

TEST(XtcCover, TwoBitCoverRowsPaddedToFourBytesWithZeroTail) {
  // 5 px * 2 bits = 10 bits: bits past the image and the pad bytes are zero.
  Book book(twoBit(5, 8, [](int, int) { return 0; }));  // white -> index 3 everywhere
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.rowBytes, 4u);
  for (int y = 0; y < 8; ++y) {
    EXPECT_EQ(bmp.row(y)[0], 0xFF);
    EXPECT_EQ(bmp.row(y)[1], 0xC0);
    EXPECT_EQ(bmp.row(y)[2], 0x00);
    EXPECT_EQ(bmp.row(y)[3], 0x00);
  }
}

TEST(XtcCover, ExistingCoverIsNotRegenerated) {
  Book book(oneBit(8, 1, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  book.xtc->setupCacheDir();
  ASSERT_TRUE(xtcfix::writeFile(book.xtc->getCoverBmpPath(), {'X', 'X'}));
  EXPECT_TRUE(book.xtc->generateCoverBmp());
  EXPECT_EQ(xtcfix::readFile(book.xtc->getCoverBmpPath()), (std::vector<uint8_t>{'X', 'X'}));
}

// ---------------------------------------------------------------- malformed pages

TEST(XtcCover, TruncatedXthPlaneRefusesCoverAndLeavesNoFile) {
  XtcSpec spec = twoBit(16, 8, [](int, int) { return 3; });
  spec.truncateTail = 5;  // second plane cut short
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());  // header and page table are intact
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::READ_ERROR);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

TEST(XtcCover, PageHeaderClaimingLargerDimsThanTableIsRefused) {
  XtcSpec spec;
  xtcfix::PageSpec p = page(16, 4, xtcfix::xtgBitmap(16, 4, [](int, int) { return true; }));
  p.headerWidth = 32;
  p.headerHeight = 8;
  spec.pages.push_back(p);
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::MEMORY_ERROR);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

TEST(XtcCover, PageHeaderClaimingSmallerDimsYieldsTableSizedCover) {
  // The cover is sized from the page table while the bitmap read is sized from
  // the page header, so a shorter page still produces a table-sized BMP.
  XtcSpec spec;
  xtcfix::PageSpec p = page(16, 8, xtcfix::xtgBitmap(16, 4, [](int, int) { return true; }));
  p.headerHeight = 4;
  spec.pages.push_back(p);
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.width, 16);
  EXPECT_EQ(bmp.heightRaw, -8);
  for (int y = 0; y < 4; ++y) EXPECT_EQ(bmp.row(y)[0], 0xFF);
}

TEST(XtcCover, ZeroSizedPageProducesNoCoverOrThumb) {
  XtcSpec spec;
  spec.pages.push_back(page(0, 0, {}));
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
  EXPECT_FALSE(book.xtc->generateThumbBmp(100));
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getThumbBmpPath(100)));
}

TEST(XtcCover, OversizePageWithShortDataIsRefused) {
  XtcSpec spec;
  spec.pages.push_back(page(4096, 4096, std::vector<uint8_t>(100, 0xFF)));
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::READ_ERROR);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
  EXPECT_FALSE(book.xtc->generateThumbBmp(100));
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getThumbBmpPath(100)));
}

// ---------------------------------------------------------------- thumbnails

TEST(XtcThumb, PageNoLargerThanTargetCopiesCoverUnchanged) {
  Book book(oneBit(16, 4, [](int x, int) { return x < 8; }));
  ASSERT_TRUE(book.xtc->load());
  platform_host::resetCounters();
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  EXPECT_EQ(platform_host::yieldCount(), 0u);
  const auto cover = xtcfix::readFile(book.xtc->getCoverBmpPath());
  const auto thumb = xtcfix::readFile(book.xtc->getThumbBmpPath(100));
  ASSERT_FALSE(cover.empty());
  EXPECT_EQ(thumb, cover);
  Bmp bmp;
  ASSERT_TRUE(xtcfix::parseBmp(thumb, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.width, 16);
}

TEST(XtcThumb, XtchNoUpscaleThumbIsTheFourGreyCover) {
  Book book(twoBit(16, 8, [](int x, int) { return x % 4; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  const auto thumb = xtcfix::readFile(book.xtc->getThumbBmpPath(100));
  EXPECT_EQ(thumb, xtcfix::readFile(book.xtc->getCoverBmpPath()));
  Bmp bmp;
  ASSERT_TRUE(xtcfix::parseBmp(thumb, bmp));
  EXPECT_EQ(bmp.bitCount, 2);
  EXPECT_EQ(bmp.palette.size(), 4u);
}

TEST(XtcThumb, ScalesFullPageDownToTargetHeightAsOneBit) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.width, 60);
  EXPECT_EQ(bmp.heightRaw, -100);
  EXPECT_EQ(bmp.offBits, 62u);
  EXPECT_EQ(countWhite(bmp), 60 * 100);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));  // scaled path never needs cover.bmp
}

TEST(XtcThumb, AllBlackPageStaysBlack) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return false; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(countWhite(bmp), 0);
}

TEST(XtcThumb, WiderPageIsScaledByHeightSoTheCardCanCrop) {
  // 1000x800 -> target 60x100: the larger scale (height) wins, width overflows.
  Book book(oneBit(1000, 800, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(bmp.width, 125);
  EXPECT_EQ(bmp.heightRaw, -100);
}

TEST(XtcThumb, AreaAveragingKeepsHalvesSeparated) {
  Book book(oneBit(kFullW, kFullH, [](int x, int) { return x >= kFullW / 2; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 60; ++x) EXPECT_EQ(bmp.pixel(x, y), x < 30 ? 0 : 1) << x << "," << y;
  }
}

TEST(XtcThumb, SourceRowStrideThatIsNotAMultipleOfFourIsIndexedCorrectly) {
  // 200 px = 25 source bytes per row, so the source stride is not a BMP stride;
  // 200x400 -> scale 0.3 -> 60x120. Columns well inside each half stay clean.
  Book book(oneBit(200, 400, [](int x, int) { return x >= 100; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(bmp.width, 60);
  EXPECT_EQ(bmp.heightRaw, -120);
  for (int y = 0; y < 120; ++y) {
    for (int x = 0; x <= 28; ++x) EXPECT_EQ(bmp.pixel(x, y), 0) << x << "," << y;
    for (int x = 32; x < 60; ++x) EXPECT_EQ(bmp.pixel(x, y), 1) << x << "," << y;
  }
}

TEST(XtcThumb, DarkGreyDithersMostlyBlackAndLightGreyMostlyWhite) {
  // Threshold range is 64..192, so dark grey (85) passes ~17% of the time and
  // light grey (170) ~84%; the exact hash pattern is not pinned.
  Book dark(twoBit(kFullW, kFullH, [](int, int) { return 1; }));
  ASSERT_TRUE(dark.xtc->load());
  ASSERT_TRUE(dark.xtc->generateThumbBmp(100));
  Bmp darkBmp;
  ASSERT_TRUE(loadThumb(*dark.xtc, 100, darkBmp));
  const int darkWhite = countWhite(darkBmp);
  EXPECT_GT(darkWhite, 6000 * 5 / 100);
  EXPECT_LT(darkWhite, 6000 * 30 / 100);

  Book light(twoBit(kFullW, kFullH, [](int, int) { return 2; }));
  ASSERT_TRUE(light.xtc->load());
  ASSERT_TRUE(light.xtc->generateThumbBmp(100));
  Bmp lightBmp;
  ASSERT_TRUE(loadThumb(*light.xtc, 100, lightBmp));
  const int lightWhite = countWhite(lightBmp);
  EXPECT_GT(lightWhite, 6000 * 70 / 100);
  EXPECT_LT(lightWhite, 6000 * 95 / 100);
  EXPECT_LT(darkWhite, lightWhite);
}

TEST(XtcThumb, XthBlackAndWhiteExtremesAreExact) {
  Book book(twoBit(kFullW, kFullH, [](int x, int) { return x < kFullW / 2 ? 3 : 0; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 60; ++x) EXPECT_EQ(bmp.pixel(x, y), x < 30 ? 0 : 1) << x << "," << y;
}

TEST(XtcThumb, YieldsToSchedulerEveryEightRows) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  platform_host::resetCounters();
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  EXPECT_EQ(platform_host::yieldCount(), 100u / 8u);
}

TEST(XtcThumb, ExistingThumbIsNotRegenerated) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  book.xtc->setupCacheDir();
  ASSERT_TRUE(xtcfix::writeFile(book.xtc->getThumbBmpPath(100), {'T'}));
  EXPECT_TRUE(book.xtc->generateThumbBmp(100));
  EXPECT_EQ(xtcfix::readFile(book.xtc->getThumbBmpPath(100)), (std::vector<uint8_t>{'T'}));
}

TEST(XtcThumb, DifferentHeightsAreCachedSeparately) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  ASSERT_TRUE(book.xtc->generateThumbBmp(200));
  Bmp a, b;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, a));
  ASSERT_TRUE(loadThumb(*book.xtc, 200, b));
  EXPECT_EQ(a.width, 60);
  EXPECT_EQ(b.width, 120);
  EXPECT_EQ(b.heightRaw, -200);
}

TEST(XtcThumb, TruncatedPageRefusesThumbAndLeavesNoFile) {
  XtcSpec spec = oneBit(kFullW, kFullH, [](int, int) { return true; });
  spec.truncateTail = 1;
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateThumbBmp(100));
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getThumbBmpPath(100)));
}

// ---------------------------------------------------------------- page access

TEST(XtcBook, ChaptersAreExposedThroughTheWrapper) {
  XtcSpec spec;
  for (int i = 0; i < 6; ++i) spec.pages.push_back(page(8, 1, {0xFF}));
  spec.chapters = {{"Intro", 1, 2}, {"Middle", 3, 4}, {"End", 5, 6}};
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_TRUE(book.xtc->hasChapters());
  const auto& chapters = book.xtc->getChapters();
  ASSERT_EQ(chapters.size(), 3u);
  EXPECT_EQ(chapters[0].name, "Intro");
  EXPECT_EQ(chapters[0].startPage, 0u);  // stored 1-based, exposed 0-based
  EXPECT_EQ(chapters[2].endPage, 5u);
  // The chapter-selection row the reader opens on comes from the same vector.
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(chapters, 0), 0);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(chapters, 3), 1);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(chapters, 5), 2);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(chapters, 6), 0);  // past the end
}

TEST(XtcBook, GarbageAndEmptyContainersFailToLoad) {
  TempDir tmp;
  const std::string garbage = tmp.path() + "/garbage.xtc";
  ASSERT_TRUE(xtcfix::writeFile(garbage, std::vector<uint8_t>(200, 0x5A)));
  Xtc bad(garbage, tmp.path());
  EXPECT_FALSE(bad.load());
  // load() drops the parser on failure, so the specific open error is gone.
  EXPECT_EQ(bad.getLastError(), xtc::XtcError::FILE_NOT_FOUND);

  const std::string empty = tmp.path() + "/empty.xtc";
  ASSERT_TRUE(xtcfix::writeFile(empty, {}));
  Xtc none(empty, tmp.path());
  EXPECT_FALSE(none.load());
  EXPECT_FALSE(none.generateCoverBmp());
}

TEST(XtcBook, LoadPageFillsTheBufferAndRefusesSmallerOnes) {
  Book book(oneBit(16, 4, [](int x, int) { return x < 8; }));
  ASSERT_TRUE(book.xtc->load());
  uint8_t exact[8] = {};
  EXPECT_EQ(book.xtc->loadPage(0, exact, sizeof(exact)), 8u);
  for (int y = 0; y < 4; ++y) {
    EXPECT_EQ(exact[y * 2], 0xFF);
    EXPECT_EQ(exact[y * 2 + 1], 0x00);
  }
  uint8_t small[7] = {};
  EXPECT_EQ(book.xtc->loadPage(0, small, sizeof(small)), 0u);
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::MEMORY_ERROR);
}

TEST(XtcBook, LoadPageRejectsAnIndexPastTheLastPage) {
  Book book(oneBit(16, 4, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  uint8_t buf[8] = {};
  EXPECT_EQ(book.xtc->loadPage(1, buf, sizeof(buf)), 0u);
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::PAGE_OUT_OF_RANGE);
}

TEST(XtcBook, StreamingLoadDeliversTheWholeBitmapInOrder) {
  Book book(oneBit(32, 8, [](int x, int y) { return (x + y) % 3 == 0; }));
  ASSERT_TRUE(book.xtc->load());
  ChunkSink sink;
  const auto err = book.xtc->loadPageStreaming(0, sink.fn(), 16);
  EXPECT_EQ(err, xtc::XtcError::OK);
  EXPECT_TRUE(sink.offsetsContiguous);
  const std::vector<uint8_t>& streamed = sink.assembled;
  std::vector<uint8_t> direct(32);
  ASSERT_EQ(book.xtc->loadPage(0, direct.data(), direct.size()), direct.size());
  EXPECT_EQ(streamed, direct);
}

TEST(XtcCover, PageMagicMustMatchTheContainerBitDepth) {
  XtcSpec spec = oneBit(16, 8, [](int, int) { return true; });
  spec.pageMagic = xtc::XTH_MAGIC;  // 2-bit page inside a 1-bit container
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

// ---------------------------------------------------------------- row padding

TEST(XtcCover, WidthMultipleOfThirtyTwoNeedsNoRowPadding) {
  Book book(oneBit(32, 2, [](int x, int) { return x < 16; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.rowBytes, 4u);
  for (int y = 0; y < 2; ++y) {
    EXPECT_EQ(bmp.row(y)[0], 0xFF);
    EXPECT_EQ(bmp.row(y)[1], 0xFF);
    EXPECT_EQ(bmp.row(y)[2], 0x00);
    EXPECT_EQ(bmp.row(y)[3], 0x00);
  }
}

TEST(XtcCover, NarrowPageIsPaddedWithZeroBytes) {
  // 3 px: one source byte per row, three zero pad bytes.
  Book book(oneBit(3, 2, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.width, 3);
  EXPECT_EQ(bmp.rowBytes, 4u);
  for (int y = 0; y < 2; ++y) {
    EXPECT_EQ(bmp.row(y)[0], 0xE0);  // three white pixels, five unused bits
    EXPECT_EQ(bmp.row(y)[1], 0x00);
    EXPECT_EQ(bmp.row(y)[2], 0x00);
    EXPECT_EQ(bmp.row(y)[3], 0x00);
  }
}

TEST(XtcCover, TwoBitWidthMultipleOfSixteenNeedsNoRowPadding) {
  Book book(twoBit(16, 8, [](int x, int) { return x < 8 ? 3 : 0; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.rowBytes, 4u);  // 16 px * 2 bits = 32 bits exactly
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) EXPECT_EQ(bmp.pixel(x, y), x < 8 ? 0 : 3) << x << "," << y;
}

TEST(XtcCover, CoverIsRebuiltAfterTheCacheIsCleared) {
  Book book(oneBit(16, 8, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  const auto first = xtcfix::readFile(book.xtc->getCoverBmpPath());
  ASSERT_TRUE(book.xtc->clearCache());
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  EXPECT_EQ(xtcfix::readFile(book.xtc->getCoverBmpPath()), first);
}

TEST(XtcCover, PageDataCutAwayEntirelyIsRefused) {
  // The header and page table survive but the file ends where page 0 should
  // start, so nothing at all can be read back for the cover.
  XtcSpec spec = oneBit(16, 8, [](int, int) { return true; });
  spec.truncateTail = sizeof(xtc::XtgPageHeader) + 2 * 8;  // whole page payload
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

TEST(XtcCover, TruncatedXtgBitmapRefusesCoverAndLeavesNoFile) {
  XtcSpec spec = oneBit(64, 16, [](int, int) { return true; });
  spec.truncateTail = 3;
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateCoverBmp());
  EXPECT_EQ(book.xtc->getLastError(), xtc::XtcError::READ_ERROR);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

// ---------------------------------------------------------------- more thumbnails

TEST(XtcThumb, ScaleOfExactlyOneCopiesTheCoverInsteadOfScaling) {
  // 60x100 against a 60x100 target: both scales are 1.0, so the copy path wins.
  Book book(oneBit(60, 100, [](int x, int) { return x < 30; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  EXPECT_EQ(xtcfix::readFile(book.xtc->getThumbBmpPath(100)), xtcfix::readFile(book.xtc->getCoverBmpPath()));
}

TEST(XtcThumb, TargetIsSixtyPercentOfTheRequestedHeight) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(50));  // target 30x50, scale 0.0625
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 50, bmp));
  EXPECT_EQ(bmp.width, 30);
  EXPECT_EQ(bmp.heightRaw, -50);
  EXPECT_EQ(bmp.rowBytes, 4u);
}

TEST(XtcThumb, ScaledXtchThumbIsOneBit) {
  Book book(twoBit(kFullW, kFullH, [](int x, int) { return x < kFullW / 2 ? 3 : 0; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.offBits, 62u);
  EXPECT_EQ(bmp.palette.size(), 2u);
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getCoverBmpPath()));
}

TEST(XtcThumb, XthHeightNotAMultipleOfEightStaysInBounds) {
  // The plane is (w*h+7)/8 bytes but is indexed at (h+7)/8 bytes per column, so
  // the tail columns have no storage. The thumbnail path bounds-checks them and
  // treats them as white instead of reading past the buffer.
  ASSERT_FALSE(xtcfix::xthLayoutIsAddressable(kFullW, 804));
  Book book(twoBit(kFullW, 804, [](int, int) { return 3; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateThumbBmp(100));
  Bmp bmp;
  ASSERT_TRUE(loadThumb(*book.xtc, 100, bmp));
  EXPECT_EQ(bmp.width, 60);
  EXPECT_EQ(bmp.heightRaw, -100);
}

TEST(XtcCover, XthHeightNotAMultipleOfEightStaysInBounds) {
  // The cover path walks the same column-major planes as the thumbnail. The tail
  // columns of a height that is not a multiple of 8 have no storage, so they must
  // read as white instead of running off the end of the two-plane buffer.
  ASSERT_FALSE(xtcfix::xthLayoutIsAddressable(kFullW, 804));
  Book book(twoBit(kFullW, 804, [](int, int) { return 3; }));
  ASSERT_TRUE(book.xtc->load());
  ASSERT_TRUE(book.xtc->generateCoverBmp());
  Bmp bmp;
  ASSERT_TRUE(loadCover(*book.xtc, bmp));
  EXPECT_EQ(bmp.bitCount, 2);
  EXPECT_EQ(bmp.width, kFullW);
  EXPECT_EQ(bmp.heightRaw, -804);
}

// ---------------------------------------------------------------- reader page pixels

TEST(XtcReaderPixels, XthHeightNotAMultipleOfEightStaysInBounds) {
  // Same defect on the reader's page-drawing path (XtcReaderActivity's
  // getPixelValue): the rightmost columns index past the plane pair.
  constexpr uint16_t kW = 480;
  constexpr uint16_t kH = 804;
  ASSERT_FALSE(xtcfix::xthLayoutIsAddressable(kW, kH));
  const std::vector<uint8_t> planes = xtcfix::xthBitmap(kW, kH, [](int, int) { return 3; });
  const size_t planeSize = planes.size() / 2;
  // x = 0 is the rightmost column, whose bottom offsets fall past both planes.
  EXPECT_EQ(xtc_reader::xthPixelValue(planes.data(), planeSize, kW, kH, 0, kH - 1), 0);
  // x = width - 1 is column 0, always addressable, and decodes normally.
  EXPECT_EQ(xtc_reader::xthPixelValue(planes.data(), planeSize, kW, kH, kW - 1, 0), 3);
}

TEST(XtcReaderPixels, XthDecodesBothPlanesOnAnAddressablePage) {
  // Height a multiple of 8: every offset has storage and all four values round-trip.
  constexpr uint16_t kW = 16;
  constexpr uint16_t kH = 8;
  ASSERT_TRUE(xtcfix::xthLayoutIsAddressable(kW, kH));
  const std::vector<uint8_t> planes = xtcfix::xthBitmap(kW, kH, [](int x, int) { return x % 4; });
  const size_t planeSize = planes.size() / 2;
  for (uint16_t x = 0; x < kW; ++x) {
    EXPECT_EQ(xtc_reader::xthPixelValue(planes.data(), planeSize, kW, kH, x, 3), x % 4) << "x=" << x;
  }
}

TEST(XtcReaderPixels, CoordinatesOutsideThePageAndANullBufferAreWhite) {
  constexpr uint16_t kW = 16;
  constexpr uint16_t kH = 8;
  const std::vector<uint8_t> planes = xtcfix::xthBitmap(kW, kH, [](int, int) { return 3; });
  const size_t planeSize = planes.size() / 2;
  EXPECT_EQ(xtc_reader::xthPixelValue(planes.data(), planeSize, kW, kH, kW, 0), 0);
  EXPECT_EQ(xtc_reader::xthPixelValue(planes.data(), planeSize, kW, kH, 0, kH), 0);
  EXPECT_EQ(xtc_reader::xthPixelValue(nullptr, planeSize, kW, kH, 0, 0), 0);
}

TEST(XtcThumb, NoUpscaleCopyFailsWhenTheCoverCannotBeBuilt) {
  XtcSpec spec = oneBit(16, 8, [](int, int) { return true; });
  spec.truncateTail = 4;  // bitmap short, so the cover the copy path needs fails
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateThumbBmp(100));
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getThumbBmpPath(100)));
}

TEST(XtcThumb, TruncatedXthPlaneRefusesTheScaledThumb) {
  // The 2-bit cover path is covered above; the scaled path reads the same
  // planes through a different allocation, so it gets its own short plane.
  XtcSpec spec = twoBit(kFullW, kFullH, [](int, int) { return 3; });
  spec.truncateTail = 64;
  Book book(spec);
  ASSERT_TRUE(book.xtc->load());
  EXPECT_FALSE(book.xtc->generateThumbBmp(100));
  EXPECT_FALSE(xtcfix::fileExists(book.xtc->getThumbBmpPath(100)));
}

TEST(XtcThumb, TallPageYieldsOncePerEightWrittenRows) {
  Book book(oneBit(kFullW, kFullH, [](int, int) { return true; }));
  ASSERT_TRUE(book.xtc->load());
  platform_host::resetCounters();
  ASSERT_TRUE(book.xtc->generateThumbBmp(200));
  EXPECT_EQ(platform_host::yieldCount(), 200u / 8u);
}

// The cover->thumb copy path's 512-byte buffer lives on the heap, not in
// generateThumbBmp's frame, where it cost ~780 bytes -- three times the
// 256-byte stack budget. HalFile::read() samples the deepest stack address the
// copy loop reaches.
TEST(XtcThumb, CoverCopyRunsInASmallStackFrame) {
  if (!halfile_stack_probe::kMeasurementIsReliable) {
    GTEST_SKIP() << "AddressSanitizer pads every frame on the path; the measurement is not the production frame";
  }

  // A page smaller than the requested thumbnail takes the "no scaling needed"
  // path, which streams cover.bmp into thumb.bmp through that buffer.
  Book book(oneBit(64, 64, [](int x, int) { return (x & 1) != 0; }));
  ASSERT_TRUE(book.xtc->load());
  // Generate the cover first so the measured call is just the copy.
  ASSERT_TRUE(book.xtc->generateCoverBmp());

  const char anchor = 0;
  halfile_stack_probe::reset();
  ASSERT_TRUE(book.xtc->generateThumbBmp(200));
  const size_t depth = halfile_stack_probe::depthFrom(&anchor);

  ASSERT_GT(depth, 0u) << "read() was never reached; the probe measured nothing";
  EXPECT_LT(depth, 640u);
}
