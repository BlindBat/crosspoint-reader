// serialization::readPod/writePod/readString/writeString over both the HalFile
// and the std::iostream overloads (lib/Serialization/Serialization.h).
//
// Every cache file on the SD card is untrusted input: the length prefixes come
// straight off a card the user can edit, so the tests pin short reads, lying
// lengths, truncation and the pre-allocation rejection the header promises.

#include <Serialization.h>
#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "TestFileUtil.h"
#include "test/support/AllocCounter.h"

namespace {

using testutil::TempPath;

// Mixed-width POD standing in for the cache record structs writePod is used on.
struct PodRecord {
  uint32_t offset;
  int16_t delta;
  uint8_t flags;
  float ratio;
};

bool samePod(const PodRecord& a, const PodRecord& b) {
  return a.offset == b.offset && a.delta == b.delta && a.flags == b.flags && a.ratio == b.ratio;
}

class SerializationFileTest : public ::testing::Test {
 protected:
  void SetUp() override { halstub::reset(); }
  void TearDown() override { halstub::reset(); }
};

// --- HalFile writePod / readPod ---------------------------------------------

TEST_F(SerializationFileTest, WritePodReadPodRoundTripsScalars) {
  const TempPath path("pod_scalars");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::writePod<uint32_t>(out, 0xDEADBEEFu);
    serialization::writePod<int16_t>(out, -12345);
    serialization::writePod<uint8_t>(out, 0x5A);
    serialization::writePod<double>(out, 0.125);
  }

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  uint32_t u32 = 0;
  int16_t i16 = 0;
  uint8_t u8 = 0;
  double d = 0;
  EXPECT_TRUE(serialization::readPod(in, u32));
  EXPECT_TRUE(serialization::readPod(in, i16));
  EXPECT_TRUE(serialization::readPod(in, u8));
  EXPECT_TRUE(serialization::readPod(in, d));
  EXPECT_EQ(u32, 0xDEADBEEFu);
  EXPECT_EQ(i16, -12345);
  EXPECT_EQ(u8, 0x5A);
  EXPECT_DOUBLE_EQ(d, 0.125);
}

TEST_F(SerializationFileTest, WritePodReadPodRoundTripsStruct) {
  const TempPath path("pod_struct");
  const PodRecord written{4242u, -7, 0xC3, 0.5f};
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::writePod(out, written);
  }

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  PodRecord read{};
  EXPECT_TRUE(serialization::readPod(in, read));
  EXPECT_TRUE(samePod(read, written));
}

TEST_F(SerializationFileTest, ReadPodRejectsShortRead) {
  const TempPath path("pod_short");
  testutil::writeRaw(path.c_str(), {0x01, 0x02});  // two bytes where four are needed

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  uint32_t value = 0;
  EXPECT_FALSE(serialization::readPod(in, value));
}

TEST_F(SerializationFileTest, ReadPodRejectsReadAtEof) {
  const TempPath path("pod_eof");
  testutil::writeRaw(path.c_str(), {0x01, 0x02, 0x03, 0x04});

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  uint32_t value = 0;
  EXPECT_TRUE(serialization::readPod(in, value));
  EXPECT_FALSE(serialization::readPod(in, value));  // nothing left
}

TEST_F(SerializationFileTest, ReadPodOnEmptyFileFails) {
  const TempPath path("pod_empty");
  testutil::writeRaw(path.c_str(), {});

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  uint8_t value = 0xFF;
  EXPECT_FALSE(serialization::readPod(in, value));
}

TEST_F(SerializationFileTest, ReadPodCostsOneUnderlyingReadPerCall) {
  const TempPath path("pod_calls");
  testutil::writeRaw(path.c_str(), std::vector<uint8_t>(16, 0x11));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  halstub::reset();
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(serialization::readPod(in, value));
  EXPECT_EQ(halstub::readCalls, 4u);  // the unbuffered overloads do not batch
}

// --- HalFile writeString / readString ---------------------------------------

