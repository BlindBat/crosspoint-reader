// Host tests for the SD-card firmware update path (FR-166/FR-167,
// contracts/firmware-update.md "SD path"):
//   - firmware_flash::validateImageFile: every rejection in contract order,
//     driven by programmatically built ESP32 app images.
//   - firmware_flash::flashFromSdPath: erase-ahead / 4 KiB write cadence,
//     progress reporting and failure propagation against the partition model.
//   - ota_boot::switchTo: otadata slot selection, sequence parity and CRC.
//
// Images are built in-test (buildImage) so each case controls exactly one
// defect. The suite binary is run as one process by bin/run-tests, so the
// running chip id is latched once, before any test, by ChipIdEnvironment.
#include <EspFlashStub.h>
#include <FirmwareFlasher.h>
#include <HalStorage.h>
#include <OtaBootSwitch.h>
#include <PlatformHost.h>
#include <esp_rom_crc.h>
#include <gtest/gtest.h>
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using firmware_flash::Result;
using Bytes = std::vector<uint8_t>;

namespace {

constexpr uint16_t CHIP_ESP32C3 = 0x0005;
constexpr uint16_t CHIP_ESP32S3 = 0x0009;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t SEG_HEADER_SIZE = 8;
constexpr size_t SHA_TRAILER = 32;

// ctest runs each discovered test in its own process in one shared working
// directory, so image files live in a per-process directory.
std::string processRoot() { return "fw_root_" + std::to_string(::getpid()); }

void removeTree(const std::string& path) {
  const std::string cmd = "rm -rf '" + path + "'";
  [[maybe_unused]] const int rc = std::system(cmd.c_str());
}

class RootCleanup : public ::testing::Environment {
 public:
  void SetUp() override {
    removeTree(processRoot());
    ::mkdir(processRoot().c_str(), 0755);
  }
  void TearDown() override { removeTree(processRoot()); }
};

// runningPartitionChipId() caches the running slot's chip id in a function
// static, so it is fixed here before the first test can observe it.
class ChipIdEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    espstub::resetDefaults();
    espstub::running.data[12] = static_cast<uint8_t>(CHIP_ESP32C3 & 0xFF);
    espstub::running.data[13] = static_cast<uint8_t>(CHIP_ESP32C3 >> 8);
    ASSERT_EQ(firmware_flash::runningPartitionChipId(), CHIP_ESP32C3);
  }
};

const ::testing::Environment* const rootCleanup = ::testing::AddGlobalTestEnvironment(new RootCleanup);
const ::testing::Environment* const chipIdEnv = ::testing::AddGlobalTestEnvironment(new ChipIdEnvironment);

Bytes patternBytes(size_t n, uint32_t seed) {
  Bytes out(n);
  uint32_t x = seed;
  for (size_t i = 0; i < n; i++) {
    x = x * 1664525u + 1013904223u;
    out[i] = static_cast<uint8_t>(x >> 24);
  }
  return out;
}

void putU32(Bytes& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

Bytes sha256Of(const uint8_t* data, size_t len) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len);
  Bytes out(SHA_TRAILER);
  mbedtls_sha256_finish(&ctx, out.data());
  mbedtls_sha256_free(&ctx);
  return out;
}

struct ImageSpec {
  uint8_t magic = 0xE9;
  uint16_t chipId = CHIP_ESP32C3;
  bool hashAppended = true;
  int segCountOverride = -1;  // header byte 1 when >= 0
  std::vector<Bytes> segments;
};

// Lays out header, segment table, 16-byte padding carrying the XOR checksum
// (seed 0xEF over segment data) and the optional SHA-256 trailer, exactly as
// esptool does.
Bytes buildImage(const ImageSpec& spec) {
  Bytes img;
  img.push_back(spec.magic);
  img.push_back(static_cast<uint8_t>(spec.segCountOverride >= 0 ? spec.segCountOverride : spec.segments.size()));
  img.push_back(0x02);  // spi mode
  img.push_back(0x40);  // flash size/speed
  putU32(img, 0x40380000u);
  img.push_back(0xEE);  // wp pin
  img.push_back(0);
  img.push_back(0);
  img.push_back(0);
  img.push_back(static_cast<uint8_t>(spec.chipId & 0xFF));
  img.push_back(static_cast<uint8_t>(spec.chipId >> 8));
  while (img.size() < HEADER_SIZE - 1) img.push_back(0);
  img.push_back(spec.hashAppended ? 1 : 0);

  uint8_t xorAccum = 0xEF;
  uint32_t loadAddr = 0x3C000020u;
  for (const Bytes& seg : spec.segments) {
    putU32(img, loadAddr);
    putU32(img, static_cast<uint32_t>(seg.size()));
    img.insert(img.end(), seg.begin(), seg.end());
    for (uint8_t b : seg) xorAccum ^= b;
    loadAddr += 0x10000u;
  }
  const size_t padEnd = (img.size() + 16) & ~static_cast<size_t>(15);
  img.resize(padEnd, 0);
  img[padEnd - 1] = xorAccum;
  if (spec.hashAppended) {
    const Bytes sha = sha256Of(img.data(), img.size());
    img.insert(img.end(), sha.begin(), sha.end());
  }
  return img;
}

// Single-segment image whose body is `bodyLen` pattern bytes (>= 64 KiB by default).
ImageSpec defaultSpec(size_t bodyLen = 66000, bool hashAppended = true) {
  ImageSpec spec;
  spec.hashAppended = hashAppended;
  spec.segments.push_back(patternBytes(bodyLen, 7));
  return spec;
}

