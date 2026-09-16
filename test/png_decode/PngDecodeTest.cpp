// Host tests for lib/PngToBmpConverter: the streaming PNG -> 2-bit/1-bit BMP
// cover pipeline (real miniz InflateStream + real GfxRenderer ditherers; only
// the device HAL is stubbed).
//
// Fixture strategy: scripts/generate_test_images.py hand-rolls deterministic
// PNGs (committed under resources/). Several fixtures encode the SAME pixels
// through different color types / bit depths / filter mixes / chunk layouts,
// so equivalence assertions check real decode work (de-filtering, palette
// lookup, 16-bit narrowing) without re-implementing the dither pipeline here.
// Truncations are byte surgery on the base fixture done in-test at every
// single byte offset.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "AllocGuard.h"  // defines the global allocator interposers (one TU only)
#include "BmpTestUtil.h"
#include "HalStorage.h"
#include "PngToBmpConverter.h"

namespace {

using imgtest::MemoryPrint;
using imgtest::ParsedBmp;

std::string res(const std::string& name) { return std::string(PNG_RESOURCES_DIR) + "/" + name; }

// Prime the stdio buffer so allocation tracking sees converter work only.
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
  return PngToBmpConverter::pngFileToBmpStreamWithSize(file, out, tw, th);
}

bool convert1Bit(const std::string& path, MemoryPrint& out, const int tw, const int th) {
  HalFile file;
  if (!file.open(path.c_str(), "rb")) {
    ADD_FAILURE() << "cannot open fixture " << path;
    return false;
  }
  return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(file, out, tw, th);
}

// Convert an in-memory PNG (byte-surgery variants) via a temp file.
bool convertBytes(const std::vector<uint8_t>& png, MemoryPrint& out, const int tw, const int th,
                  const std::string& tag = "surgery") {
  const std::string path = "tmp_" + tag + ".png";
  EXPECT_TRUE(imgtest::writeFileBytes(path, png));
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

void expectPaddingClear(const ParsedBmp& bmp) {
  const size_t usedBits = static_cast<size_t>(bmp.width) * bmp.bitCount;
  const size_t usedBytes = (usedBits + 7) / 8;
  for (int y = 0; y < bmp.absHeight(); y++) {
    const uint8_t* row = bmp.row(y);
    // Trailing bits of the last used byte and all padding bytes must be 0.
    for (size_t i = usedBytes; i < bmp.bytesPerRow; i++) {
      ASSERT_EQ(row[i], 0) << "padding byte " << i << " in row " << y;
    }
  }
}

// ---------------------------------------------------------------------------
// Happy path: exact output for flat images (dither error is zero or clamps
// away, so every pixel lands on the same palette index -- hand-computable).
// ---------------------------------------------------------------------------

TEST(PngDecode, FlatWhiteDecodesToAllWhite2Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_flat_white_8x8.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 8);
  EXPECT_EQ(bmp.heightRaw, -8);  // negative = top-down, as the header promises
  EXPECT_EQ(bmp.bitCount, 2);
  EXPECT_EQ(bmp.colorsUsed, 4u);
  expectAllPixels(bmp, 3);  // palette index 3 = white
  expectPaddingClear(bmp);
}

TEST(PngDecode, FlatBlackDecodesToAllBlack2Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_flat_black_8x8.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.bitCount, 2);
  expectAllPixels(bmp, 0);
}

