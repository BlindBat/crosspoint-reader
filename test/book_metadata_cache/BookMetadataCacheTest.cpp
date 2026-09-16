// Host suite for lib/Epub/Epub/BookMetadataCache: the on-disk book.bin cache.
//
// Covers a full write->read round-trip (metadata, spine, TOC, cumulative
// sizes), the version gate that must reject stale/newer caches, and how load()
// behaves on corrupt input -- truncation at every section boundary and a
// length-prefixed string with a bogus u32 length. The binary layout is
// documented in docs/file-formats.md; the tests follow the real code.
//
// BOOK_RESOURCES_DIR (a small EPUB-shaped ZIP produced by
// scripts/generate_test_zips.py) is injected by CMake; buildBookBin reads real
// uncompressed sizes from it, so the cumulative-size assertions are meaningful.

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "AllocGuard.h"  // defines the global allocator interposers (one TU only)
#include "BookMetadataCache.h"

namespace {

// Must match BOOK_CACHE_VERSION in BookMetadataCache.cpp and docs/file-formats.md.
constexpr uint8_t kExpectedVersion = 10;

std::string epubPath() { return std::string(BOOK_RESOURCES_DIR) + "/book.epub"; }

// Uncompressed sizes of the fixture's spine items (see generate_test_zips.py).
constexpr uint32_t kSize1 = 38;
constexpr uint32_t kSize2 = 130;
constexpr uint32_t kSize3 = 48;

std::string makeTempDir() {
  char tmpl[] = "/tmp/bmc_testXXXXXX";
  char* dir = ::mkdtemp(tmpl);
  EXPECT_NE(nullptr, dir);
  return std::string(dir);
}

std::string bookBinPath(const std::string& dir) { return dir + "/book.bin"; }

std::vector<uint8_t> readFile(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  EXPECT_NE(nullptr, f) << "could not open " << path;
  if (!f) return {};
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(n > 0 ? static_cast<size_t>(n) : 0);
  if (!data.empty()) {
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    (void)got;
  }
  std::fclose(f);
  return data;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(nullptr, f);
  if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
}

// Drive the public build API to produce a valid book.bin in `dir`. Spine hrefs
// intentionally equal the fixture ZIP's entry names so buildBookBin resolves
// real uncompressed sizes for them.
BookMetadataCache::BookMetadata buildValidCache(const std::string& dir) {
  BookMetadataCache cache(dir);
  EXPECT_TRUE(cache.beginWrite());

  EXPECT_TRUE(cache.beginContentOpfPass());
  cache.createSpineEntry("OEBPS/chapter1.xhtml");
  cache.createSpineEntry("OEBPS/chapter2.xhtml");
  cache.createSpineEntry("OEBPS/chapter3.xhtml");
  EXPECT_TRUE(cache.endContentOpfPass());

  EXPECT_TRUE(cache.beginTocPass());
  cache.createTocEntry("Chapter One", "OEBPS/chapter1.xhtml", "", 0);
  cache.createTocEntry("Chapter Two", "OEBPS/chapter2.xhtml", "sec2", 1);
  EXPECT_TRUE(cache.endTocPass());

  EXPECT_TRUE(cache.endWrite());

  BookMetadataCache::BookMetadata md;
  md.title = "The Test Book";
  md.author = "A. Tester";
  md.language = "en";
  md.coverItemHref = "OEBPS/cover.png";
  md.textReferenceHref = "OEBPS/chapter1.xhtml";

  const std::string path = epubPath();
  EXPECT_TRUE(cache.buildBookBin(path, md));
  return md;
}

enum class LoadOutcome { ReturnedTrue, ReturnedFalse, Threw };

// Run load() on the cache at `dir`, guarding the heap so an unbounded
// allocation fails like the device would instead of the host committing GBs.
LoadOutcome loadGuarded(const std::string& dir, size_t failAbove, size_t* maxAllocOut = nullptr) {
  BookMetadataCache cache(dir);
  LoadOutcome outcome = LoadOutcome::ReturnedFalse;
  {
    allocguard::TrackScope guard(failAbove);
    try {
      outcome = cache.load() ? LoadOutcome::ReturnedTrue : LoadOutcome::ReturnedFalse;
    } catch (const std::exception&) {
      outcome = LoadOutcome::Threw;
    }
    if (maxAllocOut) *maxAllocOut = allocguard::maxSingle;
  }
  return outcome;
}

}  // namespace

