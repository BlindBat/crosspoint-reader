// lib/ZipFile: batch size lookup, the sequential central-directory cursor,
// early-stop streaming and the EOCD / long-name rules of FR-188, on archives
// built byte-by-byte here (stored entries, plus raw-deflate "stored block"
// entries so the inflate path runs without a compressor).

#include <ZipFile.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "TestSupport.h"

namespace {

struct Entry {
  std::string name;
  std::string data;
  bool deflate = false;
  size_t localExtra = 0;    // bytes of local-header extra field
  size_t centralExtra = 0;  // bytes of central-directory extra field
  size_t comment = 0;       // bytes of central-directory comment
};

// Raw deflate stream made of one final stored block (RFC 1951 BTYPE=00).
std::vector<uint8_t> storedBlockDeflate(const std::string& data) {
  ByteWriter w;
  w.u8(0x01).le16(static_cast<uint32_t>(data.size())).le16(static_cast<uint32_t>(~data.size()) & 0xFFFF).raw(data);
  return w.bytes;
}

std::vector<uint8_t> buildZip(const std::vector<Entry>& entries, size_t archiveComment = 0) {
  ByteWriter out;
  std::vector<uint32_t> offsets;
  std::vector<std::vector<uint8_t>> payloads;
  for (const auto& e : entries) {
    offsets.push_back(static_cast<uint32_t>(out.bytes.size()));
    payloads.push_back(e.deflate ? storedBlockDeflate(e.data) : std::vector<uint8_t>(e.data.begin(), e.data.end()));
    const auto& payload = payloads.back();
    out.le32(0x04034b50).le16(20).le16(0).le16(e.deflate ? 8 : 0).le16(0).le16(0).le32(0);
    out.le32(static_cast<uint32_t>(payload.size())).le32(static_cast<uint32_t>(e.data.size()));
    out.le16(static_cast<uint32_t>(e.name.size())).le16(static_cast<uint32_t>(e.localExtra));
    out.raw(e.name).fill(e.localExtra, 0xEE).raw(payload);
  }
  const uint32_t cdOffset = static_cast<uint32_t>(out.bytes.size());
  for (size_t i = 0; i < entries.size(); i++) {
    const auto& e = entries[i];
    out.le32(0x02014b50).le16(20).le16(20).le16(0).le16(e.deflate ? 8 : 0).le16(0).le16(0).le32(0);
    out.le32(static_cast<uint32_t>(payloads[i].size())).le32(static_cast<uint32_t>(e.data.size()));
    out.le16(static_cast<uint32_t>(e.name.size())).le16(static_cast<uint32_t>(e.centralExtra));
    out.le16(static_cast<uint32_t>(e.comment)).le16(0).le16(0).le32(0).le32(offsets[i]);
    out.raw(e.name).fill(e.centralExtra, 0xCC).fill(e.comment, 'c');
  }
  const uint32_t cdSize = static_cast<uint32_t>(out.bytes.size()) - cdOffset;
  out.le32(0x06054b50).le16(0).le16(0).le16(static_cast<uint32_t>(entries.size()));
  out.le16(static_cast<uint32_t>(entries.size())).le32(cdSize).le32(cdOffset);
  out.le16(static_cast<uint32_t>(archiveComment)).fill(archiveComment, 'z');
  return out.bytes;
}

std::string bytesOf(size_t n, char seed = 'A') {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; i++) s[i] = static_cast<char>(seed + (i % 26));
  return s;
}

// Print sink that accepts at most `cap` bytes in total.
class CappedSink : public Print {
 public:
  explicit CappedSink(size_t cap) : cap(cap) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    const size_t room = cap - bytes.size();
    const size_t n = len < room ? len : room;
    bytes.insert(bytes.end(), data, data + n);
    return n;
  }
  size_t cap;
  std::vector<uint8_t> bytes;
};

ZipFile::SizeTarget target(const std::string& name, uint16_t index) {
  return {ZipFile::fnvHash64(name.data(), name.size()), static_cast<uint16_t>(name.size()), index};
}

