// Host tests for lib/JpegToBmpConverter: the streaming JPEG -> 2-bit/1-bit
// BMP cover pipeline, compiled against the REAL JPEGDEC library at the same
// pinned commit + CrossPoint patches as the firmware (see test/CMakeLists.txt)
// so decoder error handling on malformed input is genuinely under test.
//
// Fixtures come from scripts/generate_test_images.py (frozen Pillow output);
// truncation and SOF-dimension-lying variants are byte surgery done in-test.
//
// Note on golden checksums: JPEG entropy decode is bit-exact for a given
// decoder build; the FNV pins below were captured on the arm64 host build of
// JPEGDEC (NEON paths). If a future host arch produces different IDCT
// rounding these two pins are the place to re-capture.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "AllocGuard.h"  // defines the global allocator interposers (one TU only)
#include "BmpTestUtil.h"
#include "HalStorage.h"
#include "JPEGDEC.h"  // sizeof(JPEGDEC) for allocation-budget assertions
#include "JpegToBmpConverter.h"

namespace {

using imgtest::MemoryPrint;
using imgtest::ParsedBmp;

std::string res(const std::string& name) { return std::string(JPEG_RESOURCES_DIR) + "/" + name; }

void primeFile(HalFile& file) {
  uint8_t b;
  (void)file.read(&b, 1);
  file.seek(0);
}

bool convert(const std::string& path, MemoryPrint& out, const int tw, const int th) {
  HalFile file;
  if (!file.open(path.c_str(), "rb")) {
    ADD_FAILURE() << "cannot open fixture " << path;
    return false;
  }
  return JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, out, tw, th);
}

bool convert1Bit(const std::string& path, MemoryPrint& out, const int tw, const int th) {
  HalFile file;
  if (!file.open(path.c_str(), "rb")) {
    ADD_FAILURE() << "cannot open fixture " << path;
    return false;
  }
  return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file, out, tw, th);
}

bool convertBytes(const std::vector<uint8_t>& jpeg, MemoryPrint& out, const int tw, const int th,
                  const std::string& tag = "surgery") {
  const std::string path = "tmp_" + tag + ".jpg";
  EXPECT_TRUE(imgtest::writeFileBytes(path, jpeg));
  const bool ok = convert(path, out, tw, th);
  std::remove(path.c_str());
  return ok;
}

void expectAllPixels(const ParsedBmp& bmp, const int expected) {
  for (int y = 0; y < bmp.absHeight(); y++) {
    for (int x = 0; x < bmp.width; x++) {
      ASSERT_EQ(bmp.pixel(x, y), expected) << "pixel (" << x << "," << y << ")";
    }
  }
}

// Walk the marker segments of a JPEG. Returns the start offset of every
// segment (including SOI) and, via outScanStart, the offset where entropy
// data begins (just after the SOS header).
std::vector<size_t> markerOffsets(const std::vector<uint8_t>& j, size_t* outScanStart) {
  std::vector<size_t> offsets;
  *outScanStart = j.size();
  size_t off = 0;
  EXPECT_GE(j.size(), 4u);
  EXPECT_EQ(j[0], 0xFF);
  EXPECT_EQ(j[1], 0xD8);
  offsets.push_back(0);
  off = 2;
  while (off + 4 <= j.size()) {
    EXPECT_EQ(j[off], 0xFF) << "lost marker sync at " << off;
    const uint8_t marker = j[off + 1];
    offsets.push_back(off);
    if (marker == 0xD9) break;  // EOI
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      off += 2;
      continue;
    }
    const size_t len = (static_cast<size_t>(j[off + 2]) << 8) | j[off + 3];
    off += 2 + len;
    if (marker == 0xDA) {  // SOS: entropy-coded data follows
      *outScanStart = off;
      break;
    }
  }
  return offsets;
}

