// lib/GfxRenderer/Bitmap.cpp header validation and row decoding, plus the
// createBmpHeader writer in lib/GfxRenderer/BitmapHelpers.cpp.
//
// Every fixture is built byte by byte in the test and written into a per-test
// sandbox directory, so the malformed cases (bad magic, lying dimensions,
// oversized palettes, truncated pixel data) exercise the real file reads.

#include <Bitmap.h>
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

// A well-formed 40-byte-DIB BMP, mutable field by field before serialisation.
struct BmpSpec {
  uint16_t magic = 0x4D42;
  uint32_t fileSize = 0;  // 0 = computed
  uint32_t offBits = 0;   // 0 = computed
  uint32_t dibSize = 40;
  int32_t width = 4;
  int32_t height = 2;
  uint16_t planes = 1;
  uint16_t bpp = 1;
  uint32_t compression = 0;
  uint32_t sizeImage = 0;
  uint32_t clrUsed = 0;         // as written into the header
  uint32_t paletteEntries = 2;  // how many entries actually get written
  std::vector<uint8_t> palette;   // BGRA quads; default black/white when empty
  std::vector<uint8_t> pixelData;  // padded rows, bottom-up unless height < 0
  int truncateTo = -1;             // >= 0: cut the serialised file to this length
};

void push16(std::vector<uint8_t>& out, const uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void push32(std::vector<uint8_t>& out, const uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

std::vector<uint8_t> serialise(const BmpSpec& spec) {
  std::vector<uint8_t> out;
  const std::vector<uint8_t> defaultPalette = {0, 0, 0, 0, 255, 255, 255, 0};
  const std::vector<uint8_t>& palette = spec.palette.empty() ? defaultPalette : spec.palette;
  const size_t paletteBytes = spec.palette.empty() ? spec.paletteEntries * 4 : palette.size();

  push16(out, spec.magic);
  push32(out, spec.fileSize);
  push16(out, 0);
  push16(out, 0);
  push32(out, spec.offBits != 0 ? spec.offBits : static_cast<uint32_t>(54 + paletteBytes));
  push32(out, spec.dibSize);
  push32(out, static_cast<uint32_t>(spec.width));
  push32(out, static_cast<uint32_t>(spec.height));
  push16(out, spec.planes);
  push16(out, spec.bpp);
  push32(out, spec.compression);
  push32(out, spec.sizeImage);
  push32(out, 2835);
  push32(out, 2835);
  push32(out, spec.clrUsed);
  push32(out, 0);
  for (size_t i = 0; i < paletteBytes; i++) out.push_back(i < palette.size() ? palette[i] : 0);
  out.insert(out.end(), spec.pixelData.begin(), spec.pixelData.end());
  if (spec.truncateTo >= 0 && static_cast<size_t>(spec.truncateTo) < out.size()) {
    out.resize(static_cast<size_t>(spec.truncateTo));
  }
  return out;
}

class BitmapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Per-process: ctest runs each test as its own process, in parallel, and a
    // shared sandbox would have them deleting each other's fixtures.
    sandbox_ = std::string(GFX_SANDBOX_DIR) + "/bitmap_" + std::to_string(::getpid());
    std::error_code ec;
    std::filesystem::remove_all(sandbox_, ec);
    std::filesystem::create_directories(sandbox_, ec);
    halstub::root = sandbox_;
  }

  void TearDown() override {
    halstub::root.clear();
    std::error_code ec;
    std::filesystem::remove_all(sandbox_, ec);
  }

  // Writes `bytes` to /img.bmp inside the sandbox and opens it for reading.
  void writeFile(const std::vector<uint8_t>& bytes, HalFile& file) const {
    const std::string path = sandbox_ + "/img.bmp";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    if (!bytes.empty()) ASSERT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), f), bytes.size());
    std::fclose(f);
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/img.bmp", file));
  }

  std::string sandbox_;
};

// ---------------------------------------------------------------------------
// createBmpHeader
// ---------------------------------------------------------------------------

// BmpHeader is 1-byte packed, so every multi-byte field is read out through a
// local copy: binding a reference (which EXPECT_EQ does) straight to a packed
// member is undefined behaviour and trips UBSan.
template <typename T>
T field(T loadedValue) {
  return loadedValue;
}

