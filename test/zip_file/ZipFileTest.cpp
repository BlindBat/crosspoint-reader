// Host suite for lib/ZipFile: enumeration + entry reads on a known-good
// archive, and graceful rejection of malformed archives (no crash, and no
// allocation larger than the device could survive).
//
// Fixtures live under resources/ and are produced by
// scripts/generate_test_zips.py. ZIP_RESOURCES_DIR is injected by CMake.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "AllocGuard.h"  // defines the global allocator interposers (one TU only)
#include "ZipFile.h"

namespace {

std::string res(const char* name) { return std::string(ZIP_RESOURCES_DIR) + "/" + name; }

// ZipFile holds a `const std::string&` to its path, so the string must outlive
// it. This owns the path and exposes the ZipFile via operator-> / operator*.
struct OwnedZip {
  std::string path;
  ZipFile zip;
  explicit OwnedZip(const char* name) : path(res(name)), zip(path) {}
  ZipFile* operator->() { return &zip; }
  ZipFile& operator*() { return zip; }
};

// A Print sink that captures everything written to it.
class ByteCollector : public Print {
 public:
  size_t write(uint8_t b) override {
    bytes.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* data, size_t len) override {
    bytes.insert(bytes.end(), data, data + len);
    return len;
  }
  std::vector<uint8_t> bytes;
};

const std::string kStored = "This entry is stored without compression.\n";
std::string deflatedPayload() {
  std::string s;
  for (int i = 0; i < 64; i++) s += "Deflate me! ";
  return s;
}
std::string nestedPayload() {
  std::string s;
  for (int i = 0; i < 16; i++) s += "Line of nested content.\n";
  return s;
}

// A "sane cap": anything a single malloc above this during malformed-input
// handling would be fatal on the ~380KB device. Generous headroom for legit
// buffers (the largest real entry here is <1KB).
constexpr size_t kSaneCap = 1u << 20;  // 1 MiB

}  // namespace

/* ---------- Good archive: enumeration ---------- */

TEST(ZipFileTest, EnumeratesEveryCentralDirectoryEntry) {
  OwnedZip zip("good.zip");
  std::vector<std::string> names;
  std::vector<uint32_t> crcs;
  std::vector<uint32_t> compSizes;

  const bool ok = zip->enumerateFileEntries([&](std::string_view path, uint32_t crc, uint32_t comp) {
    names.emplace_back(path);
    crcs.push_back(crc);
    compSizes.push_back(comp);
  });

  ASSERT_TRUE(ok);
  ASSERT_EQ(3u, names.size());

  // Verify per-entry CRC and compressed size by name (enumeration order is the
  // central-directory order, but assert by lookup to stay robust).
  for (size_t i = 0; i < names.size(); i++) {
    if (names[i] == "stored.txt") {
      EXPECT_EQ(0x3F840BE8u, crcs[i]);
      EXPECT_EQ(42u, compSizes[i]);
    } else if (names[i] == "deflated.txt") {
      EXPECT_EQ(0xEB0E355Fu, crcs[i]);
      EXPECT_EQ(21u, compSizes[i]);  // deflated => smaller than the 768-byte payload
    } else if (names[i] == "nested/deep.txt") {
      EXPECT_EQ(0x1275D4BEu, crcs[i]);
      EXPECT_EQ(31u, compSizes[i]);
    } else {
      ADD_FAILURE() << "unexpected entry: " << names[i];
    }
  }
}

TEST(ZipFileTest, EnumerateFilePathsListsEveryEntry) {
  OwnedZip zip("good.zip");
  std::vector<std::string> names;
  ASSERT_TRUE(zip->enumerateFilePaths([&](std::string_view p) { names.emplace_back(p); }));
  std::sort(names.begin(), names.end());
  ASSERT_EQ(3u, names.size());
  EXPECT_EQ("deflated.txt", names[0]);
  EXPECT_EQ("nested/deep.txt", names[1]);
  EXPECT_EQ("stored.txt", names[2]);
}

TEST(ZipFileTest, LoadAllFileStatSlimsCachesThenEnumeratesFromCache) {
  OwnedZip zip("good.zip");
  ASSERT_TRUE(zip->open());
  ASSERT_TRUE(zip->loadAllFileStatSlims());
  // With the cache populated, enumerateFilePaths serves from it rather than
  // re-scanning the central directory.
  std::vector<std::string> names;
  ASSERT_TRUE(zip->enumerateFilePaths([&](std::string_view p) { names.emplace_back(p); }));
  EXPECT_EQ(3u, names.size());
  zip->close();
}