std::string writeImage(const char* name, const Bytes& bytes) {
  const std::string path = processRoot() + "/" + name;
  std::FILE* f = std::fopen(path.c_str(), "wb");
  EXPECT_NE(f, nullptr);
  if (f) {
    // bytes.data() may be null when empty; fwrite's pointer is nonnull regardless of the count.
    if (!bytes.empty()) EXPECT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), f), bytes.size());
    std::fclose(f);
  }
  return path;
}

Result validate(const Bytes& bytes, size_t partitionSize = 0) {
  return firmware_flash::validateImageFile(writeImage("img.bin", bytes).c_str(), partitionSize);
}

// Offset of segment 0's data within a single-segment image.
constexpr size_t SEG0_DATA = HEADER_SIZE + SEG_HEADER_SIZE;

// Single-segment body length whose unhashed image is exactly 65536 bytes (the
// MIN_FIRMWARE_SIZE floor) with a single padding byte carrying the checksum.
constexpr size_t BODY_65536 = 65536 - SEG0_DATA - 1;

class FirmwareFlashTest : public ::testing::Test {
 protected:
  void SetUp() override {
    espstub::resetDefaults();
    espstub::running.data[12] = static_cast<uint8_t>(CHIP_ESP32C3 & 0xFF);
    espstub::running.data[13] = static_cast<uint8_t>(CHIP_ESP32C3 >> 8);
    platform_host::resetCounters();
    halstub::readBudget = SIZE_MAX;
  }
  void TearDown() override { halstub::readBudget = SIZE_MAX; }
};

}  // namespace

// ---------------------------------------------------------------------------
// Stub self-checks: the fixtures lean on these primitives.
// ---------------------------------------------------------------------------

TEST_F(FirmwareFlashTest, Sha256StubKnownAnswer) {
  const Bytes sha = sha256Of(reinterpret_cast<const uint8_t*>("abc"), 3);
  const uint8_t expected[SHA_TRAILER] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  EXPECT_EQ(std::memcmp(sha.data(), expected, SHA_TRAILER), 0);
  // Multi-block message (> 64 bytes) exercises the block loop.
  const std::string longMsg(1000, 'a');
  const Bytes shaLong = sha256Of(reinterpret_cast<const uint8_t*>(longMsg.data()), longMsg.size());
  const uint8_t expectedLong[4] = {0x41, 0xed, 0xec, 0xe4};
  EXPECT_EQ(std::memcmp(shaLong.data(), expectedLong, 4), 0);
}

TEST_F(FirmwareFlashTest, Crc32StubMatchesRomSemantics) {
  EXPECT_EQ(esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>("123456789"), 9), 0xCBF43926u);
}

TEST_F(FirmwareFlashTest, ResultNameCoversEveryResult) {
  for (int r = static_cast<int>(Result::OK); r <= static_cast<int>(Result::OTADATA_FAIL); r++) {
    EXPECT_STRNE(firmware_flash::resultName(static_cast<Result>(r)), "?") << r;
  }
  EXPECT_STREQ(firmware_flash::resultName(Result::BAD_CHIP), "BAD_CHIP");
}

TEST_F(FirmwareFlashTest, RunningChipIdIsLatchedOnFirstRead) {
  EXPECT_EQ(firmware_flash::runningPartitionChipId(), CHIP_ESP32C3);
  espstub::running.data[12] = 0x09;
  EXPECT_EQ(firmware_flash::runningPartitionChipId(), CHIP_ESP32C3);
}

// ---------------------------------------------------------------------------
// validateImageFile: rejections in contract order.
// ---------------------------------------------------------------------------

TEST_F(FirmwareFlashTest, ValidateRejectsMissingFile) {
  EXPECT_EQ(firmware_flash::validateImageFile((processRoot() + "/nope.bin").c_str(), 0), Result::OPEN_FAIL);
}

TEST_F(FirmwareFlashTest, ValidateRejectsEmptyFile) { EXPECT_EQ(validate(Bytes{}), Result::TOO_SMALL); }

TEST_F(FirmwareFlashTest, ValidateRejectsImageUnder64KiB) {
  const Bytes img = buildImage(defaultSpec(65000));
  ASSERT_LT(img.size(), 65536u);
  EXPECT_EQ(validate(img), Result::TOO_SMALL);
}

TEST_F(FirmwareFlashTest, ValidateTooSmallWinsOverBadMagic) {
  ImageSpec spec = defaultSpec(1000);
  spec.magic = 0x00;
  EXPECT_EQ(validate(buildImage(spec)), Result::TOO_SMALL);
}

TEST_F(FirmwareFlashTest, ValidateAcceptsExactly64KiB) {
  // 24 header + 8 seg header + 65456 data = 65488; padded to 65504; + 32 SHA = 65536.
  const Bytes img = buildImage(defaultSpec(65456));
  ASSERT_EQ(img.size(), 65536u);
  EXPECT_EQ(validate(img), Result::OK);
}

TEST_F(FirmwareFlashTest, ValidateRejectsImageLargerThanPartition) {
  const Bytes img = buildImage(defaultSpec());
  EXPECT_EQ(validate(img, img.size() - 1), Result::TOO_LARGE);
  EXPECT_EQ(validate(img, img.size()), Result::OK);
  EXPECT_EQ(validate(img, 0), Result::OK);  // 0 skips the partition check
}

TEST_F(FirmwareFlashTest, ValidateRejectsBadMagic) {
  ImageSpec spec = defaultSpec();
  spec.magic = 0xE8;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_MAGIC);
}

TEST_F(FirmwareFlashTest, ValidateBadMagicWinsOverBadChip) {
  ImageSpec spec = defaultSpec();
  spec.magic = 0xFF;
  spec.chipId = CHIP_ESP32S3;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_MAGIC);
}