TEST(CreateBmpHeader, WritesA62ByteOneBitHeaderWithABlackWhitePalette) {
  BmpHeader header{};
  std::memset(&header, 0xAA, sizeof(header));
  createBmpHeader(&header, 16, 4, BmpRowOrder::BottomUp);

  ASSERT_EQ(sizeof(BmpHeader), 62u);  // 14 file + 40 DIB + 2 palette entries
  EXPECT_EQ(field(header.fileHeader.bfType), 0x4D42);
  EXPECT_EQ(field(header.fileHeader.bfOffBits), sizeof(BmpHeader));
  EXPECT_EQ(field(header.fileHeader.bfReserved1), 0);
  EXPECT_EQ(field(header.fileHeader.bfReserved2), 0);
  EXPECT_EQ(field(header.infoHeader.biSize), 40u);
  EXPECT_EQ(field(header.infoHeader.biWidth), 16);
  EXPECT_EQ(field(header.infoHeader.biHeight), 4);
  EXPECT_EQ(field(header.infoHeader.biPlanes), 1);
  EXPECT_EQ(field(header.infoHeader.biBitCount), 1);
  EXPECT_EQ(field(header.infoHeader.biCompression), 0u);
  EXPECT_EQ(field(header.infoHeader.biSizeImage), 16u);  // 4-byte rows x 4
  EXPECT_EQ(field(header.fileHeader.bfSize), sizeof(BmpHeader) + 16u);
  EXPECT_EQ(field(header.infoHeader.biXPelsPerMeter), 2835);
  EXPECT_EQ(field(header.infoHeader.biYPelsPerMeter), 2835);
  EXPECT_EQ(field(header.infoHeader.biClrUsed), 2u);
  EXPECT_EQ(field(header.infoHeader.biClrImportant), 2u);
  EXPECT_EQ(field(header.colors[0].rgbBlue), 0);
  EXPECT_EQ(field(header.colors[0].rgbGreen), 0);
  EXPECT_EQ(field(header.colors[0].rgbRed), 0);
  EXPECT_EQ(field(header.colors[1].rgbBlue), 255);
  EXPECT_EQ(field(header.colors[1].rgbGreen), 255);
  EXPECT_EQ(field(header.colors[1].rgbRed), 255);
  EXPECT_EQ(field(header.colors[1].rgbReserved), 0);
}

TEST(CreateBmpHeader, TopDownOrderIsEncodedAsANegativeHeight) {
  BmpHeader header{};
  createBmpHeader(&header, 8, 5, BmpRowOrder::TopDown);
  EXPECT_EQ(field(header.infoHeader.biHeight), -5);
  EXPECT_EQ(field(header.infoHeader.biSizeImage), 20u);  // 4-byte rows x 5
}

TEST(CreateBmpHeader, RowSizeIsPaddedToFourByteMultiples) {
  BmpHeader header{};
  createBmpHeader(&header, 1, 1, BmpRowOrder::BottomUp);
  EXPECT_EQ(field(header.infoHeader.biSizeImage), 4u);
  createBmpHeader(&header, 32, 1, BmpRowOrder::BottomUp);
  EXPECT_EQ(field(header.infoHeader.biSizeImage), 4u);
  createBmpHeader(&header, 33, 1, BmpRowOrder::BottomUp);
  EXPECT_EQ(field(header.infoHeader.biSizeImage), 8u);
}

TEST(CreateBmpHeader, NullTargetIsANoOp) {
  createBmpHeader(nullptr, 8, 8, BmpRowOrder::TopDown);  // must not crash
  SUCCEED();
}

// ---------------------------------------------------------------------------
// parseHeaders: accepted shapes
// ---------------------------------------------------------------------------

TEST_F(BitmapTest, ParsesAMinimalOneBitBitmap) {
  BmpSpec spec;
  spec.pixelData = std::vector<uint8_t>(8, 0);  // 2 rows x 4 padded bytes
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_EQ(bmp.getWidth(), 4);
  EXPECT_EQ(bmp.getHeight(), 2);
  EXPECT_FALSE(bmp.isTopDown());
  EXPECT_TRUE(bmp.is1Bit());
  EXPECT_FALSE(bmp.hasGreyscale());
  EXPECT_EQ(bmp.getBpp(), 1);
  EXPECT_EQ(bmp.getRowBytes(), 4);
}

TEST_F(BitmapTest, NegativeHeightMeansTopDownAndIsStoredAsMagnitude) {
  BmpSpec spec;
  spec.height = -3;
  spec.pixelData = std::vector<uint8_t>(12, 0);
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bmp.isTopDown());
  EXPECT_EQ(bmp.getHeight(), 3);
}

