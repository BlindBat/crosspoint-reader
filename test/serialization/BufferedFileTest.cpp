// BufferedFileWriter / BufferedFileReader (lib/Serialization/BufferedFile.h).
//
// The wrappers exist to collapse many small HalFile calls into chunk-sized SD
// transfers, so the tests assert both the bytes and the underlying call counts,
// plus the degraded no-buffer passthrough, the in-window seek fast path, and
// the same untrusted-length handling the unbuffered overloads have.

#include <BufferedFile.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TestFileUtil.h"
#include "test/support/AllocCounter.h"

namespace {

using testutil::TempPath;

// Small enough that every test crosses several buffer boundaries.
constexpr size_t kCap = 64;

std::vector<uint8_t> rampBytes(const size_t count) {
  std::vector<uint8_t> bytes(count);
  for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(i & 0xFF);
  return bytes;
}

class BufferedFileTest : public ::testing::Test {
 protected:
  void SetUp() override { halstub::reset(); }
  void TearDown() override { halstub::reset(); }
};

// --- writer ------------------------------------------------------------------

TEST_F(BufferedFileTest, WriterBatchesManySmallWritesIntoChunks) {
  const TempPath path("w_batch");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    halstub::reset();
    serialization::BufferedFileWriter writer(out, kCap);
    for (uint32_t i = 0; i < 100; ++i) serialization::writePod(writer, i);
    EXPECT_TRUE(writer.flush());
  }
  // 400 bytes through a 64-byte buffer: six full-buffer transfers plus the
  // final 16-byte flush, versus 100 unbuffered writes.
  EXPECT_EQ(halstub::writeCalls, 7u);

  const std::vector<uint8_t> raw = testutil::readRaw(path.c_str());
  ASSERT_EQ(raw.size(), 400u);
  for (uint32_t i = 0; i < 100; ++i) {
    uint32_t value = 0;
    std::memcpy(&value, raw.data() + i * 4, sizeof(value));
    EXPECT_EQ(value, i);
  }
}

TEST_F(BufferedFileTest, WriterFlushesOnDestruction) {
  const TempPath path("w_dtor");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::BufferedFileWriter writer(out, kCap);
    writer.write("abc", 3);  // never flushed explicitly
  }
  const std::vector<uint8_t> raw = testutil::readRaw(path.c_str());
  ASSERT_EQ(raw.size(), 3u);
  EXPECT_EQ(std::string(raw.begin(), raw.end()), "abc");
}

TEST_F(BufferedFileTest, WriterPositionTracksLogicalBytes) {
  const TempPath path("w_pos");
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
  serialization::BufferedFileWriter writer(out, kCap);
  EXPECT_EQ(writer.position(), 0u);
  writer.write("0123456789", 10);
  EXPECT_EQ(writer.position(), 10u);  // counted before any flush
  const std::vector<uint8_t> big(200, 0x7E);
  writer.write(big.data(), big.size());
  EXPECT_EQ(writer.position(), 210u);
}

TEST_F(BufferedFileTest, WriterSendsOversizeWriteStraightThroughInOrder) {
  const TempPath path("w_bypass");
  const std::vector<uint8_t> big(kCap * 3, 0xAB);
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::BufferedFileWriter writer(out, kCap);
    writer.write("head", 4);
    writer.write(big.data(), big.size());  // >= capacity: bypasses the buffer
    writer.write("tail", 4);
  }
  const std::vector<uint8_t> raw = testutil::readRaw(path.c_str());
  ASSERT_EQ(raw.size(), 8u + big.size());
  EXPECT_EQ(std::string(raw.begin(), raw.begin() + 4), "head");
  EXPECT_EQ(raw[4], 0xAB);
  EXPECT_EQ(raw[4 + big.size() - 1], 0xAB);
  EXPECT_EQ(std::string(raw.end() - 4, raw.end()), "tail");
}