TEST(PngDecode, FlatWhiteDecodesToAllWhite1Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert1Bit(res("gray8_flat_white_8x8.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  EXPECT_EQ(bmp.colorsUsed, 2u);
  expectAllPixels(bmp, 1);
  expectPaddingClear(bmp);
}

TEST(PngDecode, FlatBlackDecodesToAllBlack1Bit) {
  MemoryPrint out;
  ASSERT_TRUE(convert1Bit(res("gray8_flat_black_8x8.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.bitCount, 1);
  expectAllPixels(bmp, 0);
}

// ---------------------------------------------------------------------------
// Equivalence: fixtures that encode the SAME gray pixels through different
// PNG features must produce byte-identical BMP streams.
// ---------------------------------------------------------------------------

std::vector<uint8_t> decodeRef(const std::string& name) {
  MemoryPrint out;
  EXPECT_TRUE(convert(res(name), out, 16, 16)) << name;
  return out.bytes;
}

TEST(PngDecode, AllFilterTypesReverseCorrectly) {
  // Same ramp, one file all filter-0, the other mixing Sub/Up/Average/Paeth.
  EXPECT_EQ(decodeRef("gray8_ramp_16x16_filters.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, RgbMatchesGrayWhenChannelsEqual) {
  // (r*25 + g*50 + b*25)/100 == v when r=g=b=v: exact equivalence.
  EXPECT_EQ(decodeRef("rgb8_ramp_16x16.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, RgbLumaWeightsPinnedByPureGreen) {
  // Unequal channels: pure green (0,255,0) -> (0*25 + 255*50 + 0*25)/100 =
  // 127, so the decode must be byte-identical to a flat-127 gray image. This
  // fails if the (25,50,25) luma weights drift.
  MemoryPrint green, gray;
  ASSERT_TRUE(convert(res("rgb8_flat_green_8x8.png"), green, 8, 8));
  ASSERT_TRUE(convert(res("gray8_flat_127_8x8.png"), gray, 8, 8));
  EXPECT_EQ(green.bytes, gray.bytes);
}

TEST(PngDecode, RgbaIgnoresAlphaChannel) {
  EXPECT_EQ(decodeRef("rgba8_ramp_16x16.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, GrayAlphaIgnoresAlphaChannel) {
  EXPECT_EQ(decodeRef("graya8_ramp_16x16.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, Gray16UsesHighByte) {
  // 16-bit samples are (ramp << 8) | 0xAB; the decoder keeps the high byte.
  EXPECT_EQ(decodeRef("gray16_ramp_16x16.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, OneBitGrayExpandsToFullRange) {
  MemoryPrint bit1, bit8;
  ASSERT_TRUE(convert(res("gray1_checker_8x8.png"), bit1, 8, 8));
  ASSERT_TRUE(convert(res("gray8_checker_8x8.png"), bit8, 8, 8));
  EXPECT_EQ(bit1.bytes, bit8.bytes);
}

TEST(PngDecode, FourBitGrayScalesBy17) {
  MemoryPrint bit4, bit8;
  ASSERT_TRUE(convert(res("gray4_ramp_8x8.png"), bit4, 8, 8));
  ASSERT_TRUE(convert(res("gray8_ramp4_8x8.png"), bit8, 8, 8));
  EXPECT_EQ(bit4.bytes, bit8.bytes);
}

TEST(PngDecode, PaletteLookupWithOutOfRangeIndexClampedToEntryZero) {
  // palette8_oob uses a 4-entry gray palette plus index 200 in the last two
  // columns; the reference gray image bakes in the clamp-to-entry-0 rule.
  MemoryPrint pal, ref;
  ASSERT_TRUE(convert(res("palette8_oob_8x8.png"), pal, 8, 8));
  ASSERT_TRUE(convert(res("gray8_palette_ref_8x8.png"), ref, 8, 8));
  EXPECT_EQ(pal.bytes, ref.bytes);
}

TEST(PngDecode, FourBitPaletteWithOutOfRangeIndex) {
  MemoryPrint pal, ref;
  ASSERT_TRUE(convert(res("palette4_oob_4x4.png"), pal, 4, 4));
  ASSERT_TRUE(convert(res("gray8_palette4_ref_4x4.png"), ref, 4, 4));
  EXPECT_EQ(pal.bytes, ref.bytes);
}

// Pinned lenient behavior: a palette image with no PLTE chunk decodes through
// the zero-initialized palette -- every pixel comes out black.
TEST(PngDecode, PaletteWithoutPlteDecodesAllBlack) {
  MemoryPrint pal, ref;
  ASSERT_TRUE(convert(res("palette8_no_plte_8x8.png"), pal, 8, 8));
  ASSERT_TRUE(convert(res("gray8_flat_black_8x8.png"), ref, 8, 8));
  EXPECT_EQ(pal.bytes, ref.bytes);
}

TEST(PngDecode, OversizedPlteClampsTo256Entries) {
  MemoryPrint big, normal;
  ASSERT_TRUE(convert(res("palette8_plte_oversized_8x8.png"), big, 8, 8));
  ASSERT_TRUE(convert(res("palette8_oob_8x8.png"), normal, 8, 8));
  EXPECT_EQ(big.bytes, normal.bytes);
}

TEST(PngDecode, SplitIdatWithInterveningAncillaryChunk) {
  EXPECT_EQ(decodeRef("gray8_ramp_16x16_split_idat.png"), decodeRef("gray8_ramp_16x16.png"));
}

TEST(PngDecode, AncillaryChunksBeforeIdatSkipped) {
  EXPECT_EQ(decodeRef("gray8_ramp_16x16_ancillary.png"), decodeRef("gray8_ramp_16x16.png"));
}

// Pinned behavior: chunk CRCs are never verified on this streaming path (a
// deliberate device-side trade-off -- corruption surfaces as a deflate error
// instead). A corrupted IDAT CRC therefore decodes identically.
TEST(PngDecode, IdatCrcMismatchStillDecodes) {
  EXPECT_EQ(decodeRef("gray8_ramp_16x16_idat_bad_crc.png"), decodeRef("gray8_ramp_16x16.png"));
}

// Pinned behavior: the IHDR length field is read but never validated against
// the actual 13-byte body; parsing uses fixed offsets.
TEST(PngDecode, LyingIhdrLengthFieldStillDecodes) {
  EXPECT_EQ(decodeRef("gray8_ramp_16x16_ihdr_len_lying.png"), decodeRef("gray8_ramp_16x16.png"));
}

// Pinned behavior: an IDAT length field that overshoots the file by 4 bytes
// still decodes, because every scanline inflates before the overshoot is
// ever read (the deflate stream itself is complete). The extra "compressed"
// bytes (really the CRC) are simply never consumed.
TEST(PngDecode, LyingIdatLengthFieldStillDecodesWhenStreamIsComplete) {
  MemoryPrint lying, ref;
  ASSERT_TRUE(convert(res("idat_len_lying.png"), lying, 8, 8));
  ASSERT_TRUE(convert(res("gray8_flat_white_8x8.png"), ref, 8, 8));
  EXPECT_EQ(lying.bytes, ref.bytes);
}

// ---------------------------------------------------------------------------
// Golden regression pins. The full-output FNV-1a checksums were captured from
// the current implementation after the structural assertions above were in
// place; they pin the dither pipeline (Atkinson serpentine state, palette,
// header bytes) against silent drift.
// ---------------------------------------------------------------------------

TEST(PngDecode, GoldenChecksumRamp16NoScaling) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_ramp_16x16.png"), out, 16, 16));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(imgtest::fnv1a64(out.bytes), 0x383E8920B13BDED1ULL);
}

TEST(PngDecode, GoldenChecksumRamp32DownscaledTo8) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_ramp_32x32.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 8);
  EXPECT_EQ(bmp.absHeight(), 8);
  EXPECT_EQ(imgtest::fnv1a64(out.bytes), 0xAD5A0A2C5D81BC7EULL);
}

// ---------------------------------------------------------------------------
// Scaling geometry
// ---------------------------------------------------------------------------

TEST(PngDecode, DownscaleProducesRequestedSizeAndStaysWhite) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_flat_white_32x32.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 8);
  EXPECT_EQ(bmp.absHeight(), 8);
  expectAllPixels(bmp, 3);
}

TEST(PngDecode, UpscaleProducesRequestedSizeAndStaysWhite) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_flat_white_4x4.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 8);
  EXPECT_EQ(bmp.absHeight(), 8);
  expectAllPixels(bmp, 3);
}

// Pinned behavior: the target is "cover" (crop) semantics -- the scale factor
// is the LARGER of the two axis ratios, so a 16x8 source asked to fill 8x8
// keeps scale 1.0 and emits 16x8 (the consumer crops).
TEST(PngDecode, CropScalingKeepsAspectAndOvershootsTarget) {
  MemoryPrint out;
  ASSERT_TRUE(convert(res("gray8_flat_white_16x8.png"), out, 8, 8));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 16);
  EXPECT_EQ(bmp.absHeight(), 8);
  expectAllPixels(bmp, 3);
}

TEST(PngDecode, DefaultApiUsesSwappedDisplayGeometry) {
  // Stub display is 24x32 (w x h); the cover path swaps to target 32x24 and
  // crops: a 16x8 source scales by max(2, 3) = 3 -> 48x24.
  HalFile file;
  ASSERT_TRUE(file.open(res("gray8_flat_white_16x8.png").c_str(), "rb"));
  MemoryPrint out;
  ASSERT_TRUE(PngToBmpConverter::pngFileToBmpStream(file, out));
  ParsedBmp bmp;
  ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp));
  EXPECT_EQ(bmp.width, 48);
  EXPECT_EQ(bmp.absHeight(), 24);
  expectAllPixels(bmp, 3);
}

// ---------------------------------------------------------------------------
// Malformed inputs: every one returns false without crashing. The whole
// suite runs ASan/UBSan-clean via bin/run-tests --asan.
// ---------------------------------------------------------------------------

class PngRejects : public ::testing::TestWithParam<const char*> {};

TEST_P(PngRejects, ReturnsFalse) {
  MemoryPrint out;
  EXPECT_FALSE(convert(res(GetParam()), out, 16, 16));
}

INSTANTIATE_TEST_SUITE_P(All, PngRejects,
                         ::testing::Values("not_a_png.bin",             // wrong magic
                                           "interlaced_gray8_8x8.png",  // Adam7 unsupported (pinned)
                                           "comp_method1.png",          // compression method != 0
                                           "filter_method1.png",        // filter method != 0
                                           "colortype7.png",            // unknown color type
                                           "zero_width.png",            //
                                           "zero_height.png",           //
                                           "width_2049.png",            // > MAX_IMAGE_WIDTH
                                           "height_3073.png",           // > MAX_IMAGE_HEIGHT
                                           "width_huge.png",            // 2^30 wide
                                           "corrupt_deflate.png",       // garbage after zlib header
                                           "corrupt_zlib_header.png",   //
                                           "no_idat.png"),              //
                         [](const auto& info) {
                           std::string n = info.param;
                           for (auto& c : n)
                             if (c == '.' || c == '-') c = '_';
                           return n;
                         });

TEST(PngDecode, EmptyAndTinyFilesRejected) {
  MemoryPrint out;
  EXPECT_FALSE(convertBytes({}, out, 8, 8, "empty"));
  MemoryPrint out2;
  EXPECT_FALSE(convertBytes({0x89}, out2, 8, 8, "onebyte"));
  MemoryPrint out3;
  EXPECT_FALSE(convertBytes({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, out3, 8, 8, "sigonly"));
}

// Lying dimensions WITHIN the caps: IHDR claims more rows/columns than the
// IDAT stream provides. Chunk CRCs are unverified, so in-place IHDR surgery
// is honored; the decode must fail when the scanlines run out -- not crash.
TEST(PngDecode, LyingIhdrHeightRunsOutOfScanlinesCleanly) {
  std::vector<uint8_t> png = imgtest::readFileBytes(res("gray8_flat_white_8x8.png"));
  ASSERT_GT(png.size(), 24u);
  png[22] = 0;
  png[23] = 16;  // height 8 -> 16 (IHDR bytes 20..23, big-endian)
  MemoryPrint out;
  EXPECT_FALSE(convertBytes(png, out, 8, 8, "lying_height"));
}

TEST(PngDecode, LyingIhdrWidthRunsOutOfScanlinesCleanly) {
  std::vector<uint8_t> png = imgtest::readFileBytes(res("gray8_flat_white_8x8.png"));
  ASSERT_GT(png.size(), 24u);
  png[18] = 0;
  png[19] = 16;  // width 8 -> 16 (IHDR bytes 16..19, big-endian)
  MemoryPrint out;
  EXPECT_FALSE(convertBytes(png, out, 8, 8, "lying_width"));
}

// Truncate the base fixture at EVERY byte offset. Each attempt must terminate
// without crashing; if it reports success (possible once all scanlines were
// inflatable), the emitted BMP must still be structurally complete.
TEST(PngDecode, TruncationAtEveryByteOffsetIsSafe) {
  const std::vector<uint8_t> base = imgtest::readFileBytes(res("gray8_ramp_16x16.png"));
  ASSERT_GT(base.size(), 50u);
  int successes = 0;
  for (size_t len = 0; len < base.size(); len++) {
    std::vector<uint8_t> cut(base.begin(), base.begin() + len);
    MemoryPrint out;
    const bool ok = convertBytes(cut, out, 16, 16, "trunc");
    if (ok) {
      successes++;
      ParsedBmp bmp;
      ASSERT_TRUE(imgtest::parseBmp(out.bytes, bmp)) << "truncation at " << len << " returned success "
                                                     << "but wrote a malformed BMP";
    }
  }
  // Sanity: the sweep exercised both outcomes (deep truncations succeed once
  // the final IDAT bytes only feed the unread CRC/IEND tail).
  EXPECT_GT(successes, 0);
  EXPECT_LT(successes, static_cast<int>(base.size()));
}

// ---------------------------------------------------------------------------
// Allocation bounds (AllocGuard): rejected inputs must be turned away before
// any attacker-sized buffer is allocated, and a normal decode must stay
// within the small streaming budget the 380KB device depends on.
// ---------------------------------------------------------------------------

TEST(PngDecode, HugeDimsRejectedBeforeAnyLargeAllocation) {
  for (const char* name : {"width_huge.png", "width_2049.png", "height_3073.png", "zero_width.png"}) {
    HalFile file;
    ASSERT_TRUE(file.open(res(name).c_str(), "rb")) << name;
    primeFile(file);
    MemoryPrint out;
    size_t maxAlloc;
    bool ok;
    {
      allocguard::TrackScope guard;
      ok = PngToBmpConverter::pngFileToBmpStreamWithSize(file, out, 16, 16);
      maxAlloc = allocguard::maxSingle;
    }
    EXPECT_FALSE(ok) << name;
    EXPECT_LT(maxAlloc, 16u * 1024) << name << ": rejected input allocated a large buffer";
  }
}

TEST(PngDecode, ValidDecodeStaysWithinStreamingBudget) {
  HalFile file;
  ASSERT_TRUE(file.open(res("gray8_ramp_32x32.png").c_str(), "rb"));
  primeFile(file);
  MemoryPrint out;
  out.bytes.reserve(4096);  // keep the capture vector's growth out of the trace
  size_t maxAlloc, totalAlloc;
  bool ok;
  {
    allocguard::TrackScope guard;
    ok = PngToBmpConverter::pngFileToBmpStreamWithSize(file, out, 8, 8);
    maxAlloc = allocguard::maxSingle;
    totalAlloc = allocguard::totalBytes;
  }
  EXPECT_TRUE(ok);
  // Largest single block is the 32KB inflate back-reference window.
  EXPECT_LE(maxAlloc, 33u * 1024);
  EXPECT_LE(totalAlloc, 128u * 1024);
}

// ---------------------------------------------------------------------------
// IHDR bit-depth validation (regression guard). The PNG spec (ISO/IEC 15948
// s11.2.2) ties the legal bit depths to the color type: grayscale allows
// {1,2,4,8,16}, palette {1,2,4,8}, and RGB / gray+alpha / RGBA {8,16}. The
// converter must reject anything else at header parse. Before the fix,
// crafted depths reached convertScanlineToGray where `8 / bitDepth` divided
// by zero (depth 0 gray; depth 16 palette via ppb == 0) and the x/ppb
// indexing read past the scanline buffer (depth 3 gray, depth 4 RGB).
// ---------------------------------------------------------------------------

class PngBitDepthValidation : public ::testing::TestWithParam<const char*> {};

TEST_P(PngBitDepthValidation, IllegalDepthForColorTypeIsRejected) {
  MemoryPrint out;
  EXPECT_FALSE(convert(res(GetParam()), out, 16, 16));
  // Rejection happens at header parse: no BMP bytes may be emitted.
  EXPECT_TRUE(out.bytes.empty());
}

INSTANTIATE_TEST_SUITE_P(All, PngBitDepthValidation,
                         ::testing::Values("bitdepth0_gray_8x8.png", "bitdepth3_gray_8x4.png",
                                           "bitdepth16_palette_4x2.png", "bitdepth4_rgb_4x2.png"),
                         [](const auto& info) {
                           std::string n = info.param;
                           for (auto& c : n)
                             if (c == '.' || c == '-') c = '_';
                           return n;
                         });

TEST(PngDecode, IllegalIhdrBitDepthSweepIsRejected) {
  // Byte surgery on a valid grayscale fixture: the IHDR bit-depth byte sits
  // at offset 24 (8 signature + 4 length + 4 "IHDR" + 4 width + 4 height).
  // Every depth outside {1,2,4,8,16} must be rejected without output. The
  // converter skips the IHDR CRC, so no checksum re-fix is needed.
  std::vector<uint8_t> base = imgtest::readFileBytes(res("gray8_ramp_16x16.png"));
  ASSERT_GT(base.size(), 25u);
  ASSERT_EQ(base[24], 8u);  // pristine fixture is 8-bit grayscale
  const uint8_t badDepths[] = {0, 3, 5, 6, 7, 9, 12, 15, 17, 32, 64, 255};
  for (const uint8_t depth : badDepths) {
    std::vector<uint8_t> patched = base;
    patched[24] = depth;
    MemoryPrint out;
    EXPECT_FALSE(convertBytes(patched, out, 16, 16, "baddepth")) << "depth " << int(depth);
    EXPECT_TRUE(out.bytes.empty()) << "depth " << int(depth);
  }
}

TEST(PngDecode, GrayLegalDepth16OnPaletteColorTypeIsRejected) {
  // Depth 16 is legal for grayscale but NOT for palette images: the legality
  // check must be per color type, not just set membership.
  MemoryPrint out;
  EXPECT_FALSE(convert(res("bitdepth16_palette_4x2.png"), out, 16, 16));
  EXPECT_TRUE(out.bytes.empty());
}

// The 2KB IDAT read buffer and 768-byte palette (PngDecodeContext, ~2.9KB) live
// on the heap, not in pngFileToBmpStreamInternal's frame, where they were larger
// than a whole 2KB FreeRTOS task stack. The output Print is called from inside
// the decode, so it samples how deep the stack actually goes.
class StackProbePrint : public Print {
 public:
  size_t write(uint8_t b) override {
    sample();
    bytes.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* data, size_t length) override {
    sample();
    bytes.insert(bytes.end(), data, data + length);
    return length;
  }

  size_t depthFrom(const void* anchor) const {
    if (deepest_ == 0) return 0;
    return static_cast<size_t>(reinterpret_cast<uintptr_t>(anchor) - deepest_);
  }

  std::vector<uint8_t> bytes;

 private:
  void sample() {
    const char here = 0;
    const auto addr = reinterpret_cast<uintptr_t>(&here);
    if (deepest_ == 0 || addr < deepest_) deepest_ = addr;
  }

  uintptr_t deepest_ = 0;
};

// Stack budget for the PNG decode, measured from the test's frame down to the
// deepest point inside the output Print. Generous enough for the sanitizer
// build's frame padding, far below the ~2.9KB context this used to hold.
constexpr size_t kMaxPngDecodeStackBytes = 1200;

#ifndef __has_feature
#define __has_feature(x) 0
#endif

// AddressSanitizer inserts a redzone around every local in every frame on the
// path, so a measured depth reflects instrumentation rather than the production
// frame. The budget assertion skips itself under it.
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
constexpr bool kStackMeasurementIsReliable = false;
#else
constexpr bool kStackMeasurementIsReliable = true;
#endif

TEST(PngDecode, DecodeRunsInASmallStackFrame) {
  if (!kStackMeasurementIsReliable) {
    GTEST_SKIP() << "AddressSanitizer pads every frame on the path; the measurement is not the production frame";
  }

  StackProbePrint out;
  HalFile file;
  ASSERT_TRUE(file.open(res("gray8_ramp_16x16.png").c_str(), "rb"));

  const char anchor = 0;
  ASSERT_TRUE(PngToBmpConverter::pngFileToBmpStreamWithSize(file, out, 16, 16));
  const size_t depth = out.depthFrom(&anchor);

  ASSERT_GT(depth, 0u) << "the output Print was never written to; the probe measured nothing";
  EXPECT_LT(depth, kMaxPngDecodeStackBytes);
}

}  // namespace