TEST_F(BitmapTest, RowBytesFollowWidthTimesBppPaddedToFourBytes) {
  const struct {
    int32_t width;
    uint16_t bpp;
    uint32_t paletteEntries;
    int expected;
  } cases[] = {{17, 1, 2, 4}, {3, 24, 0, 12}, {5, 8, 256, 8}, {4, 4, 16, 4}, {5, 2, 4, 4}};
  for (const auto& c : cases) {
    BmpSpec spec;
    spec.width = c.width;
    spec.height = 1;
    spec.bpp = c.bpp;
    spec.paletteEntries = c.paletteEntries;
    spec.palette.assign(c.paletteEntries * 4, 0);
    spec.pixelData = std::vector<uint8_t>(static_cast<size_t>(c.expected), 0);
    HalFile file;
    writeFile(serialise(spec), file);
    Bitmap bmp(file);
    ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok) << "bpp " << c.bpp;
    EXPECT_EQ(bmp.getRowBytes(), c.expected) << "width " << c.width << " bpp " << c.bpp;
  }
}

TEST_F(BitmapTest, ThirtyTwoBitBitfieldsCompressionIsAccepted) {
  BmpSpec spec;
  spec.bpp = 32;
  spec.compression = 3;  // BI_BITFIELDS
  spec.paletteEntries = 0;
  spec.palette.assign(1, 0);
  spec.palette.clear();
  spec.width = 2;
  spec.height = 1;
  spec.pixelData = std::vector<uint8_t>(8, 0);
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bmp.hasGreyscale());
}

TEST_F(BitmapTest, ZeroColoursUsedDefaultsToTwoToThePowerOfBpp) {
  // 4 bpp with clrUsed == 0 means 16 palette entries; the pixel data must
  // therefore start at 54 + 64.
  BmpSpec spec;
  spec.bpp = 4;
  spec.width = 2;
  spec.height = 1;
  spec.clrUsed = 0;
  spec.paletteEntries = 16;
  spec.palette.assign(64, 0);
  spec.palette[4 * 1 + 0] = 255;  // entry 1 = white
  spec.palette[4 * 1 + 1] = 255;
  spec.palette[4 * 1 + 2] = 255;
  spec.pixelData = {0x10, 0, 0, 0};  // pixel0 = index 1, pixel1 = index 0
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[4] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0] >> 6, 3);           // white
  EXPECT_EQ((out[0] >> 4) & 0x3, 0);   // black
}

// ---------------------------------------------------------------------------
// parseHeaders: malformed input (Constitution VI)
// ---------------------------------------------------------------------------

TEST_F(BitmapTest, ClosedFileIsRejectedBeforeAnyRead) {
  HalFile file;
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::FileInvalid);
}

TEST_F(BitmapTest, EmptyFileIsRejectedAsNotBmp) {
  HalFile file;
  writeFile({}, file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::NotBMP);
}

TEST_F(BitmapTest, BadMagicIsRejected) {
  BmpSpec spec;
  spec.magic = 0x4142;  // "AB"
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::NotBMP);
}

TEST_F(BitmapTest, HeaderTruncatedAfterTheMagicIsRejected) {
  BmpSpec spec;
  spec.truncateTo = 2;
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::DIBTooSmall);
}

TEST_F(BitmapTest, DibHeaderSmallerThanFortyBytesIsRejected) {
  BmpSpec spec;
  spec.dibSize = 12;  // BITMAPCOREHEADER
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::DIBTooSmall);
}

TEST_F(BitmapTest, PlaneCountsOtherThanOneAreRejected) {
  for (const uint16_t planes : {uint16_t{0}, uint16_t{2}, uint16_t{0xFFFF}}) {
    BmpSpec spec;
    spec.planes = planes;
    HalFile file;
    writeFile(serialise(spec), file);
    Bitmap bmp(file);
    EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::BadPlanes) << "planes " << planes;
  }
}

TEST_F(BitmapTest, UnsupportedBitDepthsAreRejected) {
  for (const uint16_t bpp : {uint16_t{0}, uint16_t{3}, uint16_t{16}, uint16_t{64}, uint16_t{0xFFFF}}) {
    BmpSpec spec;
    spec.bpp = bpp;
    HalFile file;
    writeFile(serialise(spec), file);
    Bitmap bmp(file);
    EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::UnsupportedBpp) << "bpp " << bpp;
  }
}