TEST_F(FirmwareFlashTest, ValidateRejectsForeignChipId) {
  ImageSpec spec = defaultSpec();
  spec.chipId = CHIP_ESP32S3;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_CHIP);
  spec.chipId = 0xFFFF;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_CHIP);
}

TEST_F(FirmwareFlashTest, ValidateRejectsSegmentHeaderPastEof) {
  // Body length chosen so the segment ends one byte short of a 16-byte
  // boundary: exactly one padding byte follows it, so a second segment header
  // cannot fit before EOF.
  ImageSpec spec = defaultSpec(BODY_65536, /*hashAppended=*/false);
  const Bytes probe = buildImage(spec);
  ASSERT_EQ(probe.size(), 65536u);
  ASSERT_EQ(probe.size() - (SEG0_DATA + spec.segments[0].size()), 1u);
  spec.segCountOverride = 2;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_SEGMENTS);
}

TEST_F(FirmwareFlashTest, ValidateRejectsSegmentCountBeyondFile) {
  ImageSpec spec = defaultSpec();
  spec.segCountOverride = 255;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_SEGMENTS);
}

// `data_len` comes straight off the SD card, so the bounds check is the only
// thing between a hostile value and a read past EOF. The values here stay below
// 2^31 so `pos + dataLen` cannot wrap in the device's 32-bit size_t and the
// expectation holds on both host and target.
TEST_F(FirmwareFlashTest, ValidateRejectsSegmentLengthPastEof) {
  const Bytes img = buildImage(defaultSpec());
  for (uint32_t lying : {static_cast<uint32_t>(img.size()), 0x0FFFFFFFu, 0x7FFFFFF0u}) {
    Bytes bad = img;
    std::memcpy(bad.data() + HEADER_SIZE + 4, &lying, sizeof(lying));
    EXPECT_EQ(validate(bad), Result::BAD_SEGMENTS) << "data_len=" << lying;
  }
}

// Off-by-one at the bounds check: one byte past EOF is BAD_SEGMENTS, while
// exactly-at-EOF gets through to the padding check (ValidateRejectsImageWithPaddingStripped).
TEST_F(FirmwareFlashTest, ValidateRejectsSegmentLengthOneBytePastEof) {
  Bytes img = buildImage(defaultSpec(66000, /*hashAppended=*/false));
  const uint32_t lying = static_cast<uint32_t>(img.size() - SEG0_DATA + 1);
  std::memcpy(img.data() + HEADER_SIZE + 4, &lying, sizeof(lying));
  EXPECT_EQ(validate(img), Result::BAD_SEGMENTS);
}

// The bounds check lives inside the segment loop, so a lying length on a later
// segment must be caught just as the first one is.
TEST_F(FirmwareFlashTest, ValidateRejectsLyingLengthOnLaterSegment) {
  ImageSpec spec;
  spec.segments = {patternBytes(40000, 1), patternBytes(30000, 2)};
  Bytes img = buildImage(spec);
  const uint32_t lying = 0x0FFFFFFFu;
  std::memcpy(img.data() + HEADER_SIZE + SEG_HEADER_SIZE + 40000 + 4, &lying, sizeof(lying));
  EXPECT_EQ(validate(img), Result::BAD_SEGMENTS);
}

// A `data_len` big enough to wrap `pos + dataLen` in the device's 32-bit size_t
// slips past the bounds check there and is stopped one layer down by the
// bounded chunk reader; on the 64-bit host the bounds check catches it. Pin
// "rejected" rather than a host-only result code (see FirmwareFlasher.cpp:193).
TEST_F(FirmwareFlashTest, ValidateRejectsSegmentLengthThatWrapsThirtyTwoBits) {
  Bytes img = buildImage(defaultSpec());
  const uint32_t lying = 0xFFFFFFF0u;
  std::memcpy(img.data() + HEADER_SIZE + 4, &lying, sizeof(lying));
  const Result r = validate(img);
  EXPECT_TRUE(r == Result::BAD_SEGMENTS || r == Result::READ_FAIL) << firmware_flash::resultName(r);
}

TEST_F(FirmwareFlashTest, ValidateRejectsTruncatedSegment) {
  Bytes img = buildImage(defaultSpec(70000));
  img.resize(66000);  // still >= 64 KiB, but segment data runs past EOF
  EXPECT_EQ(validate(img), Result::BAD_SEGMENTS);
}