TEST_F(SerializationFileTest, WriteStringReadStringRoundTrips) {
  const TempPath path("str_roundtrip");
  const std::string title = "Alice's Adventures in Wonderland";
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path.c_str(), out));
    serialization::writeString(out, title);
    serialization::writeString(out, "");
    serialization::writeString(out, std::string("\x00\x01\xFF binary", 10));
  }

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string a, b, c;
  EXPECT_TRUE(serialization::readString(in, a));
  EXPECT_TRUE(serialization::readString(in, b));
  EXPECT_TRUE(serialization::readString(in, c));
  EXPECT_EQ(a, title);
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(c, std::string("\x00\x01\xFF binary", 10));
}

TEST_F(SerializationFileTest, ReadStringAcceptsLengthReachingExactlyEof) {
  const TempPath path("str_exact_eof");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "tail");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  EXPECT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(s, "tail");
}

TEST_F(SerializationFileTest, ReadStringRejectsTruncatedLengthPrefix) {
  const TempPath path("str_short_prefix");
  testutil::writeRaw(path.c_str(), {0x05, 0x00});  // half a length prefix

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(SerializationFileTest, ReadStringRejectsLengthPastEndOfFile) {
  const TempPath path("str_lying_length");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 4096);  // claims 4KB
  testutil::appendChars(bytes, "only nine");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(SerializationFileTest, ReadStringRejectsLengthOneByteBeyondEof) {
  const TempPath path("str_off_by_one");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 5);
  testutil::appendChars(bytes, "four");  // one short of the claim
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(SerializationFileTest, ReadStringRejectsLyingLengthBeforeAllocating) {
  // The header promises a corrupt prefix is rejected before the resize, which
  // is what keeps an 0xFFFFFFFF length from aborting a -fno-exceptions build.
  const TempPath path("str_no_alloc");
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 0xFFFFFFFFu);
  testutil::appendChars(bytes, "short");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  bool ok = true;
  size_t allocations = 0;
  {
    const alloc_counter::CountingScope scope;
    ok = serialization::readString(in, s);
    allocations = scope.count();
  }
  EXPECT_FALSE(ok);
  EXPECT_EQ(allocations, 0u);
}

TEST_F(SerializationFileTest, ReadStringRejectsLengthAboveCallerMax) {
  const TempPath path("str_caller_max");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "0123456789");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(in, s, 5));
  EXPECT_TRUE(s.empty());
}

TEST_F(SerializationFileTest, ReadStringAcceptsLengthExactlyCallerMax) {
  const TempPath path("str_caller_max_ok");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "0123456789");
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  EXPECT_TRUE(serialization::readString(in, s, 10));
  EXPECT_EQ(s, "0123456789");
}

TEST_F(SerializationFileTest, MaxStringLengthIsEightKilobytes) {
  EXPECT_EQ(serialization::MAX_STRING_LENGTH, 8u * 1024u);
}

TEST_F(SerializationFileTest, ReadStringAcceptsExactlyMaxStringLength) {
  const TempPath path("str_at_max");
  const std::string payload(serialization::MAX_STRING_LENGTH, 'x');
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, payload);
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  EXPECT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(s.size(), serialization::MAX_STRING_LENGTH);
}

TEST_F(SerializationFileTest, ReadStringRejectsOneByteOverMaxStringLength) {
  const TempPath path("str_over_max");
  const std::string payload(serialization::MAX_STRING_LENGTH + 1, 'x');
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, payload);  // the bytes really are there
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_TRUE(s.empty());
}

TEST_F(SerializationFileTest, ReadStringLeavesPositionAfterPayloadOnSuccess) {
  const TempPath path("str_position");
  std::vector<uint8_t> bytes;
  testutil::appendLengthPrefixed(bytes, "abc");
  testutil::appendU32(bytes, 0x0A0B0C0Du);
  testutil::writeRaw(path.c_str(), bytes);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s;
  ASSERT_TRUE(serialization::readString(in, s));
  EXPECT_EQ(in.position(), 7u);
  uint32_t trailer = 0;
  EXPECT_TRUE(serialization::readPod(in, trailer));
  EXPECT_EQ(trailer, 0x0A0B0C0Du);
}