TEST_F(BitmapTest, CompressedVariantsOtherThanThirtyTwoBitBitfieldsAreRejected) {
  BmpSpec rle;
  rle.bpp = 8;
  rle.compression = 1;  // BI_RLE8
  rle.paletteEntries = 256;
  rle.palette.assign(1024, 0);
  HalFile file;
  writeFile(serialise(rle), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::UnsupportedCompression);

  BmpSpec bitfields24;
  bitfields24.bpp = 24;
  bitfields24.compression = 3;
  bitfields24.paletteEntries = 0;
  HalFile file2;
  writeFile(serialise(bitfields24), file2);
  Bitmap bmp2(file2);
  EXPECT_EQ(bmp2.parseHeaders(), BmpReaderError::UnsupportedCompression);
}

TEST_F(BitmapTest, PaletteLargerThanTwoHundredFiftySixEntriesIsRejected) {
  BmpSpec spec;
  spec.bpp = 8;
  spec.clrUsed = 257;
  spec.paletteEntries = 2;
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::PaletteTooLarge);
}

TEST_F(BitmapTest, HugeDeclaredPaletteCountIsRejectedBeforeAnyAllocation) {
  BmpSpec spec;
  spec.bpp = 8;
  spec.clrUsed = 0xFFFFFFFFu;
  spec.paletteEntries = 2;
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::PaletteTooLarge);
}

TEST_F(BitmapTest, ZeroAndNegativeDimensionsAreRejected) {
  const std::pair<int32_t, int32_t> cases[] = {{0, 2}, {4, 0}, {0, 0}, {-4, 2}, {-4, -2}};
  for (const auto& [w, h] : cases) {
    BmpSpec spec;
    spec.width = w;
    spec.height = h;
    HalFile file;
    writeFile(serialise(spec), file);
    Bitmap bmp(file);
    EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::BadDimensions) << w << "x" << h;
  }
}

TEST_F(BitmapTest, DimensionsBeyondTheEsp32SafetyLimitsAreRejected) {
  BmpSpec wide;
  wide.width = 2049;
  wide.height = 1;
  HalFile file;
  writeFile(serialise(wide), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::ImageTooLarge);

  BmpSpec tall;
  tall.width = 1;
  tall.height = 3073;
  HalFile file2;
  writeFile(serialise(tall), file2);
  Bitmap bmp2(file2);
  EXPECT_EQ(bmp2.parseHeaders(), BmpReaderError::ImageTooLarge);

  BmpSpec atLimit;
  atLimit.width = 2048;
  atLimit.height = 3072;
  HalFile file3;
  writeFile(serialise(atLimit), file3);
  Bitmap bmp3(file3);
  EXPECT_EQ(bmp3.parseHeaders(), BmpReaderError::Ok) << "the limits themselves stay valid";
}

TEST_F(BitmapTest, PaletteTruncatedByTheEndOfTheFileIsRejected) {
  // 54-byte header plus 2 of the declared palette's 8 bytes: the palette runs
  // past EOF, so the pixel-data seek is the first thing that can fail.
  BmpSpec spec;
  spec.pixelData = std::vector<uint8_t>(8, 0);
  spec.truncateTo = 56;
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::SeekPixelDataFailed);
}

TEST_F(BitmapTest, APaletteCountLargerThanTheFileIsSurvivedWithoutOverrunningTheBuffer) {
  // biClrUsed lies: 256 entries are declared but only 2 are stored. The parser
  // reads the missing entries off the end of the file (see the unchecked
  // file.read(rgb, 4) in Bitmap.cpp), so only the error code is deterministic --
  // the resulting palette luminances are not asserted on.
  BmpSpec spec;
  spec.bpp = 8;
  spec.width = 4;
  spec.height = 1;
  spec.clrUsed = 256;
  spec.paletteEntries = 2;
  spec.pixelData = {0, 0, 0, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[1] = {};
  uint8_t row[4] = {};
  EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
}

TEST_F(BitmapTest, PixelDataOffsetPastTheEndOfTheFileIsRejected) {
  BmpSpec spec;
  spec.offBits = 100000;
  spec.pixelData = std::vector<uint8_t>(8, 0);
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  EXPECT_EQ(bmp.parseHeaders(), BmpReaderError::SeekPixelDataFailed);
}

TEST_F(BitmapTest, TruncatedPixelDataIsReportedAsAShortRowRead) {
  BmpSpec spec;
  spec.width = 8;
  spec.height = 2;
  spec.pixelData = std::vector<uint8_t>(8, 0);
  spec.truncateTo = 62 + 6;  // header + palette + 6 of the 8 pixel bytes
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[4] = {};
  uint8_t row[4] = {};
  EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok) << "the first row is complete";
  EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::ShortReadRow);
  EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::ShortReadRow) << "and stays short at EOF";
}