TEST_F(FirmwareFlashTest, ValidateRejectsZeroSegmentsOnLargeFile) {
  ImageSpec spec = defaultSpec();
  spec.segCountOverride = 0;
  EXPECT_EQ(validate(buildImage(spec)), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateRejectsTrailingGarbage) {
  Bytes img = buildImage(defaultSpec());
  img.insert(img.end(), 16, 0xAB);
  EXPECT_EQ(validate(img), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateRejectsHashFlagWithoutTrailer) {
  Bytes img = buildImage(defaultSpec(66000, /*hashAppended=*/false));
  img[23] = 1;
  EXPECT_EQ(validate(img), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateRejectsTrailerWithoutHashFlag) {
  Bytes img = buildImage(defaultSpec(66000, /*hashAppended=*/true));
  img[23] = 0;
  EXPECT_EQ(validate(img), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateRejectsCorruptChecksumByte) {
  Bytes img = buildImage(defaultSpec(66000, /*hashAppended=*/false));
  img.back() ^= 0x01;
  EXPECT_EQ(validate(img), Result::BAD_CHECKSUM);
}

TEST_F(FirmwareFlashTest, ValidateRejectsCorruptSegmentByteAsChecksumBeforeSha) {
  Bytes img = buildImage(defaultSpec());
  img[SEG0_DATA + 12345] ^= 0x80;
  EXPECT_EQ(validate(img), Result::BAD_CHECKSUM);
}

// Transfer cut short inside the SHA trailer: the size arithmetic catches it
// before any hash is compared.
TEST_F(FirmwareFlashTest, ValidateRejectsPartialShaTrailer) {
  Bytes img = buildImage(defaultSpec());
  img.resize(img.size() - 10);
  EXPECT_EQ(validate(img), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateRejectsCorruptShaTrailer) {
  Bytes img = buildImage(defaultSpec());
  img[img.size() - 5] ^= 0x01;
  EXPECT_EQ(validate(img), Result::BAD_SHA);
}

TEST_F(FirmwareFlashTest, ValidateShaCoversHeaderBytesOutsideChecksum) {
  Bytes img = buildImage(defaultSpec());
  img[4] ^= 0x01;  // entry address: not in the XOR checksum, but hashed
  EXPECT_EQ(validate(img), Result::BAD_SHA);
}

TEST_F(FirmwareFlashTest, ValidateAcceptsUnhashedImage) {
  EXPECT_EQ(validate(buildImage(defaultSpec(66000, /*hashAppended=*/false))), Result::OK);
}

TEST_F(FirmwareFlashTest, ValidateAcceptsMultiSegmentImage) {
  ImageSpec spec;
  spec.segments = {patternBytes(30000, 1), patternBytes(20003, 2), patternBytes(17, 3), patternBytes(20000, 4)};
  EXPECT_EQ(validate(buildImage(spec)), Result::OK);
  spec.hashAppended = false;
  EXPECT_EQ(validate(buildImage(spec)), Result::OK);
}

TEST_F(FirmwareFlashTest, ValidateRejectsForeignBoardTag) {
  ImageSpec spec = defaultSpec();
  const char tag[] = "CROSSPOINT-BOARD-V1:sticky;";
  std::memcpy(spec.segments[0].data() + 1000, tag, sizeof(tag) - 1);
  EXPECT_EQ(validate(buildImage(spec)), Result::WRONG_BOARD);
}

TEST_F(FirmwareFlashTest, ValidateAcceptsOwnBoardTagAndUntaggedImage) {
  ImageSpec spec = defaultSpec();
  EXPECT_EQ(validate(buildImage(spec)), Result::OK);  // untagged
  const char tag[] = "CROSSPOINT-BOARD-V1:x4;";
  std::memcpy(spec.segments[0].data() + 1000, tag, sizeof(tag) - 1);
  EXPECT_EQ(validate(buildImage(spec)), Result::OK);
}

TEST_F(FirmwareFlashTest, ValidateDetectsTagSplitAcrossReadChunks) {
  ImageSpec spec = defaultSpec();
  const char tag[] = "CROSSPOINT-BOARD-V1:x4pro;";
  // Segment data is streamed in 4096-byte chunks from its own start; straddle
  // the first boundary so the magic and the name arrive in different reads.
  std::memcpy(spec.segments[0].data() + 4096 - 10, tag, sizeof(tag) - 1);
  EXPECT_EQ(validate(buildImage(spec)), Result::WRONG_BOARD);
}

TEST_F(FirmwareFlashTest, ValidateBoardTagScannerSpansSegments) {
  ImageSpec spec;
  spec.segments = {patternBytes(40000, 1), patternBytes(30000, 2)};
  const char tag[] = "CROSSPOINT-BOARD-V1:papermono;";
  std::memcpy(spec.segments[1].data() + 5, tag, sizeof(tag) - 1);
  EXPECT_EQ(validate(buildImage(spec)), Result::WRONG_BOARD);
}

TEST_F(FirmwareFlashTest, ValidateBoardTagWinsOverBadSize) {
  ImageSpec spec = defaultSpec();
  const char tag[] = "CROSSPOINT-BOARD-V1:sticky;";
  std::memcpy(spec.segments[0].data() + 1000, tag, sizeof(tag) - 1);
  Bytes img = buildImage(spec);
  img.insert(img.end(), 16, 0);
  EXPECT_EQ(validate(img), Result::WRONG_BOARD);
}

TEST_F(FirmwareFlashTest, ValidateAcceptsZeroLengthSegment) {
  ImageSpec spec;
  spec.segments = {patternBytes(66000, 1), Bytes{}, patternBytes(64, 2)};
  EXPECT_EQ(validate(buildImage(spec)), Result::OK);
}

TEST_F(FirmwareFlashTest, ValidateRejectsImageWithPaddingStripped) {
  Bytes img = buildImage(defaultSpec(66000, /*hashAppended=*/false));
  img.resize(SEG0_DATA + 66000);  // segment data ends exactly at EOF, no pad byte
  EXPECT_EQ(validate(img), Result::BAD_SIZE);
}

TEST_F(FirmwareFlashTest, ValidateReportsReadFailure) {
  const Bytes img = buildImage(defaultSpec());
  const std::string path = writeImage("img.bin", img);
  halstub::readBudget = 30000;
  EXPECT_EQ(firmware_flash::validateImageFile(path.c_str(), 0), Result::READ_FAIL);
}

// A file long enough to pass the size floor whose bytes stop arriving models a
// failing SD card; every read site in the walk must surface READ_FAIL.
TEST_F(FirmwareFlashTest, ValidateReportsReadFailureAtEveryReadSite) {
  const std::string path = writeImage("img.bin", buildImage(defaultSpec(66000)));
  const size_t afterHeader = HEADER_SIZE;
  const size_t afterSegHeader = SEG0_DATA;
  const size_t afterSegData = SEG0_DATA + 66000;
  const size_t afterPad = afterSegData + 16;
  for (size_t budget : {afterHeader - 1, afterHeader, afterSegHeader, afterSegData, afterPad}) {
    halstub::readBudget = budget;
    EXPECT_EQ(firmware_flash::validateImageFile(path.c_str(), 0), Result::READ_FAIL) << "budget=" << budget;
  }
  halstub::readBudget = SIZE_MAX;
  EXPECT_EQ(firmware_flash::validateImageFile(path.c_str(), 0), Result::OK);
}

// ---------------------------------------------------------------------------
// flashFromSdPath: cadence and failure propagation.
// ---------------------------------------------------------------------------

namespace {

struct Progress {
  std::vector<std::pair<size_t, size_t>> calls;
  static void cb(size_t written, size_t total, void* ctx) {
    static_cast<Progress*>(ctx)->calls.emplace_back(written, total);
  }
};

// Unhashed image of exactly 69648 bytes: 16 full 4 KiB chunks past the first
// 64 KiB block, then a 16-byte tail chunk.
Bytes image69648() {
  const Bytes img = buildImage(defaultSpec(69600, /*hashAppended=*/false));
  EXPECT_EQ(img.size(), 69648u);
  return img;
}

// Unhashed image of exactly one 64 KiB erase block: no tail chunk, no second erase.
Bytes image65536() {
  const Bytes img = buildImage(defaultSpec(BODY_65536, /*hashAppended=*/false));
  EXPECT_EQ(img.size(), 65536u);
  return img;
}

void expectOp(const espstub::Op& op, espstub::Op::Kind kind, size_t offset, size_t len) {
  EXPECT_EQ(op.kind, kind);
  EXPECT_EQ(op.part, espstub::nextPtr);
  EXPECT_EQ(op.offset, offset);
  EXPECT_EQ(op.len, len);
}

// The safety invariant behind the interleaved erase: the app-slot erase front
// only ever advances contiguously, and no byte is written before the block
// holding it has been erased (NOR flash cannot set bits back to 1).
void expectWritesOnlyIntoErasedFlash() {
  size_t erasedUpto = 0;
  bool sawErase = false;
  for (const espstub::Op& op : espstub::ops) {
    if (op.part != espstub::nextPtr) continue;
    if (op.kind == espstub::Op::ERASE) {
      EXPECT_EQ(op.offset, erasedUpto) << "erase front jumped";
      erasedUpto = op.offset + op.len;
      sawErase = true;
    } else {
      EXPECT_TRUE(sawErase) << "write before any erase";
      EXPECT_LE(op.offset + op.len, erasedUpto) << "write at " << op.offset << " outside the erased region";
    }
  }
}

bool otadataUntouched() {
  for (uint8_t b : espstub::otadata.data) {
    if (b != 0xFF) return false;
  }
  return true;
}

// otadata sector helpers: two 4 KiB sectors, one SelectEntry at the start of each.
ota_boot::SelectEntry makeEntry(uint32_t seq, uint32_t state, bool validCrc = true) {
  ota_boot::SelectEntry e{};
  e.ota_seq = seq;
  std::memset(e.seq_label, 0xFF, sizeof(e.seq_label));
  e.ota_state = state;
  e.crc = validCrc ? ota_boot::computeSeqCrc(seq) : ota_boot::computeSeqCrc(seq) ^ 0xDEADBEEFu;
  return e;
}

void storeSlot(int slot, const ota_boot::SelectEntry& e) {
  std::memcpy(espstub::otadata.data.data() + static_cast<size_t>(slot) * 4096, &e, sizeof(e));
}

ota_boot::SelectEntry loadSlot(int slot) {
  ota_boot::SelectEntry e{};
  std::memcpy(&e, espstub::otadata.data.data() + static_cast<size_t>(slot) * 4096, sizeof(e));
  return e;
}

bool slotIsBlank(int slot) {
  const uint8_t* p = espstub::otadata.data.data() + static_cast<size_t>(slot) * 4096;
  for (size_t i = 0; i < sizeof(ota_boot::SelectEntry); i++) {
    if (p[i] != 0xFF) return false;
  }
  return true;
}

}  // namespace

TEST_F(FirmwareFlashTest, FlashFailsWithoutNextPartition) {
  const std::string path = writeImage("img.bin", buildImage(defaultSpec()));
  espstub::nextPtr = nullptr;
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::NO_PARTITION);
  EXPECT_TRUE(espstub::ops.empty());
}

TEST_F(FirmwareFlashTest, FlashValidationFailureTouchesNoFlash) {
  ImageSpec spec = defaultSpec();
  spec.magic = 0x00;
  const std::string path = writeImage("img.bin", buildImage(spec));
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::BAD_MAGIC);
  EXPECT_TRUE(espstub::ops.empty());
  EXPECT_EQ(espstub::otadata.data[0], 0xFF);
}

TEST_F(FirmwareFlashTest, FlashRejectsImageLargerThanNextPartition) {
  espstub::next.configure("app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 65536);
  const std::string path = writeImage("img.bin", buildImage(defaultSpec()));
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::TOO_LARGE);
  EXPECT_TRUE(espstub::ops.empty());
}

TEST_F(FirmwareFlashTest, FlashWritesImageWithEraseAheadCadence) {
  const Bytes img = image69648();
  const std::string path = writeImage("img.bin", img);
  Progress progress;
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), &Progress::cb, &progress), Result::OK);

  const auto& ops = espstub::ops;
  // 2 block erases + 18 chunk writes into app1, then the otadata switch (erase + write).
  ASSERT_EQ(ops.size(), 22u);
  expectOp(ops[0], espstub::Op::ERASE, 0, 65536);
  for (size_t i = 0; i < 16; i++) expectOp(ops[1 + i], espstub::Op::WRITE, i * 4096, 4096);
  expectOp(ops[17], espstub::Op::ERASE, 65536, 65536);
  expectOp(ops[18], espstub::Op::WRITE, 65536, 4096);
  expectOp(ops[19], espstub::Op::WRITE, 69632, 16);
  EXPECT_EQ(ops[20].part, espstub::otadataPtr);
  EXPECT_EQ(ops[21].part, espstub::otadataPtr);

  // Bytes landed intact (the model corrupts writes into unerased flash).
  EXPECT_EQ(std::memcmp(espstub::next.data.data(), img.data(), img.size()), 0);
  EXPECT_EQ(espstub::next.data[img.size()], 0xFF);

  ASSERT_EQ(progress.calls.size(), 18u);
  size_t expected = 0;
  for (size_t i = 0; i < 18; i++) {
    expected += (i < 17) ? 4096 : 16;
    EXPECT_EQ(progress.calls[i].first, expected);
    EXPECT_EQ(progress.calls[i].second, img.size());
  }
  EXPECT_EQ(platform_host::yieldCount(), 18u);
  expectWritesOnlyIntoErasedFlash();
}

