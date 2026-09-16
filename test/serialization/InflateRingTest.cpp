// InflateReader::initWithRing (lib/InflateReader/InflateReader.cpp).
//
// initWithRing exists so a caller can allocate the 32KB ring FIRST, when the
// heap still has a run that large, and hand it to the reader. The reader must
// then use that buffer for back-references, zero it before use and never free
// it -- the tests lean on ASan to catch the ownership half.
//
// Compressed fixtures are produced in-test with miniz's deflate (the firmware
// never compresses); the decompressor under test is the vendored uzlib.

#include <InflateReader.h>
#include <gtest/gtest.h>
#include <miniz.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// Repetitive but not uniform: long-range matches reach back into the ring
// after the producing bytes have long left the caller's output window.
std::vector<uint8_t> buildCorpus() {
  static constexpr const char* kLines[] = {
      "the quick brown fox jumps over the lazy dog ",
      "it was the best of times, it was the worst of times ",
      "call me ishmael. some years ago -- never mind how long precisely ",
      "in the beginning the universe was created, which made a lot of people angry ",
  };
  std::string text;
  text.reserve(220000);
  for (int i = 0; i < 2600; ++i) {
    text += kLines[i % 4];
    text += std::to_string(i);
    text += '\n';
  }
  return std::vector<uint8_t>(text.begin(), text.end());
}

// Raw deflate (no zlib wrapper), which is what InflateReader consumes.
std::vector<uint8_t> deflateRaw(const std::vector<uint8_t>& src) {
  size_t outLen = 0;
  void* out = tdefl_compress_mem_to_heap(src.data(), src.size(), &outLen, 128 /* max probes */);
  EXPECT_NE(out, nullptr);
  if (out == nullptr) return {};
  std::vector<uint8_t> compressed(static_cast<uint8_t*>(out), static_cast<uint8_t*>(out) + outLen);
  mz_free(out);
  return compressed;
}

// Built once: every test reuses the same bytes.
const std::vector<uint8_t>& corpus() {
  static const std::vector<uint8_t> data = buildCorpus();
  return data;
}

const std::vector<uint8_t>& corpusDeflated() {
  static const std::vector<uint8_t> data = deflateRaw(corpus());
  return data;
}

// Streams the whole thing through readAtMost() in fixed windows, which is the
// only mode where the ring matters.
::testing::AssertionResult inflateChunked(InflateReader& reader, const size_t chunk, const size_t expectedSize,
                                          std::vector<uint8_t>& out) {
  std::vector<uint8_t> window(chunk);
  out.clear();
  out.reserve(expectedSize);
  for (size_t guard = 0; guard <= expectedSize / chunk + 8; ++guard) {
    size_t produced = 0;
    const InflateStatus status = reader.readAtMost(window.data(), chunk, &produced);
    if (status == InflateStatus::Error) return ::testing::AssertionFailure() << "readAtMost reported Error";
    out.insert(out.end(), window.begin(), window.begin() + static_cast<std::ptrdiff_t>(produced));
    if (status == InflateStatus::Done) return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "stream never reported Done";
}

TEST(InflateRingTest, RingSizeIsThirtyTwoKilobytes) { EXPECT_EQ(InflateReader::RING_BYTES, 32768u); }

TEST(InflateRingTest, InitWithRingRejectsANullBuffer) {
  InflateReader reader;
  EXPECT_FALSE(reader.initWithRing(nullptr));
}

TEST(InflateRingTest, ARejectedNullRingLeavesTheReaderReusable) {
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();
  ASSERT_FALSE(compressed.empty());

  InflateReader reader;
  ASSERT_FALSE(reader.initWithRing(nullptr));

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());
  std::vector<uint8_t> out;
  EXPECT_TRUE(inflateChunked(reader, 4096, source.size(), out));
  EXPECT_EQ(out, source);
}

TEST(InflateRingTest, InitWithRingZeroesTheCallersBuffer) {
  std::vector<uint8_t> ring(InflateReader::RING_BYTES, 0xAA);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  EXPECT_EQ(std::count(ring.begin(), ring.end(), 0), static_cast<long>(ring.size()));
}

TEST(InflateRingTest, StreamsBackReferencesAcrossManyReadCalls) {
  const std::vector<uint8_t>& source = corpus();
  ASSERT_GT(source.size(), 3u * InflateReader::RING_BYTES);  // matches must outlive the window
  const std::vector<uint8_t>& compressed = corpusDeflated();
  ASSERT_LT(compressed.size(), source.size());  // the compressor really found matches

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out;
  EXPECT_TRUE(inflateChunked(reader, 4096, source.size(), out));
  EXPECT_EQ(out, source);
}

TEST(InflateRingTest, TinyOutputWindowsStillReproduceTheStream) {
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out;
  EXPECT_TRUE(inflateChunked(reader, 61, source.size(), out));  // deliberately not a power of two
  EXPECT_EQ(out, source);
}