/* ---------- Round-trip ---------- */

TEST(BookMetadataCacheTest, WriteThenReadPreservesMetadataSpineAndToc) {
  const std::string dir = makeTempDir();
  const auto md = buildValidCache(dir);

  BookMetadataCache cache(dir);
  ASSERT_TRUE(cache.load());
  ASSERT_TRUE(cache.isLoaded());

  // Core metadata survives verbatim.
  EXPECT_EQ(md.title, cache.coreMetadata.title);
  EXPECT_EQ(md.author, cache.coreMetadata.author);
  EXPECT_EQ(md.language, cache.coreMetadata.language);
  EXPECT_EQ(md.coverItemHref, cache.coreMetadata.coverItemHref);
  EXPECT_EQ(md.textReferenceHref, cache.coreMetadata.textReferenceHref);

  ASSERT_EQ(3, cache.getSpineCount());
  ASSERT_EQ(2, cache.getTocCount());

  // Spine hrefs and cumulative sizes (summed from the fixture's real entries).
  EXPECT_EQ("OEBPS/chapter1.xhtml", cache.getSpineEntry(0).href);
  EXPECT_EQ("OEBPS/chapter2.xhtml", cache.getSpineEntry(1).href);
  EXPECT_EQ("OEBPS/chapter3.xhtml", cache.getSpineEntry(2).href);
  EXPECT_EQ(kSize1, cache.getSpineEntry(0).cumulativeSize);
  EXPECT_EQ(kSize1 + kSize2, cache.getSpineEntry(1).cumulativeSize);
  EXPECT_EQ(kSize1 + kSize2 + kSize3, cache.getSpineEntry(2).cumulativeSize);

  // The RAM-cached cumulative sizes agree with the on-disk spine entries.
  EXPECT_EQ(kSize1, cache.getCumulativeSize(0));
  EXPECT_EQ(kSize1 + kSize2, cache.getCumulativeSize(1));
  EXPECT_EQ(kSize1 + kSize2 + kSize3, cache.getCumulativeSize(2));

  // Spine->TOC linkage: chapters 1 and 2 have direct TOC entries; chapter 3
  // inherits the previous entry's index (buildBookBin's documented behavior).
  EXPECT_EQ(0, cache.getSpineEntry(0).tocIndex);
  EXPECT_EQ(1, cache.getSpineEntry(1).tocIndex);
  EXPECT_EQ(1, cache.getSpineEntry(2).tocIndex);

  // TOC entries round-trip including anchor, level and spine linkage.
  const auto toc0 = cache.getTocEntry(0);
  EXPECT_EQ("Chapter One", toc0.title);
  EXPECT_EQ("OEBPS/chapter1.xhtml", toc0.href);
  EXPECT_EQ("", toc0.anchor);
  EXPECT_EQ(0, toc0.level);
  EXPECT_EQ(0, toc0.spineIndex);

  const auto toc1 = cache.getTocEntry(1);
  EXPECT_EQ("Chapter Two", toc1.title);
  EXPECT_EQ("sec2", toc1.anchor);
  EXPECT_EQ(1, toc1.level);
  EXPECT_EQ(1, toc1.spineIndex);
}

TEST(BookMetadataCacheTest, OutOfRangeAccessorsReturnDefaults) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);

  BookMetadataCache cache(dir);
  ASSERT_TRUE(cache.load());

  EXPECT_EQ(0u, cache.getCumulativeSize(-1));
  EXPECT_EQ(0u, cache.getCumulativeSize(99));
  // Out-of-range spine/TOC reads yield default-constructed entries.
  EXPECT_EQ("", cache.getSpineEntry(99).href);
  EXPECT_EQ(-1, cache.getSpineEntry(99).tocIndex);
  EXPECT_EQ("", cache.getTocEntry(99).title);
}

/* ---------- Version gate ---------- */

TEST(BookMetadataCacheTest, LoadRejectsOlderVersionByte) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);

  auto bytes = readFile(bookBinPath(dir));
  ASSERT_FALSE(bytes.empty());
  ASSERT_EQ(kExpectedVersion, bytes[0]);
  bytes[0] = kExpectedVersion - 1;  // pretend an older cache format
  writeFile(bookBinPath(dir), bytes);

  BookMetadataCache cache(dir);
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
}

