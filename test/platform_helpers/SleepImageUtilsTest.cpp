// src/activities/boot_sleep/SleepImageUtils: the header validators SleepActivity
// runs before allocating a decode buffer, and the full-screen placement math
// (FR-028). Every fixture is built byte-by-byte here so truncation and
// lying size fields can be exercised (Constitution VI).

#include <HalStorage.h>
#include <HostControls.h>
#include <Logging.h>
#include <SleepImageUtils.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "TestSupport.h"

namespace {

using sleepimage::BitmapPlacement;
using sleepimage::calculateBitmapPlacement;
using sleepimage::isValidPngHeader;
using sleepimage::OverlayBmpInfo;
using sleepimage::parseOverlayBmpHeader;

constexpr int kPageW = 480;
constexpr int kPageH = 800;

// --- PNG fixtures ----------------------------------------------------------

struct PngIhdr {
  uint32_t width = 640;
  uint32_t height = 480;
  uint8_t bitDepth = 8;
  uint8_t colorType = 2;  // truecolor
  uint8_t compression = 0;
  uint8_t filter = 0;
  uint8_t interlace = 0;
  uint32_t declaredLength = 13;
  std::string chunkType = "IHDR";
};

std::vector<uint8_t> makePng(const PngIhdr& h) {
  ByteWriter w;
  w.u8(0x89).raw(std::string("PNG")).u8(0x0D).u8(0x0A).u8(0x1A).u8(0x0A);
  w.be32(h.declaredLength).raw(h.chunkType);
  w.be32(h.width).be32(h.height);
  w.u8(h.bitDepth).u8(h.colorType).u8(h.compression).u8(h.filter).u8(h.interlace);
  w.be32(0);  // CRC placeholder; never read
  return w.bytes;
}

// --- BMP fixtures ----------------------------------------------------------

struct BmpFields {
  uint16_t magic = 0x4D42;
  uint32_t dataOffset = 54;
  uint32_t dibSize = 40;
  int32_t width = 100;
  int32_t height = 50;  // negative means top-down
  uint16_t planes = 1;
  uint16_t bpp = 32;
  uint32_t compression = 0;
  size_t pixelBytes = 100 * 50 * 4;
  bool padToDataOffset = true;
};

std::vector<uint8_t> makeBmp(const BmpFields& f) {
  ByteWriter w;
  w.le16(f.magic).le32(0).le16(0).le16(0);  // magic, bfSize, two reserved words
  w.le32(f.dataOffset);
  w.le32(f.dibSize);
  w.le32(static_cast<uint32_t>(f.width)).le32(static_cast<uint32_t>(f.height));
  w.le16(f.planes).le16(f.bpp).le32(f.compression);
  w.le32(0).le32(0).le32(0).le32(0).le32(0);  // sizeImage, ppm x2, clrUsed, clrImportant
  if (f.padToDataOffset) {
    while (w.bytes.size() < f.dataOffset) w.u8(0);
    w.fill(f.pixelBytes, 0x77);
  }
  return w.bytes;
}

class SleepImageUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    clearLastLogs();
  }

  // Writes bytes to the temp SD root and hands back an open read handle.
  HalFile openWith(const std::vector<uint8_t>& bytes) {
    storage.put("/img.bin", bytes);
    HalFile file;
    EXPECT_TRUE(Storage.openFileForRead("TST", "/img.bin", file));
    return file;
  }

  bool png(const PngIhdr& h) {
    HalFile file = openWith(makePng(h));
    return isValidPngHeader(file);
  }

  bool pngBytes(const std::vector<uint8_t>& bytes) {
    HalFile file = openWith(bytes);
    return isValidPngHeader(file);
  }

  bool bmp(const BmpFields& f, OverlayBmpInfo& info, bool logErrors = true) {
    HalFile file = openWith(makeBmp(f));
    return parseOverlayBmpHeader(file, info, logErrors);
  }

  ScopedStorageRoot storage;
};

}  // namespace

/* ---------- isValidPngHeader ---------- */

TEST_F(SleepImageUtilsTest, AcceptsAPlainTruecolorPng) { EXPECT_TRUE(png({})); }