void sortTargets(std::deque<ZipFile::SizeTarget>& t) {
  std::sort(t.begin(), t.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
    return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
  });
}

class ZipFileCursorTest : public ::testing::Test {
 protected:
  ScopedStorageRoot storage;
  std::string path = "/book.zip";
  const std::vector<Entry> threeEntries = {{"a.txt", bytesOf(5)}, {"b.txt", bytesOf(10)}, {"c.txt", bytesOf(20)}};

  void write(const std::vector<uint8_t>& bytes) { storage.put(path.c_str(), bytes); }
};

}  // namespace

/* ---------- fillUncompressedSizes ---------- */

TEST_F(ZipFileCursorTest, FillUncompressedSizesFillsMatchingTargets) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets = {target("a.txt", 0), target("c.txt", 2)};
  sortTargets(targets);
  std::deque<uint32_t> sizes(3, 0xFFFFFFFFu);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 2);
  EXPECT_EQ(sizes[0], 5u);
  EXPECT_EQ(sizes[1], 0xFFFFFFFFu);  // not requested
  EXPECT_EQ(sizes[2], 20u);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesFillsEveryTargetForTheSamePath) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets = {target("b.txt", 0), target("b.txt", 1)};
  std::deque<uint32_t> sizes(2, 0);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 2);
  EXPECT_EQ(sizes[0], 10u);
  EXPECT_EQ(sizes[1], 10u);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesIgnoresOutOfRangeIndex) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets = {target("a.txt", 7)};
  std::deque<uint32_t> sizes(2, 1);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 0);
  EXPECT_EQ(sizes[0], 1u);
  EXPECT_EQ(sizes[1], 1u);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesUnknownTargetLeavesSentinel) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets = {target("missing.xhtml", 0)};
  std::deque<uint32_t> sizes(1, 0xFFFFFFFFu);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 0);
  EXPECT_EQ(sizes[0], 0xFFFFFFFFu);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesWithNoTargetsIsZero) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets;
  std::deque<uint32_t> sizes;
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 0);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesOnUnreadableArchiveIsZero) {
  ZipFile zip(path);  // never written
  std::deque<ZipFile::SizeTarget> targets = {target("a.txt", 0)};
  std::deque<uint32_t> sizes(1, 0);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 0);
}

TEST_F(ZipFileCursorTest, EntryNamesOf256BytesOrMoreAreSkippedEverywhere) {
  const std::string longName(300, 'n');
  write(buildZip({{"a.txt", bytesOf(5)}, {longName, bytesOf(9)}, {"c.txt", bytesOf(20)}}));
  ZipFile zip(path);

  std::deque<ZipFile::SizeTarget> targets = {target(longName, 0), target("c.txt", 1)};
  sortTargets(targets);
  std::deque<uint32_t> sizes(2, 0);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 1);
  EXPECT_EQ(sizes[0], 0u);
  EXPECT_EQ(sizes[1], 20u);

  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize(longName.c_str(), &size));
  EXPECT_TRUE(zip.getInflatedFileSize("c.txt", &size));  // entries after the long name stay reachable
  EXPECT_EQ(size, 20u);

  std::vector<std::string> names;
  ASSERT_TRUE(zip.enumerateFilePaths([&names](std::string_view p) { names.emplace_back(p); }));
  EXPECT_EQ(names, (std::vector<std::string>{"a.txt", "c.txt"}));
}

/* ---------- sequential cursor ---------- */

TEST_F(ZipFileCursorTest, CursorFindsEntriesInSpineOrderWhileOpen) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("a.txt", &size));
  EXPECT_EQ(size, 5u);
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
  EXPECT_TRUE(zip.getInflatedFileSize("c.txt", &size));
  EXPECT_EQ(size, 20u);
  zip.close();
}

TEST_F(ZipFileCursorTest, CursorWrapsAroundToFindEarlierEntry) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  ASSERT_TRUE(zip.getInflatedFileSize("c.txt", &size));  // cursor now at end of directory
  EXPECT_TRUE(zip.getInflatedFileSize("a.txt", &size));
  EXPECT_EQ(size, 5u);
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
  zip.close();
}

