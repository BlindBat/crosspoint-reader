// Host tests for src/util/DictZip.cpp — the random-access dictzip (.dict.dz)
// reader that inflates arbitrary byte ranges of an untrusted, user-installed
// StarDict data file via the gzip FEXTRA 'RA' chunk table.
//
// Fixtures come from scripts/generate_test_dict.py (committed generator +
// committed outputs in test/dict_common/resources). The well-formed fixtures
// are verified byte-exact against their known plaintext; every malformed
// variant must fail gracefully with the documented ExtractError, without
// crashing, sanitizer-clean, and without unbounded allocation.

#include <Arduino.h>  // host stub: dictstub::HeapLimitScope drives the heap guards
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "src/util/DictZip.h"
#include "test/support/AllocCounter.h"

namespace {

using DictZip::ExtractError;
using DictZip::Info;

std::string resPath(const std::string& name) { return std::string(DICT_RESOURCES_DIR) + "/" + name; }

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << path;
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Parse a fixture through the production reader.
bool parseFixture(const std::string& name, Info* info, ExtractError* err) {
  HalFile file;
  if (!file.open(resPath(name).c_str(), "rb")) {
    ADD_FAILURE() << "cannot open fixture " << name;
    return false;
  }
  return DictZip::parse(file, info, err);
}

// Scratch-file name unique to this process: ctest runs the suite's tests as
// parallel processes sharing one working directory, so a fixed name races.
std::string scratchName(const char* tag) { return std::string("dictzip_") + tag + "_" + std::to_string(::getpid()); }

// Run extractEntry into a scratch file (cwd = the suite's build directory) and
// return the extracted bytes through *out.
bool extractRange(const std::string& name, uint32_t offset, uint32_t size, std::string* out,
                  ExtractError* err = nullptr) {
  const std::string scratch = scratchName("extract");
  bool ok = false;
  {
    HalFile outFile;
    EXPECT_TRUE(outFile.open(scratch.c_str(), "wb+"));
    ok = DictZip::extractEntry(resPath(name).c_str(), offset, size, outFile, err);
  }
  *out = readAll(scratch);
  std::remove(scratch.c_str());
  return ok;
}

class DictZipTest : public ::testing::Test {
 protected:
  void SetUp() override {
    smallDict = readAll(resPath("small.dict"));
    bigDict = readAll(resPath("big.dict"));
    ASSERT_FALSE(smallDict.empty());
    ASSERT_FALSE(bigDict.empty());
  }