TEST_F(SleepImageUtilsTest, RejectsEmptyAndTruncatedSignature) {
  EXPECT_FALSE(pngBytes({}));
  EXPECT_FALSE(pngBytes({0x89, 'P', 'N', 'G'}));
  auto bytes = makePng({});
  bytes.resize(8);  // signature only
  EXPECT_FALSE(pngBytes(bytes));
}

TEST_F(SleepImageUtilsTest, RejectsWrongSignatureByte) {
  auto bytes = makePng({});
  bytes[0] = 0x88;
  EXPECT_FALSE(pngBytes(bytes));
  bytes = makePng({});
  bytes[7] = 0x00;
  EXPECT_FALSE(pngBytes(bytes));
}

TEST_F(SleepImageUtilsTest, RejectsJpegBytesEntirely) {
  EXPECT_FALSE(pngBytes({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F'}));
}

TEST_F(SleepImageUtilsTest, RejectsWrongIhdrLengthOrChunkType) {
  PngIhdr h;
  h.declaredLength = 12;
  EXPECT_FALSE(png(h));
  h.declaredLength = 0xFFFFFFFFu;  // lying length field
  EXPECT_FALSE(png(h));
  h = PngIhdr{};
  h.chunkType = "IEND";
  EXPECT_FALSE(png(h));
}

TEST_F(SleepImageUtilsTest, RejectsTruncationInsideTheIhdrPayload) {
  for (size_t keep : {12u, 16u, 20u, 24u, 26u, 28u}) {
    auto bytes = makePng({});
    bytes.resize(keep);
    EXPECT_FALSE(pngBytes(bytes)) << keep;
  }
  auto full = makePng({});
  full.resize(29);  // through the interlace byte; the CRC is never read
  EXPECT_TRUE(pngBytes(full));
}

TEST_F(SleepImageUtilsTest, RejectsZeroDimensions) {
  PngIhdr h;
  h.width = 0;
  EXPECT_FALSE(png(h));
  h = PngIhdr{};
  h.height = 0;
  EXPECT_FALSE(png(h));
}

TEST_F(SleepImageUtilsTest, EnforcesTheDimensionCeilings) {
  PngIhdr h;
  h.width = 2048;
  h.height = 1536;
  EXPECT_TRUE(png(h));  // exactly the pixel budget
  h.width = 2049;
  h.height = 1;
  EXPECT_FALSE(png(h));
  h.width = 1;
  h.height = 3073;
  EXPECT_FALSE(png(h));
  h.width = 1;
  h.height = 3072;
  EXPECT_TRUE(png(h));
}

TEST_F(SleepImageUtilsTest, EnforcesTheTotalPixelBudget) {
  PngIhdr h;
  h.width = 2048;
  h.height = 1537;  // within both axis limits, one row over the budget
  EXPECT_FALSE(png(h));
  h.width = 1024;
  h.height = 3072;
  EXPECT_TRUE(png(h));
}

TEST_F(SleepImageUtilsTest, EnforcesHugeDimensionsWithoutIntegerOverflow) {
  PngIhdr h;
  h.width = 0x10000;  // width * height would wrap to 0
  h.height = 0x10000;
  EXPECT_FALSE(png(h));
}

TEST_F(SleepImageUtilsTest, EightBitIsAllowedForEveryColourType) {
  for (uint8_t colorType : {0, 2, 3, 4, 6}) {
    PngIhdr h;
    h.bitDepth = 8;
    h.colorType = colorType;
    EXPECT_TRUE(png(h)) << static_cast<int>(colorType);
  }
}

TEST_F(SleepImageUtilsTest, SubByteDepthsAreOnlyAllowedForGrayscaleAndIndexed) {
  for (uint8_t depth : {1, 2, 4}) {
    for (uint8_t colorType : {0, 3}) {
      PngIhdr h;
      h.bitDepth = depth;
      h.colorType = colorType;
      EXPECT_TRUE(png(h)) << depth << "/" << static_cast<int>(colorType);
    }
    for (uint8_t colorType : {2, 4, 6}) {
      PngIhdr h;
      h.bitDepth = depth;
      h.colorType = colorType;
      EXPECT_FALSE(png(h)) << depth << "/" << static_cast<int>(colorType);
    }
  }
}

TEST_F(SleepImageUtilsTest, RejectsSixteenBitAndNonsenseDepths) {
  for (uint8_t depth : {0, 3, 5, 16, 255}) {
    PngIhdr h;
    h.bitDepth = depth;
    EXPECT_FALSE(png(h)) << static_cast<int>(depth);
  }
}

TEST_F(SleepImageUtilsTest, RejectsUndefinedColourTypes) {
  for (uint8_t colorType : {1, 5, 7, 255}) {
    PngIhdr h;
    h.colorType = colorType;
    EXPECT_FALSE(png(h)) << static_cast<int>(colorType);
  }
}

TEST_F(SleepImageUtilsTest, RejectsNonZeroCompressionFilterOrInterlace) {
  PngIhdr h;
  h.compression = 1;
  EXPECT_FALSE(png(h));
  h = PngIhdr{};
  h.filter = 1;
  EXPECT_FALSE(png(h));
  h = PngIhdr{};
  h.interlace = 1;  // Adam7 is unsupported by the streaming decoder
  EXPECT_FALSE(png(h));
}

TEST_F(SleepImageUtilsTest, ValidationRewindsSoItCanRunTwice) {
  HalFile file = openWith(makePng({}));
  EXPECT_TRUE(isValidPngHeader(file));
  EXPECT_TRUE(isValidPngHeader(file));
}

/* ---------- parseOverlayBmpHeader ---------- */

TEST_F(SleepImageUtilsTest, AcceptsBottomUpBgraOverlay) {
  OverlayBmpInfo info;
  ASSERT_TRUE(bmp({}, info));
  EXPECT_EQ(info.width, 100);
  EXPECT_EQ(info.height, 50);
  EXPECT_FALSE(info.topDown);
  EXPECT_EQ(info.dataOffset, 54u);
  EXPECT_EQ(info.rowBytes, 400u);
}

TEST_F(SleepImageUtilsTest, NegativeHeightMeansTopDown) {
  BmpFields f;
  f.height = -50;
  OverlayBmpInfo info;
  ASSERT_TRUE(bmp(f, info));
  EXPECT_TRUE(info.topDown);
  EXPECT_EQ(info.height, 50);
}

TEST_F(SleepImageUtilsTest, LeavesTheFilePositionedAtPixelData) {
  BmpFields f;
  f.dataOffset = 70;
  HalFile file = openWith(makeBmp(f));
  OverlayBmpInfo info;
  ASSERT_TRUE(parseOverlayBmpHeader(file, info, true));
  EXPECT_EQ(file.position(), 70u);
  EXPECT_EQ(file.read(), 0x77);
}

TEST_F(SleepImageUtilsTest, AcceptsBitfieldsCompression) {
  BmpFields f;
  f.compression = 3;
  OverlayBmpInfo info;
  EXPECT_TRUE(bmp(f, info));
}

TEST_F(SleepImageUtilsTest, RejectsRleAndOtherCompressions) {
  for (uint32_t comp : {1u, 2u, 4u, 5u}) {
    BmpFields f;
    f.compression = comp;
    OverlayBmpInfo info;
    clearLastLogs();
    EXPECT_FALSE(bmp(f, info)) << comp;
    EXPECT_NE(getLastLogs().find("must be 32-bit BGRA BMP"), std::string::npos);
  }
}

TEST_F(SleepImageUtilsTest, RejectsNon32BitDepthsAndMultiplePlanes) {
  for (uint16_t bpp : {1, 4, 8, 16, 24}) {
    BmpFields f;
    f.bpp = bpp;
    OverlayBmpInfo info;
    EXPECT_FALSE(bmp(f, info)) << bpp;
  }
  BmpFields f;
  f.planes = 2;
  OverlayBmpInfo info;
  EXPECT_FALSE(bmp(f, info));
}

TEST_F(SleepImageUtilsTest, RejectsNonBmpMagic) {
  BmpFields f;
  f.magic = 0x4D41;  // "AM"
  OverlayBmpInfo info;
  EXPECT_FALSE(bmp(f, info));
  EXPECT_NE(getLastLogs().find("is not a BMP"), std::string::npos);
}

TEST_F(SleepImageUtilsTest, RejectsCoreHeaderSizedDib) {
  for (uint32_t dib : {0u, 12u, 39u}) {
    BmpFields f;
    f.dibSize = dib;
    OverlayBmpInfo info;
    clearLastLogs();
    EXPECT_FALSE(bmp(f, info)) << dib;
    EXPECT_NE(getLastLogs().find("Unsupported BMP DIB header"), std::string::npos);
  }
  BmpFields f;
  f.dibSize = 124;  // BITMAPV5HEADER is fine; the extra fields sit before the pixels
  f.dataOffset = 138;
  OverlayBmpInfo info;
  EXPECT_TRUE(bmp(f, info));
}

TEST_F(SleepImageUtilsTest, EnforcesOverlayDimensionBounds) {
  const int32_t cases[][2] = {{0, 50}, {100, 0}, {-100, 50}, {2049, 50}, {100, 3073}, {100, -3073}};
  for (const auto& c : cases) {
    BmpFields f;
    f.width = c[0];
    f.height = c[1];
    f.pixelBytes = 0;
    OverlayBmpInfo info;
    clearLastLogs();
    EXPECT_FALSE(bmp(f, info)) << c[0] << "x" << c[1];
    EXPECT_NE(getLastLogs().find("Bad transparent overlay dimensions"), std::string::npos);
  }
  BmpFields ok;
  ok.width = 2048;
  ok.height = 3072;
  ok.pixelBytes = 0;
  ok.dataOffset = 54;
  OverlayBmpInfo info;
  EXPECT_TRUE(bmp(ok, info));  // at the ceiling, pixel data may be short
  EXPECT_EQ(info.rowBytes, 8192u);
}

TEST_F(SleepImageUtilsTest, RejectsIntMinHeightBeforeNegating) {
  BmpFields f;
  f.height = INT32_MIN;  // -height would overflow
  f.pixelBytes = 0;
  OverlayBmpInfo info;
  EXPECT_FALSE(bmp(f, info));
  EXPECT_NE(getLastLogs().find("Bad transparent overlay dimensions"), std::string::npos);
}

TEST_F(SleepImageUtilsTest, RejectsDataOffsetPastEndOfFile) {
  BmpFields f;
  f.dataOffset = 100000;
  f.padToDataOffset = false;  // header only, so the offset lands past EOF
  OverlayBmpInfo info;
  EXPECT_FALSE(bmp(f, info));
  EXPECT_NE(getLastLogs().find("Failed to seek transparent overlay pixel data"), std::string::npos);
}

TEST_F(SleepImageUtilsTest, RejectsEmptyAndTruncatedOverlays) {
  OverlayBmpInfo info;
  HalFile empty = openWith({});
  EXPECT_FALSE(parseOverlayBmpHeader(empty, info, false));

  // "BM" only: the seekCur past the reserved words fails and every later field reads as 0.
  HalFile magicOnly = openWith({0x42, 0x4D});
  EXPECT_FALSE(parseOverlayBmpHeader(magicOnly, info, true));
  EXPECT_NE(getLastLogs().find("Unsupported BMP DIB header: 0"), std::string::npos);

  for (size_t keep : {14u, 18u, 22u, 26u, 30u, 33u}) {
    auto bytes = makeBmp({});
    bytes.resize(keep);
    HalFile file = openWith(bytes);
    OverlayBmpInfo truncated;
    EXPECT_FALSE(parseOverlayBmpHeader(file, truncated, false)) << keep;
  }
}

TEST_F(SleepImageUtilsTest, RejectsAClosedFile) {
  HalFile file;
  OverlayBmpInfo info;
  EXPECT_FALSE(parseOverlayBmpHeader(file, info, true));
  EXPECT_EQ(getLastLogs(), "");
}

TEST_F(SleepImageUtilsTest, SilentModeLogsNothing) {
  BmpFields f;
  f.bpp = 24;
  OverlayBmpInfo info;
  EXPECT_FALSE(bmp(f, info, false));
  EXPECT_EQ(getLastLogs(), "");
}

/* ---------- calculateBitmapPlacement ---------- */

TEST_F(SleepImageUtilsTest, CentresAnImageThatAlreadyFits) {
  const auto p = calculateBitmapPlacement(100, 200, kPageW, kPageH, false);
  EXPECT_EQ(p.x, 190);
  EXPECT_EQ(p.y, 300);
  EXPECT_FLOAT_EQ(p.cropX, 0.0f);
  EXPECT_FLOAT_EQ(p.cropY, 0.0f);
}

TEST_F(SleepImageUtilsTest, ExactFitSitsAtTheOrigin) {
  const auto p = calculateBitmapPlacement(kPageW, kPageH, kPageW, kPageH, true);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 0);
  EXPECT_FLOAT_EQ(p.cropX, 0.0f);
}

TEST_F(SleepImageUtilsTest, OddLeftoverTruncatesTowardsTheTopLeft) {
  const auto p = calculateBitmapPlacement(101, 201, kPageW, kPageH, false);
  EXPECT_EQ(p.x, 189);  // (480-101)/2
  EXPECT_EQ(p.y, 299);  // (800-201)/2
}

TEST_F(SleepImageUtilsTest, OversizeLandscapeLettersboxesVertically) {
  const auto p = calculateBitmapPlacement(1600, 800, kPageW, kPageH, false);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 280);  // (800 - 480/2.0) / 2
  EXPECT_FLOAT_EQ(p.cropX, 0.0f);
}

TEST_F(SleepImageUtilsTest, OversizePortraitPillarboxesHorizontally) {
  const auto p = calculateBitmapPlacement(480, 1600, kPageW, kPageH, false);
  EXPECT_EQ(p.x, 120);  // (480 - 800*0.3) / 2
  EXPECT_EQ(p.y, 0);
  EXPECT_FLOAT_EQ(p.cropY, 0.0f);
}

TEST_F(SleepImageUtilsTest, CropModeFillsTheScreenForALandscapeSource) {
  const auto p = calculateBitmapPlacement(1600, 800, kPageW, kPageH, true);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 0);
  EXPECT_NEAR(p.cropX, 0.7f, 1e-5f);  // 1 - (0.6 / 2.0)
  EXPECT_FLOAT_EQ(p.cropY, 0.0f);
}