TEST_F(ZipFileCursorTest, MissingEntryAfterCursorMovedTerminates) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  ASSERT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_FALSE(zip.getInflatedFileSize("nope.txt", &size));
  EXPECT_TRUE(zip.getInflatedFileSize("a.txt", &size));  // lookups keep working after a miss
  EXPECT_EQ(size, 5u);
  zip.close();
}

TEST_F(ZipFileCursorTest, SameEntryTwiceInARowIsFoundAgain) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  ASSERT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
  zip.close();
}

TEST_F(ZipFileCursorTest, LookupsWithoutExplicitOpenSucceedInAnyOrder) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("c.txt", &size));
  EXPECT_TRUE(zip.getInflatedFileSize("a.txt", &size));
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
  EXPECT_FALSE(zip.isOpen());
}

TEST_F(ZipFileCursorTest, SlimStatCacheAnswersLookupsWithoutScanning) {
  write(buildZip(threeEntries));
  ZipFile zip(path);
  ASSERT_TRUE(zip.loadAllFileStatSlims());
  storage.put(path.c_str(), std::string("gone"));  // archive replaced: cache must not need it
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("c.txt", &size));
  EXPECT_EQ(size, 20u);
  EXPECT_FALSE(zip.getInflatedFileSize("nope.txt", &size));
}

TEST_F(ZipFileCursorTest, CentralExtraFieldsAndCommentsAreSkipped) {
  write(buildZip(
      {{"a.txt", bytesOf(5), false, 0, 6, 9}, {"b.txt", bytesOf(10), false, 0, 3, 0}, {"c.txt", bytesOf(20)}}));
  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
  EXPECT_TRUE(zip.getInflatedFileSize("c.txt", &size));
  EXPECT_TRUE(zip.getInflatedFileSize("a.txt", &size));
  zip.close();
}

/* ---------- reading ---------- */

TEST_F(ZipFileCursorTest, LocalExtraFieldShiftsDataOffset) {
  write(buildZip({{"x.txt", bytesOf(40), false, 12}}));
  ZipFile zip(path);
  size_t size = 0;
  uint8_t* data = zip.readFileToMemory("x.txt", &size);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(size, 40u);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(data), size), bytesOf(40));
  free(data);
}

TEST_F(ZipFileCursorTest, StoredEntryStreamsInSmallChunks) {
  write(buildZip({{"x.txt", bytesOf(100)}}));
  ZipFile zip(path);
  CappedSink sink(SIZE_MAX);
  EXPECT_TRUE(zip.readFileToStream("x.txt", sink, 7));
  EXPECT_EQ(std::string(sink.bytes.begin(), sink.bytes.end()), bytesOf(100));
}

TEST_F(ZipFileCursorTest, DeflatedEntryStreamsToOriginalBytes) {
  write(buildZip({{"x.txt", bytesOf(100), true}}));
  ZipFile zip(path);
  CappedSink sink(SIZE_MAX);
  EXPECT_TRUE(zip.readFileToStream("x.txt", sink, 16));
  EXPECT_EQ(std::string(sink.bytes.begin(), sink.bytes.end()), bytesOf(100));
}

TEST_F(ZipFileCursorTest, StoredEarlyStopReturnsTrueWithPrefix) {
  write(buildZip({{"x.txt", bytesOf(100)}}));
  ZipFile zip(path);
  CappedSink sink(20);
  EXPECT_TRUE(zip.readFileToStream("x.txt", sink, 8, true));
  EXPECT_EQ(std::string(sink.bytes.begin(), sink.bytes.end()), bytesOf(100).substr(0, 20));
}

TEST_F(ZipFileCursorTest, StoredShortWriteWithoutEarlyStopFails) {
  write(buildZip({{"x.txt", bytesOf(100)}}));
  ZipFile zip(path);
  CappedSink sink(20);
  EXPECT_FALSE(zip.readFileToStream("x.txt", sink, 8, false));
  EXPECT_EQ(sink.bytes.size(), 20u);
}