TEST_F(BufferedFileTest, WriterWithZeroCapacityIsPassthrough) {
  // capacity 0 is the same cap == 0 branch the wrapper degrades to when the
  // buffer allocation fails, so it pins the OOM passthrough behaviour.
  const TempPath path("w_passthrough");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    halstub::reset();
    serialization::BufferedFileWriter writer(out, 0);
    for (int i = 0; i < 5; ++i) writer.write("ab", 2);
    EXPECT_TRUE(writer.flush());
    EXPECT_EQ(halstub::writeCalls, 5u);  // one underlying write per call
  }
  const std::vector<uint8_t> raw = testutil::readRaw(path.c_str());
  EXPECT_EQ(std::string(raw.begin(), raw.end()), "ababababab");
}

TEST_F(BufferedFileTest, WriterFlushReportsUnderlyingFailure) {
  const TempPath path("w_fail");
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
  serialization::BufferedFileWriter writer(out, kCap);
  halstub::failWrites = true;
  writer.write("data", 4);
  EXPECT_FALSE(writer.flush());
  halstub::failWrites = false;
}

TEST_F(BufferedFileTest, WriterFlushReportsFailureOfOversizeBypass) {
  const TempPath path("w_fail_bypass");
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
  serialization::BufferedFileWriter writer(out, kCap);
  const std::vector<uint8_t> big(kCap * 2, 0x01);
  halstub::failWrites = true;
  writer.write(big.data(), big.size());
  halstub::failWrites = false;
  // A later good write must not clear the sticky failure flag.
  writer.write("ok", 2);
  EXPECT_FALSE(writer.flush());
}

TEST_F(BufferedFileTest, WriterStartsFromTheFilesExistingPosition) {
  // BookMetadataCache writes a header through the raw HalFile and only then
  // wraps it, so the wrapper's logical position starts where the file is.
  const TempPath path("w_resume_pos");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    ASSERT_EQ(out.write("hdr!!", 5), 5u);

    serialization::BufferedFileWriter writer(out, kCap);
    EXPECT_EQ(writer.position(), 5u);
    writer.write("abc", 3);
    EXPECT_EQ(writer.position(), 8u);
    ASSERT_TRUE(writer.flush());
  }

  const std::vector<uint8_t> raw = testutil::readRaw(path.c_str());
  EXPECT_EQ(std::string(raw.begin(), raw.end()), "hdr!!abc");
}

TEST_F(BufferedFileTest, WriterFlushOnCleanRunReportsSuccess) {
  const TempPath path("w_ok");
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
  serialization::BufferedFileWriter writer(out, kCap);
  writer.write("data", 4);
  EXPECT_TRUE(writer.flush());
  EXPECT_TRUE(writer.flush());  // idempotent: nothing buffered the second time
}

// --- reader ------------------------------------------------------------------

TEST_F(BufferedFileTest, ReaderBatchesManySmallReadsIntoChunks) {
  const TempPath path("r_batch");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  halstub::reset();
  serialization::BufferedFileReader reader(in, kCap);
  for (int i = 0; i < 100; ++i) {
    uint8_t quad[4] = {0, 0, 0, 0};
    ASSERT_EQ(reader.read(quad, 4), 4u);
    EXPECT_EQ(quad[0], static_cast<uint8_t>(i * 4));
  }
  EXPECT_EQ(halstub::readCalls, 7u);  // ceil(400 / 64)
  EXPECT_EQ(reader.position(), 400u);
}

TEST_F(BufferedFileTest, ReaderReturnsShortCountAtEof) {
  const TempPath path("r_eof");
  testutil::writeRaw(path.c_str(), rampBytes(10));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t buffer[32] = {};
  EXPECT_EQ(reader.read(buffer, sizeof(buffer)), 10u);
  EXPECT_EQ(reader.read(buffer, sizeof(buffer)), 0u);  // nothing left
  EXPECT_EQ(reader.position(), 10u);
}