TEST_F(SleepImageUtilsTest, CropModeFillsTheScreenForAPortraitSource) {
  const auto p = calculateBitmapPlacement(480, 1600, kPageW, kPageH, true);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 0);
  EXPECT_NEAR(p.cropY, 0.5f, 1e-5f);  // 1 - (0.3 / 0.6)
  EXPECT_FLOAT_EQ(p.cropX, 0.0f);
}

TEST_F(SleepImageUtilsTest, MatchingAspectRatioNeedsNoCropEitherWay) {
  for (bool crop : {false, true}) {
    const auto p = calculateBitmapPlacement(960, 1600, kPageW, kPageH, crop);
    EXPECT_EQ(p.x, 0) << crop;
    EXPECT_EQ(p.y, 0) << crop;
    EXPECT_FLOAT_EQ(p.cropX, 0.0f);
    EXPECT_FLOAT_EQ(p.cropY, 0.0f);
  }
}

TEST_F(SleepImageUtilsTest, OneOversizeAxisStillGoesThroughTheScalingBranch) {
  // Wider than the page but shorter: scaled down to fit, then centred vertically.
  const auto p = calculateBitmapPlacement(600, 400, kPageW, kPageH, false);
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 240);  // (800 - 480/1.5) / 2
}

TEST_F(SleepImageUtilsTest, PlacementIsOrientationAgnostic) {
  const auto landscape = calculateBitmapPlacement(200, 100, kPageH, kPageW, false);
  EXPECT_EQ(landscape.x, 300);  // (800-200)/2
  EXPECT_EQ(landscape.y, 190);  // (480-100)/2
}