TEST(BookMetadataCacheTest, LoadRejectsNewerVersionByte) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);

  auto bytes = readFile(bookBinPath(dir));
  ASSERT_FALSE(bytes.empty());
  bytes[0] = kExpectedVersion + 1;  // a newer cache the firmware can't parse
  writeFile(bookBinPath(dir), bytes);

  BookMetadataCache cache(dir);
  EXPECT_FALSE(cache.load());
}

TEST(BookMetadataCacheTest, LoadRejectsWhenFileMissing) {
  const std::string dir = makeTempDir();
  // No book.bin written at all.
  BookMetadataCache cache(dir);
  EXPECT_FALSE(cache.load());
}

/* ---------- Corruption: truncation ---------- */

// Regression guard: load() validates every read past the version byte -- header
// pods, metadata strings, LUT extent, the full spine block, and the last TOC
// entry (the file's tail). A book.bin truncated at ANY section boundary is
// rejected with a clean `false`, which sends the caller down the reparse path
// instead of serving a corrupt cache. Allocation stays bounded throughout.
TEST(BookMetadataCacheTest, TruncationAtEverySectionBoundaryIsRejected) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);
  const auto full = readFile(bookBinPath(dir));
  ASSERT_GT(full.size(), 16u);
  ASSERT_EQ(kExpectedVersion, full[0]);

  // Section boundaries per docs/file-formats.md, derived from the real header.
  uint32_t lutOffset = 0;
  uint16_t spineCount = 0;
  uint16_t tocCount = 0;
  std::memcpy(&lutOffset, &full[1], sizeof(lutOffset));
  std::memcpy(&spineCount, &full[5], sizeof(spineCount));
  std::memcpy(&tocCount, &full[7], sizeof(tocCount));
  const uint32_t spineLutBytes = static_cast<uint32_t>(spineCount) * sizeof(uint32_t);
  const uint32_t lutBytes = spineLutBytes + static_cast<uint32_t>(tocCount) * sizeof(uint32_t);

  const std::vector<size_t> boundaries = {
      1,                          // after the version byte only
      9,                          // after header A (version + lutOffset + counts)
      lutOffset,                  // after the metadata block
      lutOffset + spineLutBytes,  // after the spine LUT
      lutOffset + lutBytes,       // after both LUTs (start of spine entries)
      full.size() - 4,            // just short of the end
  };

  const std::string tdir = makeTempDir();
  const std::string tpath = bookBinPath(tdir);
  for (size_t cut : boundaries) {
    if (cut == 0 || cut >= full.size()) continue;
    SCOPED_TRACE("truncated to " + std::to_string(cut) + " of " + std::to_string(full.size()) + " bytes");
    std::vector<uint8_t> truncated(full.begin(), full.begin() + cut);
    writeFile(tpath, truncated);

    // 64 MiB cap: any request past it is a device-fatal allocation.
    size_t maxAlloc = 0;
    const LoadOutcome outcome = loadGuarded(tdir, 64u << 20, &maxAlloc);

    EXPECT_EQ(LoadOutcome::ReturnedFalse, outcome) << "load() must reject the truncated cache";
    EXPECT_LT(maxAlloc, 64u * 1024u) << "rejection must not allocate unboundedly";
  }
}

// A byte appended past the last TOC entry must not reject the cache: the
// tail check reads the last TOC entry through its LUT slot, and trailing
// slack (e.g. from a pre-truncation copy) leaves that entry intact.
TEST(BookMetadataCacheTest, TrailingGarbageAfterLastTocEntryStillLoads) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);
  auto bytes = readFile(bookBinPath(dir));
  ASSERT_FALSE(bytes.empty());
  bytes.push_back(0xAB);
  writeFile(bookBinPath(dir), bytes);

  BookMetadataCache cache(dir);
  EXPECT_TRUE(cache.load());
  EXPECT_EQ(3, cache.getSpineCount());
}

/* ---------- Corruption: oversized length-prefixed string ---------- */