// Locate the SOFn dimension bytes and overwrite them (big-endian h then w).
std::vector<uint8_t> patchSofDims(std::vector<uint8_t> j, const uint16_t width, const uint16_t height) {
  size_t scanStart = 0;
  bool patched = false;
  for (const size_t off : markerOffsets(j, &scanStart)) {
    const uint8_t marker = j[off + 1];
    const bool isSof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    if (!isSof) continue;
    // FF Cn LL LL PP HH HH WW WW ...
    j[off + 5] = static_cast<uint8_t>(height >> 8);
    j[off + 6] = static_cast<uint8_t>(height & 0xFF);
    j[off + 7] = static_cast<uint8_t>(width >> 8);
    j[off + 8] = static_cast<uint8_t>(width & 0xFF);
    patched = true;
    break;
  }
  EXPECT_TRUE(patched) << "no SOF marker found to patch";
  return j;
}

// ---------------------------------------------------------------------------
// Happy path: flat fixtures give hand-computable output (zero/clamped dither
// error keeps every pixel on one palette index).
// ---------------------------------------------------------------------------

TEST(JpegToBmp, GrayFlatWhiteDecodesToAllWhite2Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray_flat_white.jpg"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 16);
  EXPECT_EQ(bmp.heightRaw, -16);  // top-down
  EXPECT_EQ(bmp.bitCount, 2);
  EXPECT_EQ(bmp.colorsUsed, 4u);
  expectAllPixels(bmp, 3);
}

TEST(JpegToBmp, GrayFlatBlackDecodesToAllBlack2Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray_flat_black.jpg"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.bitCount, 2);
  expectAllPixels(bmp, 0);
}

TEST(JpegToBmp, GrayFlatWhiteDecodesToAllWhite1Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert1Bit(res("gray_flat_white.jpg"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.colorsUsed, 2u);
  expectAllPixels(bmp, 1);
}

TEST(JpegToBmp, YCbCr420FlatWhiteDecodesToAllWhite) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("rgb_flat_white_420.jpg"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  expectAllPixels(bmp, 3);
}

// Baseline decodes across component layouts: grayscale, 4:4:4, 4:2:0.
class JpegBaseline : public ::testing::TestWithParam<const char*> {};

TEST_P(JpegBaseline, DecodesToStructurallyValid16x16) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res(GetParam()), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 16);
  EXPECT_EQ(bmp.absHeight(), 16);
  EXPECT_EQ(bmp.bitCount, 2);
}

INSTANTIATE_TEST_SUITE_P(All, JpegBaseline,
                         ::testing::Values("gray_baseline.jpg", "rgb_baseline_444.jpg", "rgb_baseline_420.jpg"),
                         [](const auto& info) {
                           std::string n = info.param;
                           for (auto& c : n)
                             if (c == '.' || c == '-') c = '_';
                           return n;
                         });

// Golden regression pins (see file header note about host arch).
TEST(JpegToBmp, GoldenChecksumGrayBaseline) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray_baseline.jpg"), out, 16, 16));
  EXPECT_EQ(imgtest::fnv1a64(out.bytes), 0x753DD7C7C2121086ULL);
}

TEST(JpegToBmp, GoldenChecksumYCbCr420Baseline) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("rgb_baseline_420.jpg"), out, 16, 16));
  EXPECT_EQ(imgtest::fnv1a64(out.bytes), 0x7481D42882183383ULL);
}

// Progressive JPEG: JPEGDEC forces 1/8-scale decode of progressive streams;
// the converter smooth-upscales the 4x4 decode grid back to the 32x32 target.
// This is also the exact path scripts/jpegdec_patches/ fixes (grayscale
// decode of a 3-component progressive image drives MCU_SKIP for Cb/Cr).
TEST(JpegToBmp, ProgressiveDecodesViaEighthScaleAndSmoothUpscale) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("progressive_rgb_32.jpg"), out, 32, 32));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 32);
  EXPECT_EQ(bmp.absHeight(), 32);
  EXPECT_EQ(imgtest::fnv1a64(out.bytes), 0x70CDF65133A41581ULL);
}

// ---------------------------------------------------------------------------
// Scaling geometry
// ---------------------------------------------------------------------------