TEST(InflateRingTest, CallerOwnedRingMatchesTheSelfAllocatedRing) {
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();

  std::vector<uint8_t> viaOwned;
  {
    InflateReader reader;
    ASSERT_TRUE(reader.init(true));
    reader.setSource(compressed.data(), compressed.size());
    ASSERT_TRUE(inflateChunked(reader, 4096, source.size(), viaOwned));
  }

  std::vector<uint8_t> viaCaller;
  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  {
    InflateReader reader;
    ASSERT_TRUE(reader.initWithRing(ring.data()));
    reader.setSource(compressed.data(), compressed.size());
    ASSERT_TRUE(inflateChunked(reader, 4096, source.size(), viaCaller));
  }

  EXPECT_EQ(viaOwned, source);
  EXPECT_EQ(viaCaller, viaOwned);
}

TEST(InflateRingTest, DeinitDoesNotFreeTheCallersBuffer) {
  // ASan turns a wrong free() here into an alloc-dealloc-mismatch report.
  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  {
    InflateReader reader;
    ASSERT_TRUE(reader.initWithRing(ring.data()));
    reader.deinit();
    ring[0] = 0x5A;  // still ours
  }
  ring[InflateReader::RING_BYTES - 1] = 0x5B;  // and still ours after the reader dies
  EXPECT_EQ(ring[0], 0x5A);
}

TEST(InflateRingTest, DestructorDoesNotFreeTheCallersBuffer) {
  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  {
    InflateReader reader;
    ASSERT_TRUE(reader.initWithRing(ring.data()));
  }
  ring[100] = 0x11;
  EXPECT_EQ(ring[100], 0x11);
}

TEST(InflateRingTest, AdoptingACallerRingReleasesAPreviouslyOwnedOne) {
  // init(true) mallocs its own ring; initWithRing() must free exactly that one
  // and then adopt the caller's. ASan catches the wrong half of the pair (a
  // second free of the caller's ring); the leak half is only caught where
  // LeakSanitizer is available, which is not macOS.
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.init(true));
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out;
  EXPECT_TRUE(inflateChunked(reader, 4096, source.size(), out));
  EXPECT_EQ(out, source);
}

TEST(InflateRingTest, ReinitialisingWithTheSameRingRestartsTheStream) {
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  std::vector<uint8_t> first, second;

  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());
  ASSERT_TRUE(inflateChunked(reader, 4096, source.size(), first));

  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());
  ASSERT_TRUE(inflateChunked(reader, 4096, source.size(), second));

  EXPECT_EQ(first, source);
  EXPECT_EQ(second, source);
}

TEST(InflateRingTest, ReadDeliversAnExactByteCount) {
  const std::vector<uint8_t>& source = corpus();
  const std::vector<uint8_t>& compressed = corpusDeflated();

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> head(5000);
  ASSERT_TRUE(reader.read(head.data(), head.size()));
  EXPECT_TRUE(std::equal(head.begin(), head.end(), source.begin()));
}

TEST(InflateRingTest, ReadFailsWhenTheStreamIsShorterThanRequested) {
  const std::vector<uint8_t> source(64, 'a');
  const std::vector<uint8_t> compressed = deflateRaw(source);

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out(source.size() * 4);
  EXPECT_FALSE(reader.read(out.data(), out.size()));
}

TEST(InflateRingTest, ReadAtMostReportsDoneWithAPartialFinalChunk) {
  const std::vector<uint8_t> source(1000, 'z');
  const std::vector<uint8_t> compressed = deflateRaw(source);

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> window(4096);
  size_t produced = 0;
  EXPECT_EQ(reader.readAtMost(window.data(), window.size(), &produced), InflateStatus::Done);
  EXPECT_EQ(produced, source.size());
}

TEST(InflateRingTest, EmptyPayloadDecompressesToNothing) {
  // Canonical empty deflate stream: final fixed-Huffman block, end-of-block only.
  const uint8_t compressed[] = {0x03, 0x00};

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed, sizeof(compressed));

  uint8_t window[16] = {};
  size_t produced = 0xFF;
  EXPECT_EQ(reader.readAtMost(window, sizeof(window), &produced), InflateStatus::Done);
  EXPECT_EQ(produced, 0u);
}

TEST(InflateRingTest, IncompressibleDataRoundTrips) {
  std::vector<uint8_t> source(70000);
  uint32_t state = 0x12345678u;  // fixed LCG: deterministic, no back-references
  for (auto& byte : source) {
    state = state * 1664525u + 1013904223u;
    byte = static_cast<uint8_t>(state >> 24);
  }
  const std::vector<uint8_t> compressed = deflateRaw(source);

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out;
  EXPECT_TRUE(inflateChunked(reader, 4096, source.size(), out));
  EXPECT_EQ(out, source);
}

// --- malformed input ---------------------------------------------------------