TEST_F(FirmwareFlashTest, FlashBlockAlignedImageNeedsOneErase) {
  const Bytes img = image65536();
  const std::string path = writeImage("img.bin", img);
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);

  // 1 block erase + 16 chunk writes, then the otadata switch (erase + write).
  ASSERT_EQ(espstub::ops.size(), 19u);
  expectOp(espstub::ops[0], espstub::Op::ERASE, 0, 65536);
  for (size_t i = 0; i < 16; i++) expectOp(espstub::ops[1 + i], espstub::Op::WRITE, i * 4096, 4096);
  EXPECT_EQ(espstub::ops[17].part, espstub::otadataPtr);
  EXPECT_EQ(std::memcmp(espstub::next.data.data(), img.data(), img.size()), 0);
  expectWritesOnlyIntoErasedFlash();
}

TEST_F(FirmwareFlashTest, FlashAcceptsImageExactlyFillingPartition) {
  espstub::next.configure("app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 65536);
  const Bytes img = image65536();
  const std::string path = writeImage("img.bin", img);
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);
  expectOp(espstub::ops[0], espstub::Op::ERASE, 0, 65536);
  EXPECT_EQ(std::memcmp(espstub::next.data.data(), img.data(), img.size()), 0);
  expectWritesOnlyIntoErasedFlash();
}