TEST_F(BitmapTest, NoRowsAtAllWhenThePixelSectionIsEmpty) {
  BmpSpec spec;
  spec.width = 8;
  spec.height = 1;
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[4] = {};
  uint8_t row[4] = {};
  EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::ShortReadRow);
}

TEST_F(BitmapTest, ErrorCodesAllHaveDistinctNonEmptyNames) {
  const BmpReaderError all[] = {
      BmpReaderError::Ok,           BmpReaderError::FileInvalid,          BmpReaderError::SeekStartFailed,
      BmpReaderError::NotBMP,       BmpReaderError::DIBTooSmall,          BmpReaderError::BadPlanes,
      BmpReaderError::UnsupportedBpp, BmpReaderError::UnsupportedCompression, BmpReaderError::BadDimensions,
      BmpReaderError::ImageTooLarge, BmpReaderError::PaletteTooLarge,     BmpReaderError::SeekPixelDataFailed,
      BmpReaderError::BufferTooSmall, BmpReaderError::OomRowBuffer,       BmpReaderError::ShortReadRow,
  };
  std::vector<std::string> names;
  for (const auto err : all) {
    const char* name = Bitmap::errorToString(err);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(std::string(name), "Unknown");
    names.emplace_back(name);
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(std::unique(names.begin(), names.end()), names.end());
  EXPECT_EQ(std::string(Bitmap::errorToString(static_cast<BmpReaderError>(200))), "Unknown");
}

// ---------------------------------------------------------------------------
// readNextRow / rewindToData
// ---------------------------------------------------------------------------

TEST_F(BitmapTest, OneBitRowsExpandToTwoBitOutputThroughThePalette) {
  BmpSpec spec;
  spec.width = 8;
  spec.height = 1;
  spec.pixelData = {0b10100000, 0, 0, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[2] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  // Palette index 1 (white, level 3) then 0 (black, level 0), alternating.
  EXPECT_EQ(out[0], 0b11001100);
  EXPECT_EQ(out[1], 0b00000000);
}

TEST_F(BitmapTest, AnInvertedPaletteFlipsTheDecodedLevels) {
  BmpSpec spec;
  spec.width = 8;
  spec.height = 1;
  spec.palette = {255, 255, 255, 0, 0, 0, 0, 0};  // entry 0 white, entry 1 black
  spec.pixelData = {0b10100000, 0, 0, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[2] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0], 0b00110011);
}

TEST_F(BitmapTest, RewindToDataReplaysTheSameFirstRow) {
  BmpSpec spec;
  spec.width = 8;
  spec.height = 2;
  spec.pixelData = {0xFF, 0, 0, 0, 0x00, 0, 0, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t first[2] = {};
  uint8_t second[2] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(first, row), BmpReaderError::Ok);
  ASSERT_EQ(bmp.readNextRow(second, row), BmpReaderError::Ok);
  EXPECT_NE(first[0], second[0]);
  ASSERT_EQ(bmp.rewindToData(), BmpReaderError::Ok);
  uint8_t again[2] = {};
  ASSERT_EQ(bmp.readNextRow(again, row), BmpReaderError::Ok);
  EXPECT_EQ(again[0], first[0]);
  EXPECT_EQ(again[1], first[1]);
}

TEST_F(BitmapTest, TwentyFourBitPixelsAreQuantisedByLuminance) {
  BmpSpec spec;
  spec.bpp = 24;
  spec.width = 4;
  spec.height = 1;
  spec.paletteEntries = 0;
  spec.palette.clear();
  spec.clrUsed = 0;
  // BGR triples: black, mid-dark, mid-light, white.
  spec.pixelData = {0, 0, 0, 50, 50, 50, 100, 100, 100, 255, 255, 255};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bmp.hasGreyscale());
  uint8_t out[1] = {};
  uint8_t row[12] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  // quantizeSimple thresholds: <45 -> 0, <70 -> 1, <140 -> 2, else 3.
  EXPECT_EQ(out[0] >> 6, 0);
  EXPECT_EQ((out[0] >> 4) & 3, 1);
  EXPECT_EQ((out[0] >> 2) & 3, 2);
  EXPECT_EQ(out[0] & 3, 3);
}