TEST_F(SerializationFileTest, ReadStringOnEmptyFileFails) {
  const TempPath path("str_empty_file");
  testutil::writeRaw(path.c_str(), {});

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TEST", path.c_str(), in));
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(in, s));
  EXPECT_TRUE(s.empty());
}

// --- std::iostream overloads -------------------------------------------------

TEST(SerializationStreamTest, WritePodReadPodRoundTrips) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  const PodRecord written{7u, -1, 0x80, -2.25f};
  serialization::writePod<uint16_t>(ss, 0xBEEF);
  serialization::writePod(ss, written);

  uint16_t u16 = 0;
  PodRecord read{};
  EXPECT_TRUE(serialization::readPod(ss, u16));
  EXPECT_TRUE(serialization::readPod(ss, read));
  EXPECT_EQ(u16, 0xBEEF);
  EXPECT_TRUE(samePod(read, written));
}

TEST(SerializationStreamTest, ReadPodRejectsShortRead) {
  std::stringstream ss(std::string("\x01\x02", 2), std::ios::in | std::ios::binary);
  uint32_t value = 0;
  EXPECT_FALSE(serialization::readPod(ss, value));
}

TEST(SerializationStreamTest, ReadPodOnEmptyStreamFails) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  uint8_t value = 0xAA;
  EXPECT_FALSE(serialization::readPod(ss, value));
}

TEST(SerializationStreamTest, WriteStringReadStringRoundTrips) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  serialization::writeString(ss, "chapter-3.xhtml#anchor");
  serialization::writeString(ss, "");

  std::string a, b;
  EXPECT_TRUE(serialization::readString(ss, a));
  EXPECT_TRUE(serialization::readString(ss, b));
  EXPECT_EQ(a, "chapter-3.xhtml#anchor");
  EXPECT_TRUE(b.empty());
}

TEST(SerializationStreamTest, ReadStringRejectsLengthAboveMax) {
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, serialization::MAX_STRING_LENGTH + 1);
  std::stringstream ss(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(ss, s));
  EXPECT_TRUE(s.empty());
}

TEST(SerializationStreamTest, ReadStringRejectsTruncatedPayload) {
  // The stream overload has no size oracle, so it resizes first and only then
  // discovers the short read; it still reports failure and clears the output.
  std::vector<uint8_t> bytes;
  testutil::appendU32(bytes, 64);
  testutil::appendChars(bytes, "too short");
  std::stringstream ss(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(ss, s));
  EXPECT_TRUE(s.empty());
}

TEST(SerializationStreamTest, ReadStringRejectsTruncatedLengthPrefix) {
  std::stringstream ss(std::string("\x09\x00", 2), std::ios::in | std::ios::binary);
  std::string s = "sentinel";
  EXPECT_FALSE(serialization::readString(ss, s));
  EXPECT_TRUE(s.empty());
}

TEST(SerializationStreamTest, ReadStringHonoursCallerMax) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  serialization::writeString(ss, "0123456789");
  std::string s;
  EXPECT_FALSE(serialization::readString(ss, s, 9));
  EXPECT_TRUE(s.empty());
}

TEST(SerializationStreamTest, ReadStringAcceptsLengthExactlyCallerMax) {
  // maxLen is inclusive: the rejection is len > maxLen, not len >= maxLen.
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  serialization::writeString(ss, "0123456789");
  std::string s;
  EXPECT_TRUE(serialization::readString(ss, s, 10));
  EXPECT_EQ(s, "0123456789");
}

TEST(SerializationStreamTest, ReadStringAcceptsExactlyMaxStringLength) {
  const std::string payload(serialization::MAX_STRING_LENGTH, 'x');
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  serialization::writeString(ss, payload);
  std::string s;
  EXPECT_TRUE(serialization::readString(ss, s));
  EXPECT_EQ(s.size(), serialization::MAX_STRING_LENGTH);
}

}  // namespace
