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
#include <fstream>
#include <iterator>
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

// Regression guard: readFileToMemory validates the central directory's
// uncompressed-size field against a device-sane cap before allocating, so a
// corrupt/malicious archive declaring a ~4GB entry is rejected with a clean
// nullptr instead of a ~4GB malloc on a 380KB device.
TEST(ZipFileTest, LyingUncompressedSizeIsRejectedBeforeAllocation) {
  OwnedZip zip("lying_uncompressed_size.zip");

  // Size queries still report the raw header value; they feed progress math
  // only and never allocate from it.
  size_t size = 0;
  ASSERT_TRUE(zip->getInflatedFileSize("deflated.txt", &size));
  EXPECT_EQ(0xFFFFFFF0u, size);

  // The read path must reject before malloc: no allocation anywhere near the
  // lied-about size, and a graceful nullptr.
  uint8_t* data = nullptr;
  size_t maxAlloc = 0;
  {
    allocguard::TrackScope guard;
    data = zip->readFileToMemory("deflated.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap) << "readFileToMemory allocated for the lied-about size";

  // The streaming path allocates per-chunk regardless of the declared size,
  // and the size mismatch surfaces as a failed read.
  ByteCollector out;
  size_t maxStreamAlloc = 0;
  {
    allocguard::TrackScope guard;
    EXPECT_FALSE(zip->readFileToStream("deflated.txt", out, 64));
    maxStreamAlloc = allocguard::maxSingle;
  }
  EXPECT_LT(maxStreamAlloc, kSaneCap);
}

namespace {

// Byte-patch a fixture's central directory in a temp copy: find the CDH whose
// name matches, then overwrite (compressedSize, uncompressedSize). Layout per
// APPNOTE: CDH sig 0x02014b50, compSize at +20, uncompSize at +24, nameLen at
// +28, name at +46.
std::string patchCentralDirSizes(const char* fixture, const std::string& entryName, uint32_t compSize,
                                 uint32_t uncompSize) {
  std::ifstream in(res(fixture), std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(bytes.empty());

  const char sig[4] = {0x50, 0x4b, 0x01, 0x02};
  bool patched = false;
  for (size_t pos = 0; (pos = bytes.find(sig, pos, 4)) != std::string::npos; pos++) {
    if (pos + 46 > bytes.size()) break;
    uint16_t nameLen;
    std::memcpy(&nameLen, &bytes[pos + 28], sizeof(nameLen));
    if (pos + 46 + nameLen > bytes.size()) break;
    if (bytes.compare(pos + 46, nameLen, entryName) == 0) {
      std::memcpy(&bytes[pos + 20], &compSize, sizeof(compSize));
      std::memcpy(&bytes[pos + 24], &uncompSize, sizeof(uncompSize));
      patched = true;
      break;
    }
  }
  EXPECT_TRUE(patched) << "entry not found in central directory: " << entryName;

  const std::string outPath =
      ::testing::TempDir() + "patched_" + std::to_string(compSize) + "_" + std::to_string(uncompSize) + ".zip";
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return outPath;
}

}  // namespace

// A compressed size that overruns the physical archive must be rejected on
// both read paths before any payload is read (100000 passes the in-memory cap,
// so this exercises the fits-in-file check specifically).
TEST(ZipFileTest, CompressedSizePastEofIsRejected) {
  const std::string path = patchCentralDirSizes("good.zip", "stored.txt", 100000, 100000);
  ZipFile zip(path);
  EXPECT_EQ(nullptr, zip.readFileToMemory("stored.txt"));
  ByteCollector out;
  EXPECT_FALSE(zip.readFileToStream("stored.txt", out, 64));
}

// A STORED entry undergoes no transformation, so mismatched size fields can
// only come from corruption; the entry must be rejected, not read with the
// larger of the two.
TEST(ZipFileTest, StoredEntryWithMismatchedSizesIsRejected) {
  // Real stored payload is 42 bytes; keep compressed honest, lie uncompressed.
  const std::string path = patchCentralDirSizes("good.zip", "stored.txt", 42, 4242);
  ZipFile zip(path);
  size_t maxAlloc = 0;
  uint8_t* data = nullptr;
  {
    allocguard::TrackScope guard;
    data = zip.readFileToMemory("stored.txt");
    maxAlloc = allocguard::maxSingle;
  }
  EXPECT_EQ(nullptr, data);
  EXPECT_LT(maxAlloc, kSaneCap);
  ByteCollector out;
  EXPECT_FALSE(zip.readFileToStream("stored.txt", out, 64));
}

namespace {

// Byte-patch a fixture's EOCD in a temp copy: overwrite the entry counts (this
// disk at +8, total at +10) and, when cdSize is non-null, the declared
// central-directory size (+12). Everything else is left intact.
std::string patchEocd(const char* fixture, uint16_t totalEntries, const uint32_t* cdSize, const char* tag) {
  std::ifstream in(res(fixture), std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_FALSE(bytes.empty());

  const char sig[4] = {0x50, 0x4b, 0x05, 0x06};
  const size_t pos = bytes.rfind(sig, std::string::npos, 4);
  EXPECT_NE(std::string::npos, pos) << "no EOCD in fixture: " << fixture;
  if (pos != std::string::npos && pos + 22 <= bytes.size()) {
    std::memcpy(&bytes[pos + 8], &totalEntries, sizeof(totalEntries));
    std::memcpy(&bytes[pos + 10], &totalEntries, sizeof(totalEntries));
    if (cdSize != nullptr) std::memcpy(&bytes[pos + 12], cdSize, sizeof(*cdSize));
  }

  const std::string outPath = ::testing::TempDir() + "eocd_" + tag + ".zip";
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return outPath;
}

// Largest single allocation loadAllFileStatSlims may make while refusing a
// lying entry count. Reserving for 65535 entries costs ~256KB of hash buckets
// on the 32-bit device -- unsurvivable on a 380KB heap -- so the bound sits far
// below that and far above the few hundred bytes a real scan needs.
constexpr size_t kSlimReserveCap = 16 * 1024;

// Runs loadAllFileStatSlims under the allocation guard.
bool loadSlimsTrackingAllocs(ZipFile& zip, size_t* maxAlloc) {
  allocguard::TrackScope guard;
  const bool loaded = zip.loadAllFileStatSlims();
  *maxAlloc = allocguard::maxSingle;
  return loaded;
}

}  // namespace

// The EOCD entry count drives loadAllFileStatSlims' reserve, so it is validated
// against the directory that must hold those records before it is trusted: 46
// bytes is the fixed part of one central-directory header, so C bytes describe
// at most C / 46 entries. good.zip's directory is 175 bytes -- room for 3
// records, nowhere near 65535.
TEST(ZipFileTest, LoadAllFileStatSlimsRejectsEntryCountTooLargeForCentralDirectory) {
  const std::string path = patchEocd("good.zip", 65535, nullptr, "count_only");
  ZipFile zip(path);
  size_t maxAlloc = 0;
  const bool loaded = loadSlimsTrackingAllocs(zip, &maxAlloc);
  EXPECT_FALSE(loaded);
  EXPECT_LT(maxAlloc, kSlimReserveCap) << "reserved from the declared entry count";
}

// The declared directory size is untrusted too, so inflating it cannot launder
// the entry count: it is clamped to the bytes physically between the directory
// and the EOCD.
TEST(ZipFileTest, LoadAllFileStatSlimsClampsDeclaredCentralDirectorySize) {
  constexpr uint32_t hugeCdSize = 0xFFFFFFF0u;
  const std::string path = patchEocd("good.zip", 65535, &hugeCdSize, "count_and_size");
  ZipFile zip(path);
  size_t maxAlloc = 0;
  const bool loaded = loadSlimsTrackingAllocs(zip, &maxAlloc);
  EXPECT_FALSE(loaded);
  EXPECT_LT(maxAlloc, kSlimReserveCap) << "declared central-directory size was taken at face value";
}

// A ZIP64 archive's legacy EOCD carries 0xFFFF entries and a 0xFFFFFFFF
// directory offset. Nothing physically precedes that offset, so no positive
// entry count is credible and the slim-stat load is refused before reserving.
TEST(ZipFileTest, LoadAllFileStatSlimsRejectsZip64SentinelEntryCount) {
  OwnedZip zip("zip64.zip");
  size_t maxAlloc = 0;
  const bool loaded = loadSlimsTrackingAllocs(*zip, &maxAlloc);
  EXPECT_FALSE(loaded);
  EXPECT_LT(maxAlloc, kSlimReserveCap);
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