TEST_F(BufferedFileTest, ReaderOnEmptyFileReadsNothing) {
  const TempPath path("r_empty");
  testutil::writeRaw(path.c_str(), {});

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t buffer[8] = {};
  EXPECT_EQ(reader.read(buffer, sizeof(buffer)), 0u);
  EXPECT_EQ(reader.position(), 0u);
}

TEST_F(BufferedFileTest, ReaderSeekInsideWindowTouchesNeitherSeekNorRead) {
  const TempPath path("r_seek_window");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t byte = 0;
  ASSERT_EQ(reader.read(&byte, 1), 1u);  // fills the first 64-byte window
  halstub::reset();

  EXPECT_TRUE(reader.seek(40));
  EXPECT_EQ(reader.position(), 40u);
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  EXPECT_EQ(byte, 40);
  EXPECT_EQ(halstub::seekCalls, 0u);
  EXPECT_EQ(halstub::readCalls, 0u);
}

TEST_F(BufferedFileTest, ReaderSeekBackwardsInsideWindowRereadsBufferedBytes) {
  const TempPath path("r_seek_back_window");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t block[50] = {};
  ASSERT_EQ(reader.read(block, sizeof(block)), 50u);
  halstub::reset();

  EXPECT_TRUE(reader.seek(2));
  uint8_t byte = 0;
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  EXPECT_EQ(byte, 2);
  EXPECT_EQ(halstub::seekCalls, 0u);
}

TEST_F(BufferedFileTest, ReaderSeekToTheExactWindowStartStaysInTheBuffer) {
  // Lower edge of the window test: target == bufStart is inside it. Reading 70
  // bytes leaves the second 64-byte window loaded, so bufStart is 64, not 0 --
  // a non-trivial value that a `target > bufStart` regression would miss.
  const TempPath path("r_seek_window_start");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t block[70] = {};
  ASSERT_EQ(reader.read(block, sizeof(block)), 70u);
  ASSERT_EQ(reader.position(), 70u);
  halstub::reset();

  EXPECT_TRUE(reader.seek(kCap));  // == bufStart of the loaded window
  EXPECT_EQ(reader.position(), kCap);
  uint8_t byte = 0;
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  EXPECT_EQ(byte, static_cast<uint8_t>(kCap));
  EXPECT_EQ(halstub::seekCalls, 0u);
  EXPECT_EQ(halstub::readCalls, 0u);
}

TEST_F(BufferedFileTest, ReaderSeekToWindowEndFallsThroughToTheFile) {
  // The window test is half-open (target < bufStart + fill), so the byte one
  // past the buffered run is a real seek.
  const TempPath path("r_seek_edge");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t byte = 0;
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  halstub::reset();

  EXPECT_TRUE(reader.seek(kCap));
  EXPECT_EQ(halstub::seekCalls, 1u);
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  EXPECT_EQ(byte, static_cast<uint8_t>(kCap));
}

TEST_F(BufferedFileTest, ReaderSeekBackwardsOutsideWindowRewindsTheFile) {
  const TempPath path("r_seek_back");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::vector<uint8_t> block(200);
  ASSERT_EQ(reader.read(block.data(), block.size()), 200u);
  halstub::reset();

  EXPECT_TRUE(reader.seek(0));
  EXPECT_EQ(halstub::seekCalls, 1u);
  EXPECT_EQ(reader.position(), 0u);
  uint8_t quad[4] = {};
  ASSERT_EQ(reader.read(quad, 4), 4u);
  EXPECT_EQ(quad[0], 0);
  EXPECT_EQ(quad[3], 3);
}

TEST_F(BufferedFileTest, ReaderSeekPastEofSucceedsAndThenReadsNothing) {
  const TempPath path("r_seek_past_eof");
  testutil::writeRaw(path.c_str(), rampBytes(100));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  EXPECT_TRUE(reader.seek(100000));  // stdio/SdFat both allow this
  EXPECT_EQ(reader.position(), 100000u);
  uint8_t byte = 0xEE;
  EXPECT_EQ(reader.read(&byte, 1), 0u);
  EXPECT_EQ(byte, 0xEE);  // untouched
}