/* ---------- Good archive: reads ---------- */

TEST(ZipFileTest, ReadsStoredEntryVerbatim) {
  OwnedZip zip("good.zip");
  size_t size = 0;
  uint8_t* data = zip->readFileToMemory("stored.txt", &size);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(kStored.size(), size);
  EXPECT_EQ(0, std::memcmp(data, kStored.data(), size));
  free(data);
}

TEST(ZipFileTest, InflatesDeflatedEntryToOriginalBytes) {
  const std::string expected = deflatedPayload();
  OwnedZip zip("good.zip");
  size_t size = 0;
  uint8_t* data = zip->readFileToMemory("deflated.txt", &size);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(expected.size(), size);
  EXPECT_EQ(0, std::memcmp(data, expected.data(), size));
  free(data);
}

TEST(ZipFileTest, InflatesNestedDeflatedEntry) {
  const std::string expected = nestedPayload();
  OwnedZip zip("good.zip");
  size_t size = 0;
  uint8_t* data = zip->readFileToMemory("nested/deep.txt", &size);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(expected.size(), size);
  EXPECT_EQ(0, std::memcmp(data, expected.data(), size));
  free(data);
}

TEST(ZipFileTest, TrailingNullByteIsAppendedAfterContent) {
  OwnedZip zip("good.zip");
  size_t size = 0;
  uint8_t* data = zip->readFileToMemory("stored.txt", &size, /*trailingNullByte=*/true);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(kStored.size(), size);
  EXPECT_EQ('\0', data[size]);
  free(data);
}

TEST(ZipFileTest, ReadFileToStreamProducesSameBytesAsMemory) {
  OwnedZip zip("good.zip");
  ByteCollector storedOut;
  ASSERT_TRUE(zip->readFileToStream("stored.txt", storedOut, 8));
  ASSERT_EQ(kStored.size(), storedOut.bytes.size());
  EXPECT_EQ(0, std::memcmp(storedOut.bytes.data(), kStored.data(), kStored.size()));

  const std::string expected = deflatedPayload();
  ByteCollector deflatedOut;
  ASSERT_TRUE(zip->readFileToStream("deflated.txt", deflatedOut, 16));
  ASSERT_EQ(expected.size(), deflatedOut.bytes.size());
  EXPECT_EQ(0, std::memcmp(deflatedOut.bytes.data(), expected.data(), expected.size()));
}

TEST(ZipFileTest, GetInflatedFileSizeReturnsUncompressedSize) {
  OwnedZip zip("good.zip");
  size_t size = 0;
  ASSERT_TRUE(zip->getInflatedFileSize("stored.txt", &size));
  EXPECT_EQ(42u, size);
  ASSERT_TRUE(zip->getInflatedFileSize("deflated.txt", &size));
  EXPECT_EQ(768u, size);
  ASSERT_TRUE(zip->getInflatedFileSize("nested/deep.txt", &size));
  EXPECT_EQ(384u, size);
}

TEST(ZipFileTest, MissingEntryReturnsNullptr) {
  OwnedZip zip("good.zip");
  size_t size = 123;
  EXPECT_EQ(nullptr, zip->readFileToMemory("does/not/exist.txt", &size));
  EXPECT_FALSE(zip->getInflatedFileSize("does/not/exist.txt", &size));
}

/* ---------- Malformed archives: graceful rejection ---------- */