TEST_F(ZipFileCursorTest, DeflatedEarlyStopReturnsTrueWithPrefix) {
  write(buildZip({{"x.txt", bytesOf(100), true}}));
  ZipFile zip(path);
  CappedSink sink(20);
  EXPECT_TRUE(zip.readFileToStream("x.txt", sink, 16, true));
  EXPECT_EQ(std::string(sink.bytes.begin(), sink.bytes.end()), bytesOf(100).substr(0, 20));
}

TEST_F(ZipFileCursorTest, DeflatedShortWriteWithoutEarlyStopFails) {
  write(buildZip({{"x.txt", bytesOf(100), true}}));
  ZipFile zip(path);
  CappedSink sink(20);
  EXPECT_FALSE(zip.readFileToStream("x.txt", sink, 16, false));
}

TEST_F(ZipFileCursorTest, SinkThatAcceptsEverythingNeverTriggersEarlyStop) {
  write(buildZip({{"x.txt", bytesOf(30)}}));
  ZipFile zip(path);
  CappedSink sink(30);
  EXPECT_TRUE(zip.readFileToStream("x.txt", sink, 8, true));
  EXPECT_EQ(sink.bytes.size(), 30u);
}

/* ---------- EOCD rules (FR-188) ---------- */

TEST_F(ZipFileCursorTest, ArchiveSmallerThanEocdIsRejected) {
  write(std::vector<uint8_t>(21, 0x50));
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("a.txt", &size));
  EXPECT_FALSE(zip.enumerateFilePaths([](std::string_view) {}));
}

TEST_F(ZipFileCursorTest, EocdWithinLastKilobyteIsFound) {
  write(buildZip(threeEntries, 1024 - 22));
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_TRUE(zip.getInflatedFileSize("b.txt", &size));
  EXPECT_EQ(size, 10u);
}

TEST_F(ZipFileCursorTest, EocdBeyondLastKilobyteIsRejected) {
  write(buildZip(threeEntries, 1024 - 21));
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("b.txt", &size));
}

TEST_F(ZipFileCursorTest, EmptyArchiveHasNoEntries) {
  write(buildZip({}));
  ZipFile zip(path);
  int count = 0;
  EXPECT_TRUE(zip.enumerateFilePaths([&count](std::string_view) { count++; }));
  EXPECT_EQ(count, 0);
}

TEST_F(ZipFileCursorTest, FillUncompressedSizesReportsInflatedSizeForDeflatedEntries) {
  write(buildZip({{"x.txt", bytesOf(60), true}}));
  ZipFile zip(path);
  std::deque<ZipFile::SizeTarget> targets = {target("x.txt", 0)};
  std::deque<uint32_t> sizes(1, 0);
  EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 1);
  EXPECT_EQ(sizes[0], 60u);  // not the 65-byte stored-block payload
}

TEST_F(ZipFileCursorTest, SinkThatRefusesTheFirstByteStopsEarlyWithoutOutput) {
  write(buildZip({{"x.txt", bytesOf(50)}, {"y.txt", bytesOf(50), true}}));
  ZipFile zip(path);
  CappedSink stored(0);
  EXPECT_TRUE(zip.readFileToStream("x.txt", stored, 8, true));
  EXPECT_TRUE(stored.bytes.empty());
  CappedSink deflated(0);
  EXPECT_TRUE(zip.readFileToStream("y.txt", deflated, 16, true));
  EXPECT_TRUE(deflated.bytes.empty());
}

TEST_F(ZipFileCursorTest, ZeroLengthEntryStreamsNothingAndSucceeds) {
  write(buildZip({{"empty.txt", ""}}));
  ZipFile zip(path);
  size_t size = 1;
  EXPECT_TRUE(zip.getInflatedFileSize("empty.txt", &size));
  EXPECT_EQ(size, 0u);
  CappedSink sink(SIZE_MAX);
  EXPECT_TRUE(zip.readFileToStream("empty.txt", sink, 8));
  EXPECT_TRUE(sink.bytes.empty());
}