TEST(JpegToBmp, DownscaleUsesCoverSemantics) {
  // 64x48 -> target 16x16: crop picks max(16/64, 16/48) = 1/3 -> 21x16.
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray_64x48.jpg"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 21);
  EXPECT_EQ(bmp.absHeight(), 16);
}

TEST(JpegToBmp, UpscaleProducesRequestedSizeAndStaysWhite) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray_flat_white.jpg"), out, 32, 32));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 32);
  EXPECT_EQ(bmp.absHeight(), 32);
  expectAllPixels(bmp, 3);
}

TEST(JpegToBmp, DefaultApiUsesSwappedDisplayGeometry) {
  // Stub display is 24x32 (w x h): cover target becomes 32x24, and a 16x16
  // source scales by max(2, 1.5) = 2 -> 32x32 all white.
  HalFile file;
  ASSERT_TRUE(file.open(res("gray_flat_white.jpg").c_str(), "rb"));
  MemoryPrint out;
  ASSERT_TRUE(JpegToBmpConverter::jpegFileToBmpStream(file, out));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 32);
  EXPECT_EQ(bmp.absHeight(), 32);
  expectAllPixels(bmp, 3);
}

// ---------------------------------------------------------------------------
// Malformed inputs. The whole suite runs ASan/UBSan-clean via
// bin/run-tests --asan; a sanitizer trip inside JPEGDEC on any of these would
// be exactly the class of bug this suite exists to catch.
// ---------------------------------------------------------------------------

TEST(JpegToBmp, NonJpegBytesRejected) {
  MemoryPrint out;
  EXPECT_FALSE(convertBytes({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13}, out, 16, 16, "png"));
  MemoryPrint out2;
  EXPECT_FALSE(convertBytes({}, out2, 16, 16, "empty"));
  MemoryPrint out3;
  EXPECT_FALSE(convertBytes({0xFF, 0xD8}, out3, 16, 16, "soi_only"));
  MemoryPrint out4;
  EXPECT_FALSE(convertBytes(std::vector<uint8_t>(64, 0x00), out4, 16, 16, "zeros"));
}

TEST(JpegToBmp, SofZeroDimensionsRejected) {
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("gray_baseline.jpg"));
  ASSERT_FALSE(base.empty());
  MemoryPrint out;
  EXPECT_FALSE(convertBytes(patchSofDims(base, 0, 0), out, 16, 16, "dims_zero"));
}

TEST(JpegToBmp, SofDimensionsOverCapsRejectedWithBoundedAllocation) {
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("gray_baseline.jpg"));
  ASSERT_FALSE(base.empty());
  struct Case {
    uint16_t w, h;
    const char* tag;
  };
  for (const Case& c : {Case{60000, 60000, "huge"}, Case{2049, 16, "overwidth"}, Case{16, 3073, "overheight"}}) {
    const std::string path = std::string("tmp_dims_") + c.tag + ".jpg";
    ASSERT_TRUE(imgtest::writeFileBytes(path, patchSofDims(base, c.w, c.h)));
    HalFile file;
    ASSERT_TRUE(file.open(path.c_str(), "rb"));
    primeFile(file);
    MemoryPrint out;
    size_t maxAlloc;
    bool ok;
    {
      allocguard::TrackScope guard;
      ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, out, 16, 16);
      maxAlloc = allocguard::maxSingle;
    }
    file.close();
    std::remove(path.c_str());
    EXPECT_FALSE(ok) << c.tag;
    // The decoder state object itself is the only sizable allocation allowed;
    // nothing may scale with the attacker-declared dimensions.
    EXPECT_LE(maxAlloc, sizeof(JPEGDEC) + 4096) << c.tag;
  }
}