TEST(ZipFileTest, TruncatedCentralDirectoryIsRejected) {
  OwnedZip zip("truncated_central_dir.zip");
  // No EOCD survives the truncation, so nothing can be enumerated or read.
  size_t maxAlloc = 0;
  uint8_t* data = nullptr;
  bool enumerated = true;
  {
    allocguard::TrackScope guard;
    enumerated = zip->enumerateFileEntries([](std::string_view, uint32_t, uint32_t) {});
    data = zip->readFileToMemory("stored.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_FALSE(enumerated);
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
}

TEST(ZipFileTest, EocdOffsetPastEofIsRejected) {
  OwnedZip zip("eocd_offset_past_eof.zip");
  int seen = 0;
  size_t maxAlloc = 0;
  uint8_t* data = nullptr;
  {
    allocguard::TrackScope guard;
    // EOCD parses, but its central-directory offset points past EOF: the scan
    // finds no entries and reads nothing out of bounds.
    zip->enumerateFileEntries([&](std::string_view, uint32_t, uint32_t) { seen++; });
    data = zip->readFileToMemory("stored.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(0, seen);
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
}

TEST(ZipFileTest, LocalHeaderMismatchRejectsOnlyTheCorruptedEntry) {
  OwnedZip zip("local_central_mismatch.zip");
  // stored.txt's local file header signature is clobbered; the central
  // directory still references it, so getDataOffset must catch the bad header.
  EXPECT_EQ(nullptr, zip->readFileToMemory("stored.txt"));

  // The other entries' local headers are intact and still read correctly,
  // proving the rejection is specific to the corrupted record.
  const std::string expected = deflatedPayload();
  size_t size = 0;
  uint8_t* data = zip->readFileToMemory("deflated.txt", &size);
  ASSERT_NE(nullptr, data);
  EXPECT_EQ(expected.size(), size);
  EXPECT_EQ(0, std::memcmp(data, expected.data(), size));
  free(data);
}

// Documents a CURRENT limitation (see bugsFound): readFileToMemory trusts the
// central directory's uncompressed-size field and mallocs it before reading a
// single byte. A corrupt/malicious archive declaring a ~4GB size makes the
// firmware attempt a ~4GB allocation on a 380KB device. The guard records the
// attempted size and simulates the device's allocation failure (failAbove) so
// the host does not actually reserve gigabytes.
TEST(ZipFileTest, LyingUncompressedSizeTriggersOversizedAllocation) {
  OwnedZip zip("lying_uncompressed_size.zip");

  // The size field is trusted verbatim...
  size_t size = 0;
  ASSERT_TRUE(zip->getInflatedFileSize("deflated.txt", &size));
  EXPECT_EQ(0xFFFFFFF0u, size) << "size is taken from the header without validation";

  // ...and readFileToMemory allocates it. Simulate device OOM for the huge
  // request; the call must then fail gracefully (return nullptr, no crash).
  uint8_t* data = nullptr;
  size_t maxAlloc = 0;
  {
    allocguard::TrackScope guard(/*failAbove=*/64u << 20);  // 64 MiB
    data = zip->readFileToMemory("deflated.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(nullptr, data);
  // Pinning the bug: the attempted allocation is enormous (would be fatal on
  // device). If ZipFile is fixed to bound sizes, this expectation should be
  // tightened to EXPECT_LT(maxAlloc, kSaneCap).
  EXPECT_GE(maxAlloc, 0xFFFFFFF0u) << "readFileToMemory attempted to allocate the lied-about size";
}

TEST(ZipFileTest, GarbageDeflateStreamIsRejectedWithoutHugeAllocation) {
  OwnedZip zip("garbage_deflate.zip");
  uint8_t* data = nullptr;
  size_t maxAlloc = 0;
  {
    allocguard::TrackScope guard;
    // Sizes are legitimate here; only the compressed payload is corrupt, so
    // the inflate step must fail and free the (small) output buffer.
    data = zip->readFileToMemory("deflated.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
}

TEST(ZipFileTest, Zip64SentinelEocdIsIgnoredSafely) {
  OwnedZip zip("zip64.zip");
  // lib/ZipFile parses only the legacy 32-bit EOCD, which here holds ZIP64
  // sentinels (0xFFFF entries, 0xFFFFFFFF cd offset). It must not crash and
  // must not reserve from the bogus 65535 entry count on the read paths.
  int seen = 0;
  uint8_t* data = nullptr;
  size_t maxAlloc = 0;
  {
    allocguard::TrackScope guard;
    zip->enumerateFileEntries([&](std::string_view, uint32_t, uint32_t) { seen++; });
    data = zip->readFileToMemory("zip64.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(0, seen);
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
}

TEST(ZipFileTest, EmptyArchiveEnumeratesNothing) {
  OwnedZip zip("empty.zip");
  int seen = 0;
  EXPECT_TRUE(zip->enumerateFileEntries([&](std::string_view, uint32_t, uint32_t) { seen++; }));
  EXPECT_EQ(0, seen);
  EXPECT_EQ(nullptr, zip->readFileToMemory("anything.txt"));
}

TEST(ZipFileTest, NonZipBytesAreRejected) {
  OwnedZip zip("not_a_zip.bin");
  size_t maxAlloc = 0;
  bool enumerated = true;
  uint8_t* data = nullptr;
  {
    allocguard::TrackScope guard;
    enumerated = zip->enumerateFileEntries([](std::string_view, uint32_t, uint32_t) {});
    data = zip->readFileToMemory("anything.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_FALSE(enumerated);
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
}