TEST_F(BufferedFileTest, ReaderSeekFailurePropagatesAndLeavesPositionAlone) {
  const TempPath path("r_seek_fail");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint8_t block[70] = {};
  ASSERT_EQ(reader.read(block, sizeof(block)), 70u);
  const size_t before = reader.position();

  halstub::failNextSeek = true;
  EXPECT_FALSE(reader.seek(0));  // out of window, so the failure is visible
  EXPECT_EQ(reader.position(), before);
  halstub::failNextSeek = false;
}

TEST_F(BufferedFileTest, ReaderStartsFromTheFilesExistingPosition) {
  // Section.cpp reads a header through the raw HalFile before wrapping it; the
  // wrapper adopts that position instead of assuming byte 0.
  const TempPath path("r_resume_pos");
  testutil::writeRaw(path.c_str(), rampBytes(400));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  ASSERT_TRUE(in.seek(10));

  serialization::BufferedFileReader reader(in, kCap);
  EXPECT_EQ(reader.position(), 10u);
  uint8_t quad[4] = {};
  ASSERT_EQ(reader.read(quad, 4), 4u);
  EXPECT_EQ(quad[0], 10);
  EXPECT_EQ(quad[3], 13);
  EXPECT_EQ(reader.position(), 14u);
}

TEST_F(BufferedFileTest, ReaderWithZeroCapacityIsPassthrough) {
  // Same cap == 0 branch the wrapper degrades to when the buffer allocation
  // fails: correct, just one underlying read per call.
  const TempPath path("r_passthrough");
  testutil::writeRaw(path.c_str(), rampBytes(32));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  halstub::reset();
  serialization::BufferedFileReader reader(in, 0);
  uint8_t quad[4] = {};
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(reader.read(quad, 4), 4u);
    EXPECT_EQ(quad[0], static_cast<uint8_t>(i * 4));
  }
  EXPECT_EQ(halstub::readCalls, 8u);
  EXPECT_EQ(reader.position(), 32u);
}

TEST_F(BufferedFileTest, ReaderWithZeroCapacitySeeksThroughToTheFile) {
  const TempPath path("r_passthrough_seek");
  testutil::writeRaw(path.c_str(), rampBytes(32));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, 0);
  uint8_t byte = 0;
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  halstub::reset();
  EXPECT_TRUE(reader.seek(16));
  EXPECT_EQ(halstub::seekCalls, 1u);
  EXPECT_EQ(reader.position(), 16u);
  ASSERT_EQ(reader.read(&byte, 1), 1u);
  EXPECT_EQ(byte, 16);
}

// --- buffered pod/string overloads -------------------------------------------

TEST_F(BufferedFileTest, PodsAndStringsRoundTripThroughBothWrappers) {
  const TempPath path("rt_mixed");
  const std::string title = "The Voyage of the Beagle";
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::BufferedFileWriter writer(out, kCap);
    serialization::writePod<uint32_t>(writer, 0x01020304u);
    serialization::writeString(writer, title);
    serialization::writePod<uint16_t>(writer, 0xFEDCu);
    serialization::writeString(writer, "");
    ASSERT_TRUE(writer.flush());
  }

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint32_t a = 0;
  uint16_t b = 0;
  std::string s, empty = "sentinel";
  EXPECT_TRUE(serialization::readPod(reader, a));
  EXPECT_TRUE(serialization::readString(reader, s));
  EXPECT_TRUE(serialization::readPod(reader, b));
  EXPECT_TRUE(serialization::readString(reader, empty));
  EXPECT_EQ(a, 0x01020304u);
  EXPECT_EQ(s, title);
  EXPECT_EQ(b, 0xFEDCu);
  EXPECT_TRUE(empty.empty());
}