TEST(InflateRingTest, ReservedBlockTypeIsRejected) {
  const uint8_t bad[] = {0x07, 0x00, 0x00, 0x00};  // BFINAL=1, BTYPE=3 (reserved)

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(bad, sizeof(bad));

  uint8_t window[32] = {};
  size_t produced = 0;
  EXPECT_EQ(reader.readAtMost(window, sizeof(window), &produced), InflateStatus::Error);
}

TEST(InflateRingTest, EmptyInputIsRejected) {
  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  const uint8_t none = 0;
  reader.setSource(&none, 0);  // valid pointer, zero length

  uint8_t window[32] = {};
  size_t produced = 0;
  EXPECT_EQ(reader.readAtMost(window, sizeof(window), &produced), InflateStatus::Error);
  EXPECT_EQ(produced, 0u);
}

TEST(InflateRingTest, TruncatedStreamIsRejected) {
  const std::vector<uint8_t>& compressed = corpusDeflated();
  ASSERT_GT(compressed.size(), 64u);
  const std::vector<uint8_t> truncated(compressed.begin(), compressed.begin() + 64);

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(truncated.data(), truncated.size());

  std::vector<uint8_t> window(4096);
  InflateStatus status = InflateStatus::Ok;
  for (int i = 0; i < 64 && status == InflateStatus::Ok; ++i) {
    size_t produced = 0;
    status = reader.readAtMost(window.data(), window.size(), &produced);
  }
  EXPECT_EQ(status, InflateStatus::Error);
}

TEST(InflateRingTest, TruncatedStoredBlockIsRejected) {
  // Stored block claiming 0x0100 bytes with only four present.
  const uint8_t stored[] = {0x01, 0x00, 0x01, 0xFF, 0xFE, 'a', 'b', 'c', 'd'};

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(stored, sizeof(stored));

  std::vector<uint8_t> window(512);
  InflateStatus status = InflateStatus::Ok;
  for (int i = 0; i < 8 && status == InflateStatus::Ok; ++i) {
    size_t produced = 0;
    status = reader.readAtMost(window.data(), window.size(), &produced);
  }
  EXPECT_EQ(status, InflateStatus::Error);
}

TEST(InflateRingTest, StoredBlockWithAMismatchedComplementIsRejected) {
  // LEN 4, NLEN 0x0000 (should be 0xFFFB).
  const uint8_t stored[] = {0x01, 0x04, 0x00, 0x00, 0x00, 'a', 'b', 'c', 'd'};

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(stored, sizeof(stored));

  uint8_t window[32] = {};
  size_t produced = 0;
  EXPECT_EQ(reader.readAtMost(window, sizeof(window), &produced), InflateStatus::Error);
}

TEST(InflateRingTest, ValidStoredBlockRoundTrips) {
  const uint8_t stored[] = {0x01, 0x04, 0x00, 0xFB, 0xFF, 'a', 'b', 'c', 'd'};

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(stored, sizeof(stored));

  uint8_t window[32] = {};
  size_t produced = 0;
  EXPECT_EQ(reader.readAtMost(window, sizeof(window), &produced), InflateStatus::Done);
  ASSERT_EQ(produced, 4u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(window), 4), "abcd");
}

TEST(InflateRingTest, CorruptedInteriorBytesAreRejected) {
  const std::vector<uint8_t>& source = corpus();
  std::vector<uint8_t> compressed = corpusDeflated();
  ASSERT_GT(compressed.size(), 400u);
  for (size_t i = 200; i < 260; ++i) compressed[i] = static_cast<uint8_t>(~compressed[i]);

  std::vector<uint8_t> ring(InflateReader::RING_BYTES);
  InflateReader reader;
  ASSERT_TRUE(reader.initWithRing(ring.data()));
  reader.setSource(compressed.data(), compressed.size());

  std::vector<uint8_t> out;
  // Either it errors out or it produces something other than the original; it
  // must never claim a clean Done on the original bytes.
  std::vector<uint8_t> window(4096);
  InflateStatus status = InflateStatus::Ok;
  for (int i = 0; i < 200 && status == InflateStatus::Ok; ++i) {
    size_t produced = 0;
    status = reader.readAtMost(window.data(), window.size(), &produced);
    out.insert(out.end(), window.begin(), window.begin() + static_cast<std::ptrdiff_t>(produced));
  }
  EXPECT_TRUE(status == InflateStatus::Error || out != source);
}

TEST(InflateRingTest, OneShotModeDecompressesWithoutARing) {
  const std::vector<uint8_t> source(4096, 'k');
  const std::vector<uint8_t> compressed = deflateRaw(source);

  InflateReader reader;
  ASSERT_TRUE(reader.init(false));
  reader.setSource(compressed.data(), compressed.size());
  std::vector<uint8_t> out(source.size());
  EXPECT_TRUE(reader.read(out.data(), out.size()));
  EXPECT_EQ(out, source);
}

}  // namespace