// Regression guard: serialization::readString bounds every length prefix
// against MAX_STRING_LENGTH and the bytes actually left in the file, so a
// corrupt/malicious book.bin declaring a ~4GB string can no longer drive an
// unbounded std::string::resize (which, with -fno-exceptions on the device,
// would abort the firmware when the allocation fails).
TEST(BookMetadataCacheTest, HugeStringLengthIsRejectedWithoutUnboundedAllocation) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);

  auto bytes = readFile(bookBinPath(dir));
  ASSERT_GT(bytes.size(), 13u);
  ASSERT_EQ(kExpectedVersion, bytes[0]);

  // The title string's 4-byte length prefix sits immediately after header A
  // (offset 9). Overwrite it with a ~4GB length.
  const uint32_t bogusLen = 0xFFFFFFF0u;
  std::memcpy(&bytes[9], &bogusLen, sizeof(bogusLen));
  writeFile(bookBinPath(dir), bytes);

  size_t maxAlloc = 0;
  const LoadOutcome outcome = loadGuarded(dir, 64u << 20, &maxAlloc);

  // The lied-about length must never reach an allocator: the largest single
  // allocation stays far below the device's ~380KB heap.
  EXPECT_LT(maxAlloc, 64u * 1024u) << "load() allocated for an unvalidated declared length";
  EXPECT_EQ(LoadOutcome::ReturnedFalse, outcome) << "load() must reject the corrupt string length";
}

/* ---------- Corruption: lookup tables ---------- */

namespace {

// Header field offsets (docs/file-formats.md): version(1) lutOffset(4)
// spineCount(2) tocCount(2).
constexpr size_t kOffLutOffset = 1;
constexpr size_t kOffSpineCount = 5;
constexpr size_t kOffTocCount = 7;

struct BookLayout {
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  size_t spineLutAt;   // first spine LUT slot
  size_t tocLutAt;     // first TOC LUT slot
  uint32_t entriesAt;  // first byte after both LUTs (start of the spine block)
};

BookLayout layoutOf(const std::vector<uint8_t>& bytes) {
  BookLayout l{};
  std::memcpy(&l.lutOffset, &bytes[kOffLutOffset], sizeof(l.lutOffset));
  std::memcpy(&l.spineCount, &bytes[kOffSpineCount], sizeof(l.spineCount));
  std::memcpy(&l.tocCount, &bytes[kOffTocCount], sizeof(l.tocCount));
  l.spineLutAt = l.lutOffset;
  l.tocLutAt = l.lutOffset + l.spineCount * sizeof(uint32_t);
  l.entriesAt = l.lutOffset + (l.spineCount + l.tocCount) * sizeof(uint32_t);
  return l;
}

void pokeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  std::memcpy(&bytes[offset], &value, sizeof(value));
}

uint32_t peekU32(const std::vector<uint8_t>& bytes, size_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, &bytes[offset], sizeof(value));
  return value;
}

}  // namespace

// Regression guard: load() validates every LUT slot, not just the last TOC one.
// getSpineEntry/getTocEntry seek blindly to whatever a slot holds, so a slot
// pointing into the header, into the other block, or past the end of the file
// would otherwise have the reader decode unrelated bytes as an entry. Every
// such cache must be rejected so the caller reparses the EPUB.
TEST(BookMetadataCacheTest, LoadRejectsLutSlotOutsideItsBlock) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);
  const auto full = readFile(bookBinPath(dir));
  ASSERT_GT(full.size(), 16u);
  const BookLayout layout = layoutOf(full);
  ASSERT_EQ(3, layout.spineCount);
  ASSERT_EQ(2, layout.tocCount);

  const uint32_t tocBlockStart = peekU32(full, layout.tocLutAt);
  ASSERT_GT(tocBlockStart, layout.entriesAt);

  const std::vector<std::pair<const char*, std::function<void(std::vector<uint8_t>&)>>> corruptions = {
      {"spine slot inside the header",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.spineLutAt + sizeof(uint32_t), 0); }},
      {"spine slot inside the LUT block",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.spineLutAt, layout.lutOffset); }},
      {"spine slot inside the TOC block",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.spineLutAt, tocBlockStart); }},
      {"spine slot past end of file",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.spineLutAt, static_cast<uint32_t>(b.size())); }},
      {"TOC slot inside the spine block",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.tocLutAt, layout.entriesAt); }},
      {"TOC slot past end of file",
       [&](std::vector<uint8_t>& b) { pokeU32(b, layout.tocLutAt, static_cast<uint32_t>(b.size()) + 1); }},
  };

  const std::string tdir = makeTempDir();
  for (const auto& [name, corrupt] : corruptions) {
    SCOPED_TRACE(name);
    std::vector<uint8_t> bytes = full;
    corrupt(bytes);
    writeFile(bookBinPath(tdir), bytes);

    BookMetadataCache cache(tdir);
    EXPECT_FALSE(cache.load()) << "load() must reject a LUT slot outside its block";
    EXPECT_FALSE(cache.isLoaded());
  }
}