  std::string smallDict;
  std::string bigDict;  // chunkLength 512, ~27KB across ~54 chunks
};

// ---------------------------------------------------------------------------
// parse(): happy path
// ---------------------------------------------------------------------------

TEST_F(DictZipTest, ParseGoodSmall) {
  Info info;
  ExtractError err = ExtractError::Decompress;
  ASSERT_TRUE(parseFixture("small.dict.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::None);
  EXPECT_TRUE(info.valid);
  EXPECT_EQ(info.chunkLength, 32u);
  EXPECT_EQ(info.totalSize, smallDict.size());
  // chunkOffsets carries chunkCount+1 cumulative entries starting at 0.
  const size_t chunkCount = (smallDict.size() + 31) / 32;
  ASSERT_EQ(info.chunkOffsets.size(), chunkCount + 1);
  EXPECT_EQ(info.chunkOffsets.front(), 0u);
  for (size_t i = 1; i < info.chunkOffsets.size(); i++) {
    EXPECT_GT(info.chunkOffsets[i], info.chunkOffsets[i - 1]) << "chunk " << i;
  }
  EXPECT_GT(info.dataOffset, 0u);
}

TEST_F(DictZipTest, ParseGoodBig) {
  Info info;
  ExtractError err = ExtractError::Decompress;
  ASSERT_TRUE(parseFixture("big.dict.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::None);
  EXPECT_EQ(info.chunkLength, 512u);
  EXPECT_EQ(info.totalSize, bigDict.size());
  EXPECT_EQ(info.chunkOffsets.size(), (bigDict.size() + 511) / 512 + 1);
}

TEST_F(DictZipTest, ParseFancyHeaderWithAllOptionalFields) {
  // Non-RA FEXTRA subfield before the RA one, FNAME, FCOMMENT and FHCRC all
  // present: the header walk must skip them all and still find the table.
  Info info;
  ExtractError err = ExtractError::Decompress;
  ASSERT_TRUE(parseFixture("fancy.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::None);
  EXPECT_EQ(info.totalSize, smallDict.size());

  std::string out;
  ASSERT_TRUE(extractRange("fancy.dz", 0, static_cast<uint32_t>(smallDict.size()), &out));
  EXPECT_EQ(out, smallDict);
}

TEST_F(DictZipTest, ParseNullInfoFailsGracefully) {
  HalFile file;
  ASSERT_TRUE(file.open(resPath("small.dict.dz").c_str(), "rb"));
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(DictZip::parse(file, nullptr, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

// ---------------------------------------------------------------------------
// extractEntry(): byte-exact random access against known plaintext
// ---------------------------------------------------------------------------

TEST_F(DictZipTest, ExtractWithinSingleChunk) {
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", 100, 200, &out));
  EXPECT_EQ(out, bigDict.substr(100, 200));
}

TEST_F(DictZipTest, ExtractSpanningTwoChunks) {
  // chunkLength 512: [500, 600) crosses the chunk 0 / chunk 1 boundary.
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", 500, 100, &out));
  EXPECT_EQ(out, bigDict.substr(500, 100));
}

TEST_F(DictZipTest, ExtractSpanningManyChunks) {
  // ~6KB starting mid-chunk: crosses about a dozen chunk boundaries.
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", 300, 6000, &out));
  EXPECT_EQ(out, bigDict.substr(300, 6000));
}

TEST_F(DictZipTest, ExtractSingleBytesAroundChunkBoundary) {
  for (uint32_t offset : {511u, 512u, 513u}) {
    std::string out;
    ASSERT_TRUE(extractRange("big.dict.dz", offset, 1, &out)) << offset;
    EXPECT_EQ(out, bigDict.substr(offset, 1)) << offset;
  }
}

TEST_F(DictZipTest, ExtractWholeFile) {
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", 0, static_cast<uint32_t>(bigDict.size()), &out));
  EXPECT_EQ(out, bigDict);
}

TEST_F(DictZipTest, ExtractTailOfLastChunk) {
  const uint32_t size = 37;
  const uint32_t offset = static_cast<uint32_t>(bigDict.size()) - size;
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", offset, size, &out));
  EXPECT_EQ(out, bigDict.substr(offset, size));
}

TEST_F(DictZipTest, ExtractChunkAlignedRange) {
  std::string out;
  ASSERT_TRUE(extractRange("big.dict.dz", 1024, 512, &out));
  EXPECT_EQ(out, bigDict.substr(1024, 512));
}

TEST_F(DictZipTest, ExtractAlignedPayloadWholeAndLastChunk) {
  // aligned.dz: totalSize is an exact multiple of chunkLength (128 = 4 * 32),
  // exercising the last-chunk size math without a short tail chunk.
  const std::string aligned = readAll(resPath("aligned.bin"));
  ASSERT_EQ(aligned.size(), 128u);
  std::string out;
  ASSERT_TRUE(extractRange("aligned.dz", 0, 128, &out));
  EXPECT_EQ(out, aligned);
  ASSERT_TRUE(extractRange("aligned.dz", 96, 32, &out));
  EXPECT_EQ(out, aligned.substr(96, 32));
}

TEST_F(DictZipTest, ExtractZeroSizeSucceedsWithoutTouchingFile) {
  // size == 0 short-circuits before the file is even opened: a nonexistent
  // path still "succeeds". Pins the documented early-out.
  const std::string scratch = scratchName("zero");
  HalFile outFile;
  ASSERT_TRUE(outFile.open(scratch.c_str(), "wb+"));
  ExtractError err = ExtractError::Decompress;
  EXPECT_TRUE(DictZip::extractEntry("/no/such/file.dz", 123, 0, outFile, &err));
  EXPECT_EQ(err, ExtractError::None);
  outFile.close();
  EXPECT_EQ(readAll(scratch), "");
  std::remove(scratch.c_str());
}

// ---------------------------------------------------------------------------
// extractEntry(): untrusted range rejection (offset/size from a hostile .idx)
// ---------------------------------------------------------------------------

TEST_F(DictZipTest, ExtractRejectsOffsetPastEof) {
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small.dict.dz", static_cast<uint32_t>(smallDict.size()) + 1, 1, &out, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
  EXPECT_TRUE(out.empty());
}

TEST_F(DictZipTest, ExtractRejectsSizePastEof) {
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small.dict.dz", 10, static_cast<uint32_t>(smallDict.size()), &out, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, ExtractRejectsOneBytePastEnd) {
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small.dict.dz", static_cast<uint32_t>(smallDict.size()), 1, &out, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, ExtractRejectsOverflowingOffsetPlusSize) {
  // offset + size wraps uint32: the subtraction-form bounds check must still
  // reject it instead of wrapping into an "in range" value.
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small.dict.dz", 0xFFFFFFF0u, 0x20u, &out, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, ExtractMissingFileIsReadError) {
  const std::string scratch = scratchName("missing");
  HalFile outFile;
  ASSERT_TRUE(outFile.open(scratch.c_str(), "wb+"));
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(DictZip::extractEntry("/no/such/file.dz", 0, 10, outFile, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
  outFile.close();
  std::remove(scratch.c_str());
}

// ---------------------------------------------------------------------------
// Malformed .dz variants: graceful, classified failure — never a crash
// ---------------------------------------------------------------------------

TEST_F(DictZipTest, MissingRaFieldPlainGzipIsDecompress) {
  // Pin: a plain .gz without the RA chunk table is rejected as a malformed
  // dictzip (Decompress), not misread as a single-chunk file.
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_nora.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
  EXPECT_FALSE(info.valid);
}

TEST_F(DictZipTest, LyingChunkCountIsDecompress) {
  // RA declares chunkCount = actual + 1 while the subfield length only covers
  // the actual table: the count-vs-length consistency check must fire.
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_lying_count.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, HugeChunkCountRejectedWithBoundedAllocation) {
  // chunkCount 16384 > MAX_CHUNK_COUNT (8192): rejected before the chunk table
  // is reserved. The allocation counter proves no table-sized buffer was even
  // attempted (16384 offsets would be 64KB+).
  Info info;
  ExtractError err = ExtractError::None;
  bool ok = true;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    ok = parseFixture("small_huge_count.dz", &info, &err);
    bytes = scope.bytes();
  }
  EXPECT_FALSE(ok);
  EXPECT_EQ(err, ExtractError::Decompress);
  EXPECT_LT(bytes, 8u * 1024u);
}

TEST_F(DictZipTest, MaxSizedChunkTableRefusedOnLowHeap) {
  // The pre-reserve guard: with the (stubbed) largest free block smaller than
  // the chunk table + headroom, parse must refuse with LowMemory instead of
  // letting vector::reserve() abort. small.dict.dz's table is 13 offsets
  // (52 bytes), so an 8-byte "heap" is below 52 + 1024 headroom.
  dictstub::HeapLimitScope heap(8);
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small.dict.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::LowMemory);
}

TEST_F(DictZipTest, TruncatedChunkTableIsReadError) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_trunc_table.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, ZeroIsizeIsDecompress) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_zero_isize.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, DoubleRaSubfieldIsDecompress) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_double_ra.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, ZeroChunkLengthIsDecompress) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_zero_chlen.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, UnsupportedRaVersionIsDecompress) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("small_bad_ver.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, NotAGzipFileIsDecompress) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("not_gzip.bin", &info, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, EmptyFileIsReadError) {
  Info info;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(parseFixture("empty.dz", &info, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, UnderstatedChunkLengthIsDecompress) {
  // Chunk 0's table entry claims 4 compressed bytes; inflating chunk 0 runs
  // out of input mid-stream. The input-exhausted path is a corrupt-stream
  // verdict (Decompress), not an IO error.
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small_short_len.dz", 0, 32, &out, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, MisalignedNextChunkAfterShortLengthFailsGracefully) {
  // Chunk 1's compressed data now nominally starts 4 bytes into chunk 0's
  // stream — mid-stream garbage. Pin the current classification: the bogus
  // bytes decode as an incomplete/invalid deflate stream -> Decompress.
  std::string out;
  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small_short_len.dz", 32, 32, &out, &err));
  EXPECT_EQ(err, ExtractError::Decompress);
}

TEST_F(DictZipTest, OverstatedChunkLengthsPastEof) {
  // Every table entry claims 0xFFFF compressed bytes. Chunk 0 still starts at
  // the true data offset, and the inflater stops as soon as the requested
  // output is produced — so a chunk-0 read still succeeds byte-exact (the lie
  // only pads the readable window). Chunk 1's computed offset lands far past
  // EOF, where the read callback fails -> ReadError. Pins both behaviors.
  std::string out;
  ASSERT_TRUE(extractRange("small_len_overflow.dz", 0, 32, &out));
  EXPECT_EQ(out, smallDict.substr(0, 32));

  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small_len_overflow.dz", 32, 32, &out, &err));
  EXPECT_EQ(err, ExtractError::ReadError);
}

TEST_F(DictZipTest, GarbageDeflateInOneChunkOnlyBreaksThatChunk) {
  // Chunk 3 is same-length garbage (reserved deflate block type). Ranges in
  // chunks 0-2 and 4+ still extract byte-exact — each chunk is an independent
  // full-flush segment — while any range touching chunk 3 is Decompress.
  std::string out;
  ASSERT_TRUE(extractRange("small_garbage_chunk.dz", 0, 96, &out));  // chunks 0..2
  EXPECT_EQ(out, smallDict.substr(0, 96));

  ASSERT_TRUE(extractRange("small_garbage_chunk.dz", 128, 64, &out));  // chunks 4..5
  EXPECT_EQ(out, smallDict.substr(128, 64));

  ExtractError err = ExtractError::None;
  EXPECT_FALSE(extractRange("small_garbage_chunk.dz", 96, 32, &out, &err));  // chunk 3
  EXPECT_EQ(err, ExtractError::Decompress);

  err = ExtractError::None;
  EXPECT_FALSE(extractRange("small_garbage_chunk.dz", 90, 50, &out, &err));  // spans into chunk 3
  EXPECT_EQ(err, ExtractError::Decompress);
}

// ---------------------------------------------------------------------------
// Allocation discipline
// ---------------------------------------------------------------------------

TEST_F(DictZipTest, SingleChunkExtractionAllocationIsBounded) {
  // One chunk slice needs the 32KB inflate ring + ~2KB chunk source + 512B
  // copy buffer + the chunk table. Anything near six figures would mean the
  // reader started trusting attacker-sized values.
  std::string out;
  bool ok = false;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    ok = extractRange("small.dict.dz", 0, 32, &out);
    bytes = scope.bytes();
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(out, smallDict.substr(0, 32));
  EXPECT_LT(bytes, 64u * 1024u);
}

TEST_F(DictZipTest, MalformedVariantsAllocationIsBounded) {
  // Every malformed fixture, parsed and (where parseable) extracted: total
  // requested heap must stay near the 32KB ring + small change, proving no
  // attacker-declared size reaches an allocator.
  const char* variants[] = {
      "small_nora.dz",
      "small_lying_count.dz",
      "small_short_len.dz",
      "small_len_overflow.dz",
      "small_garbage_chunk.dz",
      "small_huge_count.dz",
      "small_trunc_table.dz",
      "small_zero_isize.dz",
      "small_double_ra.dz",
      "small_zero_chlen.dz",
      "small_bad_ver.dz",
      "not_gzip.bin",
      "empty.dz",
  };
  for (const char* name : variants) {
    std::string out;
    size_t bytes = 0;
    {
      alloc_counter::CountingScope scope;
      ExtractError err = ExtractError::None;
      (void)extractRange(name, 0, 64, &out, &err);
      bytes = scope.bytes();
    }
    EXPECT_LT(bytes, 128u * 1024u) << name;
  }
}

}  // namespace