// alreadyValidated skips the size check, so the partition end is the last line
// of defence: the raw writer must stop rather than run past the slot.
TEST_F(FirmwareFlashTest, FlashStopsAtPartitionEndWhenValidationWasSkipped) {
  espstub::next.configure("app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 65536);
  const std::string path = writeImage("img.bin", image69648());
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, /*alreadyValidated=*/true),
            Result::WRITE_FAIL);
  for (const espstub::Op& op : espstub::ops) {
    if (op.part == espstub::nextPtr && op.kind == espstub::Op::WRITE) {
      EXPECT_LE(op.offset, espstub::next.info.size);
    }
  }
  EXPECT_TRUE(otadataUntouched());
}

TEST_F(FirmwareFlashTest, FlashOpenFailureWhenValidationWasSkipped) {
  EXPECT_EQ(firmware_flash::flashFromSdPath((processRoot() + "/nope.bin").c_str(), nullptr, nullptr,
                                            /*alreadyValidated=*/true),
            Result::OPEN_FAIL);
  EXPECT_TRUE(espstub::ops.empty());
  EXPECT_TRUE(otadataUntouched());
}

TEST_F(FirmwareFlashTest, FlashSelectsNextSlotInOtadata) {
  const std::string path = writeImage("img.bin", image69648());
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);
  // Blank otadata, destination ota_1: first sequence with (seq-1)%2 == 1 is 2, written to slot 0.
  ota_boot::SelectEntry entry{};
  std::memcpy(&entry, espstub::otadata.data.data(), sizeof(entry));
  EXPECT_EQ(entry.ota_seq, 2u);
  EXPECT_EQ(entry.ota_state, ota_boot::kOtaImgNew);
  EXPECT_EQ(entry.crc, ota_boot::computeSeqCrc(2));
}

TEST_F(FirmwareFlashTest, FlashClampsFinalEraseToPartitionEnd) {
  espstub::next.configure("app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 73728);
  const std::string path = writeImage("img.bin", image69648());
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);
  expectOp(espstub::ops[0], espstub::Op::ERASE, 0, 65536);
  expectOp(espstub::ops[17], espstub::Op::ERASE, 65536, 8192);
}

TEST_F(FirmwareFlashTest, FlashEraseFailureStopsBeforeOtadata) {
  const std::string path = writeImage("img.bin", image69648());
  espstub::failEraseAt = 2;
  Progress progress;
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), &Progress::cb, &progress), Result::ERASE_FAIL);
  EXPECT_EQ(espstub::writeCalls, 16u);
  EXPECT_EQ(progress.calls.size(), 16u);
  EXPECT_EQ(espstub::otadata.data[0], 0xFF);
}

TEST_F(FirmwareFlashTest, FlashWriteFailureStopsImmediately) {
  const std::string path = writeImage("img.bin", image69648());
  espstub::failWriteAt = 3;
  Progress progress;
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), &Progress::cb, &progress), Result::WRITE_FAIL);
  EXPECT_EQ(espstub::writeCalls, 3u);
  EXPECT_EQ(progress.calls.size(), 2u);
  EXPECT_EQ(espstub::otadata.data[0], 0xFF);
}

TEST_F(FirmwareFlashTest, FlashReadFailureMidStream) {
  const std::string path = writeImage("img.bin", image69648());
  halstub::readBudget = 10000;
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, /*alreadyValidated=*/true),
            Result::READ_FAIL);
  EXPECT_EQ(espstub::writeCalls, 2u);
}