TEST_F(BitmapTest, DitheringAllocatesAndReleasesItsErrorRowsForHighColourInput) {
  BmpSpec spec;
  spec.bpp = 24;
  spec.width = 4;
  spec.height = 2;
  spec.paletteEntries = 0;
  spec.palette.clear();
  spec.pixelData = std::vector<uint8_t>(24, 128);
  HalFile file;
  writeFile(serialise(spec), file);
  {
    Bitmap bmp(file, /*dithering=*/true);
    ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
    uint8_t out[1] = {};
    uint8_t row[12] = {};
    EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
    EXPECT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
    EXPECT_EQ(bmp.rewindToData(), BmpReaderError::Ok);
  }
  SUCCEED();  // the sanitizer run is the assertion: no leak, no overrun
}

// A grey BGR triple (v, v, v) has luminance exactly v, because the weights
// 77 + 150 + 29 sum to 256.
std::vector<uint8_t> greyPalette(const std::vector<uint8_t>& levels) {
  std::vector<uint8_t> out;
  for (const uint8_t v : levels) {
    out.push_back(v);
    out.push_back(v);
    out.push_back(v);
    out.push_back(0);
  }
  return out;
}

TEST_F(BitmapTest, TwoBitPixelsAreUnpackedFromTheHighBitsDown) {
  BmpSpec spec;
  spec.bpp = 2;
  spec.width = 4;
  spec.height = 1;
  spec.clrUsed = 4;
  spec.paletteEntries = 4;
  spec.palette = greyPalette({0, 85, 170, 255});
  spec.pixelData = {0b00011011, 0, 0, 0};  // indices 0, 1, 2, 3
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_EQ(bmp.getRowBytes(), 4);
  EXPECT_TRUE(bmp.hasGreyscale()) << "hasGreyscale() is just bpp > 1";
  uint8_t out[1] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0], 0b00011011);  // levels 0, 1, 2, 3
}

TEST_F(BitmapTest, EightBitPixelsIndexThePaletteDirectly) {
  BmpSpec spec;
  spec.bpp = 8;
  spec.width = 4;
  spec.height = 1;
  spec.clrUsed = 4;
  spec.paletteEntries = 4;
  spec.palette = greyPalette({0, 85, 170, 255});
  spec.pixelData = {3, 2, 1, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bmp.hasGreyscale());
  uint8_t out[1] = {};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0], 0b11100100);  // levels 3, 2, 1, 0
}

TEST_F(BitmapTest, ThirtyTwoBitPixelsSkipTheAlphaByteAndQuantise) {
  BmpSpec spec;
  spec.bpp = 32;
  spec.compression = 3;  // BI_BITFIELDS
  spec.width = 2;
  spec.height = 1;
  spec.paletteEntries = 0;
  spec.palette.clear();
  // BGRA: black with opaque alpha, then a light grey with zero alpha.
  spec.pixelData = {0, 0, 0, 255, 200, 200, 200, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  EXPECT_EQ(bmp.getRowBytes(), 8);
  uint8_t out[1] = {0};
  uint8_t row[8] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0] >> 6, 0) << "alpha must not reach the luminance sum";
  EXPECT_EQ((out[0] >> 4) & 3, 3);
}

TEST_F(BitmapTest, WidthsThatAreNotAMultipleOfFourFlushTheTrailingOutputByte) {
  BmpSpec spec;
  spec.width = 5;
  spec.height = 1;
  spec.pixelData = {0b11111000, 0, 0, 0};
  HalFile file;
  writeFile(serialise(spec), file);
  Bitmap bmp(file);
  ASSERT_EQ(bmp.parseHeaders(), BmpReaderError::Ok);
  uint8_t out[2] = {0, 0};
  uint8_t row[4] = {};
  ASSERT_EQ(bmp.readNextRow(out, row), BmpReaderError::Ok);
  EXPECT_EQ(out[0], 0xFF);  // four white pixels
  EXPECT_EQ(out[1], 0xC0);  // the fifth, flushed into the partial byte
}

}  // namespace