TEST(JpegToBmp, SofDimensionsLyingWithinCapsFailsCleanly) {
  // Header claims the 2048x3072 maximum while the scan holds 16x16 of data:
  // the converter sizes its MCU row buffer from the header (bounded, 32KB)
  // and the decode must then fail without overrunning it.
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("gray_baseline.jpg"));
  ASSERT_FALSE(base.empty());
  const std::string path = "tmp_dims_lying.jpg";
  ASSERT_TRUE(imgtest::writeFileBytes(path, patchSofDims(base, 2048, 3072)));
  HalFile file;
  ASSERT_TRUE(file.open(path.c_str(), "rb"));
  primeFile(file);
  MemoryPrint out;
  size_t maxAlloc;
  bool ok;
  {
    allocguard::TrackScope guard;
    ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, out, 16, 16);
    maxAlloc = allocguard::maxSingle;
  }
  file.close();
  std::remove(path.c_str());
  EXPECT_FALSE(ok);
  EXPECT_LE(maxAlloc, sizeof(JPEGDEC) + 64u * 1024);
}

TEST(JpegToBmp, CorruptedScanDataFailsCleanly) {
  std::vector<uint8_t> j = imgtest::readFileBytes(res("gray_baseline.jpg"));
  ASSERT_FALSE(j.empty());
  size_t scanStart = 0;
  markerOffsets(j, &scanStart);
  ASSERT_LT(scanStart, j.size());
  // Stomp the entropy-coded data (keep the EOI marker intact).
  for (size_t i = scanStart; i + 2 < j.size(); i++) j[i] = static_cast<uint8_t>(i * 41 + 3);
  MemoryPrint out;
  const bool ok = convertBytes(j, out, 16, 16, "corrupt_scan");
  // Whether JPEGDEC flags the broken Huffman stream or limps to an output,
  // it must terminate and any claimed success must be a well-formed BMP.
  if (ok) {
    ParsedBmp bmp;
    EXPECT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  }
}

TEST(JpegToBmp, TruncationAtEveryByteOffsetIsSafe) {
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("gray_baseline.jpg"));
  ASSERT_GT(base.size(), 100u);
  int successes = 0;
  for (size_t len = 0; len < base.size(); len++) {
    std::vector<uint8_t> cut(base.begin(), base.begin() + len);
    MemoryPrint out;
    const bool ok = convertBytes(cut, out, 16, 16, "trunc");
    if (ok) {
      successes++;
      ParsedBmp bmp;
      ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp))
          << "truncation at " << len << " returned success but wrote a malformed BMP";
    }
  }
  // The sweep must at least reject everything cut before the scan data.
  EXPECT_LT(successes, static_cast<int>(base.size()) / 2);
}

TEST(JpegToBmp, ProgressiveTruncationAtMarkerBoundariesIsSafe) {
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("progressive_rgb_32.jpg"));
  ASSERT_FALSE(base.empty());
  size_t scanStart = 0;
  const std::vector<size_t> offsets = markerOffsets(base, &scanStart);
  ASSERT_GT(offsets.size(), 3u);
  for (const size_t off : offsets) {
    std::vector<uint8_t> cut(base.begin(), base.begin() + off);
    MemoryPrint out;
    const bool ok = convertBytes(cut, out, 32, 32, "ptrunc");
    EXPECT_FALSE(ok) << "decode claimed success with the stream cut at marker offset " << off;
  }
}

TEST(JpegToBmp, ValidDecodeStaysWithinStreamingBudget) {
  HalFile file;
  ASSERT_TRUE(file.open(res("gray_baseline.jpg").c_str(), "rb"));
  primeFile(file);
  MemoryPrint out;
  out.bytes.reserve(4096);
  size_t maxAlloc, totalAlloc;
  bool ok;
  {
    allocguard::TrackScope guard;
    ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(file, out, 16, 16);
    maxAlloc = allocguard::maxSingle;
    totalAlloc = allocguard::totalBytes;
  }
  EXPECT_TRUE(ok);
  // Largest single block: the JPEGDEC state object; total stays in the same
  // ballpark (MCU row buffer + ditherer rows + BMP row).
  EXPECT_LE(maxAlloc, sizeof(JPEGDEC) + 4096);
  EXPECT_LE(totalAlloc, sizeof(JPEGDEC) + 128u * 1024);
}

}  // namespace