TEST_F(FirmwareFlashTest, FlashReportsOtadataFailureAfterFullWrite) {
  const std::string path = writeImage("img.bin", image69648());
  espstub::otadataPtr = nullptr;
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OTADATA_FAIL);
  EXPECT_EQ(espstub::writeCalls, 18u);
}

TEST_F(FirmwareFlashTest, FlashReportsOtadataWriteFailure) {
  const std::string path = writeImage("img.bin", image69648());
  espstub::failWriteAt = 19;  // 18 image chunks, then the otadata entry
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OTADATA_FAIL);
  EXPECT_EQ(espstub::writeCalls, 19u);
  EXPECT_TRUE(otadataUntouched());
}

// Aborting mid-flash must never re-point the bootloader: the pre-existing
// otadata entry (selecting the slot the device is running from) stays byte-for-
// byte intact, so a failed update is a no-op rather than a brick.
TEST_F(FirmwareFlashTest, FlashAbortKeepsExistingBootSelection) {
  const std::string path = writeImage("img.bin", image69648());
  storeSlot(0, makeEntry(1, ota_boot::kOtaImgNew));  // bootloader currently selects ota_0
  const Bytes before(espstub::otadata.data.begin(), espstub::otadata.data.end());

  for (int mode = 0; mode < 3; mode++) {
    espstub::resetDefaults();
    std::memcpy(espstub::otadata.data.data(), before.data(), before.size());
    espstub::running.data[12] = static_cast<uint8_t>(CHIP_ESP32C3 & 0xFF);
    espstub::running.data[13] = static_cast<uint8_t>(CHIP_ESP32C3 >> 8);
    halstub::readBudget = SIZE_MAX;

    Result expected = Result::ERASE_FAIL;
    if (mode == 0) {
      espstub::failEraseAt = 1;
    } else if (mode == 1) {
      espstub::failWriteAt = 5;
      expected = Result::WRITE_FAIL;
    } else {
      halstub::readBudget = 40000;
      expected = Result::READ_FAIL;
    }
    EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, /*alreadyValidated=*/true), expected)
        << "mode=" << mode;
    EXPECT_EQ(std::memcmp(espstub::otadata.data.data(), before.data(), before.size()), 0) << "mode=" << mode;
  }
  halstub::readBudget = SIZE_MAX;
}

// The stored seq must keep advancing across updates so the bootloader always
// prefers the freshly written slot.
TEST_F(FirmwareFlashTest, FlashTwiceAdvancesOtadataSequenceIntoTheOtherSlot) {
  const std::string path = writeImage("img.bin", image65536());
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);
  ASSERT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr), Result::OK);
  EXPECT_EQ(loadSlot(1).ota_seq, 4u);
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);
}

TEST_F(FirmwareFlashTest, FlashAlreadyValidatedSkipsIntegrityPass) {
  Bytes img = image69648();
  img.back() ^= 0x01;  // corrupt checksum byte
  const std::string path = writeImage("img.bin", img);
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, /*alreadyValidated=*/false),
            Result::BAD_CHECKSUM);
  EXPECT_TRUE(espstub::ops.empty());
  EXPECT_EQ(firmware_flash::flashFromSdPath(path.c_str(), nullptr, nullptr, /*alreadyValidated=*/true), Result::OK);
  EXPECT_EQ(espstub::writeCalls, 18u + 1u);
}

// ---------------------------------------------------------------------------
// ota_boot::switchTo
// ---------------------------------------------------------------------------

TEST_F(FirmwareFlashTest, ComputeSeqCrcIsStableAndDistinct) {
  EXPECT_EQ(ota_boot::computeSeqCrc(1), ota_boot::computeSeqCrc(1));
  EXPECT_NE(ota_boot::computeSeqCrc(1), ota_boot::computeSeqCrc(2));
  uint32_t seq = 7;
  EXPECT_EQ(ota_boot::computeSeqCrc(7), esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), 4));
}

TEST_F(FirmwareFlashTest, SwitchRejectsNullDest) {
  EXPECT_FALSE(ota_boot::switchTo(nullptr));
  EXPECT_TRUE(espstub::ops.empty());
}

TEST_F(FirmwareFlashTest, SwitchFailsWithoutOtadata) {
  espstub::otadataPtr = nullptr;
  EXPECT_FALSE(ota_boot::switchTo(espstub::nextPtr));
}

TEST_F(FirmwareFlashTest, SwitchFailsOnUndersizedOtadata) {
  espstub::otadata.configure("otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xE000, 4096);
  EXPECT_FALSE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_TRUE(espstub::ops.empty());
}

TEST_F(FirmwareFlashTest, SwitchFailsWhenOtadataUnreadable) {
  espstub::failReads = true;
  EXPECT_FALSE(ota_boot::switchTo(espstub::nextPtr));
}

TEST_F(FirmwareFlashTest, SwitchOnBlankOtadataWritesSeqForOta0) {
  ASSERT_TRUE(ota_boot::switchTo(espstub::runningPtr));  // ota_0
  ASSERT_EQ(espstub::ops.size(), 2u);
  EXPECT_EQ(espstub::ops[0].kind, espstub::Op::ERASE);
  EXPECT_EQ(espstub::ops[0].offset, 0u);
  EXPECT_EQ(espstub::ops[0].len, 4096u);
  EXPECT_EQ(espstub::ops[1].kind, espstub::Op::WRITE);
  EXPECT_EQ(espstub::ops[1].offset, 0u);
  EXPECT_EQ(espstub::ops[1].len, sizeof(ota_boot::SelectEntry));

  const ota_boot::SelectEntry e = loadSlot(0);
  EXPECT_EQ(e.ota_seq, 1u);
  EXPECT_EQ(e.ota_state, ota_boot::kOtaImgNew);
  EXPECT_EQ(e.crc, ota_boot::computeSeqCrc(1));
  for (uint8_t b : e.seq_label) EXPECT_EQ(b, 0xFF);
  EXPECT_TRUE(slotIsBlank(1));
}

TEST_F(FirmwareFlashTest, SwitchOnBlankOtadataWritesSeqForOta1) {
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);
  EXPECT_TRUE(slotIsBlank(1));
}