/* ---------- Corruption: partially readable entries ---------- */

namespace {

// Same as buildValidCache but with an empty TOC pass, so book.bin has no TOC
// block and the spine block runs to the end of the file. That lets a test aim a
// spine LUT slot at bytes near EOF, the only place a read can run out midway.
void buildSpineOnlyCache(const std::string& dir) {
  BookMetadataCache cache(dir);
  EXPECT_TRUE(cache.beginWrite());
  EXPECT_TRUE(cache.beginContentOpfPass());
  cache.createSpineEntry("OEBPS/chapter1.xhtml");
  cache.createSpineEntry("OEBPS/chapter2.xhtml");
  cache.createSpineEntry("OEBPS/chapter3.xhtml");
  EXPECT_TRUE(cache.endContentOpfPass());
  EXPECT_TRUE(cache.beginTocPass());
  EXPECT_TRUE(cache.endTocPass());
  EXPECT_TRUE(cache.endWrite());

  BookMetadataCache::BookMetadata md;
  md.title = "Spine Only";
  EXPECT_TRUE(cache.buildBookBin(epubPath(), md));
}

}  // namespace

// Regression guard: the accessors honour every read. An entry that starts
// decoding and then runs out of file must not be served with the fields that
// happened to arrive first -- the reader would treat a truncated record as real
// content. Both accessors return a default-constructed entry instead.
TEST(BookMetadataCacheTest, SpineAccessorDoesNotServeAPartiallyReadEntry) {
  const std::string dir = makeTempDir();
  buildSpineOnlyCache(dir);
  auto bytes = readFile(bookBinPath(dir));
  ASSERT_GT(bytes.size(), 16u);
  const BookLayout layout = layoutOf(bytes);
  ASSERT_EQ(3, layout.spineCount);
  ASSERT_EQ(0, layout.tocCount);

  // Aim spine slot 2 nine bytes from the end and plant an href length of 3
  // there: the href reads back, the cumulative size behind it does not fit. The
  // planted u32 overwrites three href characters and one size byte of the real
  // last entry, which load()'s sequential pass reads just as happily.
  const auto slotAt = static_cast<uint32_t>(bytes.size() - 9);
  ASSERT_GT(slotAt, layout.entriesAt);
  pokeU32(bytes, slotAt, 3);
  pokeU32(bytes, layout.spineLutAt + 2 * sizeof(uint32_t), slotAt);
  writeFile(bookBinPath(dir), bytes);

  BookMetadataCache cache(dir);
  ASSERT_TRUE(cache.load());

  const auto entry = cache.getSpineEntry(2);
  EXPECT_EQ("", entry.href) << "a half-read spine entry must not be served";
  EXPECT_EQ(0u, entry.cumulativeSize);
  EXPECT_EQ(-1, entry.tocIndex);
}

TEST(BookMetadataCacheTest, TocAccessorDoesNotServeAPartiallyReadEntry) {
  const std::string dir = makeTempDir();
  buildValidCache(dir);
  auto bytes = readFile(bookBinPath(dir));
  ASSERT_GT(bytes.size(), 16u);
  const BookLayout layout = layoutOf(bytes);
  ASSERT_EQ(2, layout.tocCount);

  // Nine bytes of trailing slack -- which load() tolerates, see the
  // trailing-garbage test -- holding a 3-byte title and nothing more. TOC slot 0
  // is aimed at it, so the title reads back and the href runs off the end.
  const auto slackAt = static_cast<uint32_t>(bytes.size());
  bytes.resize(bytes.size() + sizeof(uint32_t));
  pokeU32(bytes, slackAt, 3);
  bytes.insert(bytes.end(), {'a', 'b', 'c', 0x00, 0x00});
  pokeU32(bytes, layout.tocLutAt, slackAt);
  writeFile(bookBinPath(dir), bytes);

  BookMetadataCache cache(dir);
  ASSERT_TRUE(cache.load());

  const auto entry = cache.getTocEntry(0);
  EXPECT_EQ("", entry.title) << "a half-read TOC entry must not be served";
  EXPECT_EQ("", entry.href);
  EXPECT_EQ(0, entry.level);
  EXPECT_EQ(-1, entry.spineIndex);
}