TEST_F(BufferedFileTest, BufferedReadPodRejectsShortRead) {
  const TempPath path("rt_short_pod");
  testutil::writeRaw(path.c_str(), {0x01, 0x02, 0x03});

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  uint32_t value = 0;
  EXPECT_FALSE(serialization::readPod(reader, value));
}

TEST_F(BufferedFileTest, BufferedReadStringRejectsLengthAboveMax) {
  const TempPath path("rt_over_max");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, serialization::MAX_STRING_LENGTH + 1);
  testutil::appendChars(bytes, "payload");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(reader, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(BufferedFileTest, BufferedReadStringRejectsLyingLength) {
  // Under the cap but past the end of the file: the reader knows how many bytes
  // remain, so the claim is refused before resize() allocates from it.
  const TempPath path("rt_lying");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 4096);
  testutil::appendChars(bytes, "nine byte");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);  // its buffer is allocated outside the counted scope
  std::string s = "sentinel";                          // short enough to stay inside the small-string buffer
  bool ok = true;
  size_t allocations = 0;
  {
    const alloc_counter::CountingScope scope;
    ok = serialization::readString(reader, s);
    allocations = scope.count();
  }
  EXPECT_FALSE(ok);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(allocations, 0u);
}

TEST_F(BufferedFileTest, BufferedReadStringRejectsLengthOneByteBeyondEof) {
  // Tight boundary for the remaining-bytes check: the payload is one byte short
  // of the claim.
  const TempPath path("rt_off_by_one");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 5);
  testutil::appendChars(bytes, "four");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(reader, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(BufferedFileTest, BufferedReadStringHonoursCallerMax) {
  const TempPath path("rt_caller_max");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "0123456789");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(reader, s, 9));
  EXPECT_TRUE(s.empty());
}

TEST_F(BufferedFileTest, BufferedReadStringAcceptsLengthExactlyCallerMax) {
  // maxLen is inclusive here too: the rejection is len > maxLen.
  const TempPath path("rt_caller_max_ok");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "0123456789");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s;
  EXPECT_TRUE(serialization::readString(reader, s, 10));
  EXPECT_EQ(s, "0123456789");
}

TEST_F(BufferedFileTest, BufferedReadStringAcceptsExactlyMaxStringLength) {
  // 8KB across 128 refills of the 64-byte window.
  const TempPath path("rt_at_max");
  const std::string payload(serialization::MAX_STRING_LENGTH, 'x');
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, payload);
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s;
  EXPECT_TRUE(serialization::readString(reader, s));
  EXPECT_EQ(s, payload);
}

TEST_F(BufferedFileTest, BufferedStringLongerThanTheBufferRoundTrips) {
  const TempPath path("rt_long_string");
  const std::string payload(kCap * 5 + 7, 'q');
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::BufferedFileWriter writer(out, kCap);
    serialization::writeString(writer, payload);
    serialization::writePod<uint8_t>(writer, 0x99);
    ASSERT_TRUE(writer.flush());
  }

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s;
  uint8_t trailer = 0;
  EXPECT_TRUE(serialization::readString(reader, s));
  EXPECT_TRUE(serialization::readPod(reader, trailer));
  EXPECT_EQ(s, payload);
  EXPECT_EQ(trailer, 0x99);
}

TEST_F(BufferedFileTest, SeekThenReadStringRecoversTheSecondRecord) {
  const TempPath path("rt_seek_record");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "first");
  const size_t secondOffset = bytes.size();
  testutil::appendLengthPrefixed(bytes, "second");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  serialization::BufferedFileReader reader(in, kCap);
  std::string s;
  ASSERT_TRUE(serialization::readString(reader, s));
  ASSERT_EQ(s, "first");
  ASSERT_TRUE(reader.seek(secondOffset));
  ASSERT_TRUE(serialization::readString(reader, s));
  EXPECT_EQ(s, "second");
  ASSERT_TRUE(reader.seek(0));
  ASSERT_TRUE(serialization::readString(reader, s));
  EXPECT_EQ(s, "first");
}

}  // namespace