TEST_F(FirmwareFlashTest, SwitchFromSlot0WritesInactiveSlot1) {
  storeSlot(0, makeEntry(1, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_EQ(espstub::ops[0].offset, 4096u);
  EXPECT_EQ(espstub::ops[1].offset, 4096u);
  EXPECT_EQ(loadSlot(1).ota_seq, 2u);
  EXPECT_EQ(loadSlot(0).ota_seq, 1u);  // untouched
}

TEST_F(FirmwareFlashTest, SwitchToSameSlotSkipsOneSequenceForParity) {
  storeSlot(0, makeEntry(1, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::runningPtr));  // ota_0 again
  EXPECT_EQ(loadSlot(1).ota_seq, 3u);
}

TEST_F(FirmwareFlashTest, SwitchPicksHighestValidSequence) {
  storeSlot(0, makeEntry(3, ota_boot::kOtaImgNew));
  storeSlot(1, makeEntry(4, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::runningPtr));  // ota_0 -> seq 5, into slot 0
  EXPECT_EQ(espstub::ops[0].offset, 0u);
  EXPECT_EQ(loadSlot(0).ota_seq, 5u);
  EXPECT_EQ(loadSlot(1).ota_seq, 4u);
}

TEST_F(FirmwareFlashTest, SwitchIgnoresSlotWithBadCrc) {
  storeSlot(0, makeEntry(9, ota_boot::kOtaImgNew, /*validCrc=*/false));
  ASSERT_TRUE(ota_boot::switchTo(espstub::runningPtr));
  // No valid slot: sequence restarts at 1 and slot 0 is the write target.
  EXPECT_EQ(espstub::ops[0].offset, 0u);
  EXPECT_EQ(loadSlot(0).ota_seq, 1u);
}

TEST_F(FirmwareFlashTest, SwitchIgnoresInvalidAndAbortedSlots) {
  storeSlot(0, makeEntry(5, ota_boot::kOtaImgInvalid));
  storeSlot(1, makeEntry(4, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));  // active is slot 1 (seq 4) -> seq 6 into slot 0
  EXPECT_EQ(loadSlot(0).ota_seq, 6u);

  espstub::resetDefaults();
  storeSlot(0, makeEntry(5, ota_boot::kOtaImgAborted));
  storeSlot(1, makeEntry(2, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_EQ(loadSlot(0).ota_seq, 4u);
}

// Both images rolled back (the bootloader marked each one INVALID/ABORTED):
// no slot is eligible, so the sequence restarts from scratch.
TEST_F(FirmwareFlashTest, SwitchAfterBothSlotsRolledBackRestartsSequence) {
  storeSlot(0, makeEntry(5, ota_boot::kOtaImgInvalid));
  storeSlot(1, makeEntry(6, ota_boot::kOtaImgAborted));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_EQ(espstub::ops[0].offset, 0u);
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);
  EXPECT_EQ(loadSlot(0).crc, ota_boot::computeSeqCrc(2));
}

// A rolled-back newer slot must not out-rank the older working one: the next
// sequence counts on from the surviving entry, not from the rejected image.
TEST_F(FirmwareFlashTest, SwitchCountsOnFromSurvivingSlotAfterRollback) {
  storeSlot(0, makeEntry(4, ota_boot::kOtaImgNew));
  storeSlot(1, makeEntry(9, ota_boot::kOtaImgInvalid));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));  // active = slot 0 (seq 4) -> seq 6 into slot 1
  EXPECT_EQ(loadSlot(1).ota_seq, 6u);
  EXPECT_EQ(loadSlot(0).ota_seq, 4u);
}

// An erased sector reads as 0xFFFFFFFF; it is skipped before the CRC test, so
// even a (contrived) matching CRC cannot make it the active entry.
TEST_F(FirmwareFlashTest, SwitchTreatsErasedSequenceAsAbsent) {
  storeSlot(0, makeEntry(0xFFFFFFFFu, ota_boot::kOtaImgNew));
  ASSERT_TRUE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_EQ(espstub::ops[0].offset, 0u);
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);
}

TEST_F(FirmwareFlashTest, SwitchRejectsNonOtaAppPartition) {
  espstub::Partition factory;
  factory.configure("factory", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, 0x10000, 0x100000);
  EXPECT_FALSE(ota_boot::switchTo(&factory.info));
  factory.info.subtype = static_cast<esp_partition_subtype_t>(0x20);  // OTA_MIN + 16
  EXPECT_FALSE(ota_boot::switchTo(&factory.info));
  factory.info.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_15;
  EXPECT_TRUE(ota_boot::switchTo(&factory.info));
  EXPECT_EQ(loadSlot(0).ota_seq, 2u);  // idx 15 has odd parity like ota_1
}

TEST_F(FirmwareFlashTest, SwitchFailsOnEraseOrWriteError) {
  espstub::failEraseAt = 1;
  EXPECT_FALSE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_TRUE(slotIsBlank(0));

  espstub::resetDefaults();
  espstub::failWriteAt = 1;
  EXPECT_FALSE(ota_boot::switchTo(espstub::nextPtr));
  EXPECT_TRUE(slotIsBlank(0));
}
