#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#define class struct
#define private public
#include "Fb2.h"
#undef private
#undef class

#include "AllocCounter.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fileExists;
using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

template <typename T>
void appendPod(std::string& out, const T value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void appendString(std::string& out, const std::string& value) {
  appendPod(out, static_cast<uint32_t>(value.size()));
  out += value;
}

class Fb2BookTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(tmp.valid()); }

  fb2test::TempDir tmp;
};

TEST_F(Fb2BookTest, LoadParsesMetadataAndWritesBookBin) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(book.load());
  EXPECT_EQ(book.getTitle(), "The Crosspoint Chronicle");
  EXPECT_EQ(book.getAuthor(), "John Doe");
  EXPECT_EQ(book.getLanguage(), "en");
  EXPECT_EQ(book.getCoverBinaryId(), "cover.jpg");
  EXPECT_EQ(book.getSectionCount(), 2);
  EXPECT_TRUE(fileExists(book.getCachePath() + "/book.bin"));
}

TEST_F(Fb2BookTest, MetadataCacheRoundTripsWithoutReparsing) {
  Fb2 first(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(first.load());

  // buildIfMissing=false: this instance may only succeed via the cache file.
  Fb2 second(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(second.load(false));
  EXPECT_EQ(second.getTitle(), first.getTitle());
  EXPECT_EQ(second.getAuthor(), first.getAuthor());
  EXPECT_EQ(second.getLanguage(), first.getLanguage());
  EXPECT_EQ(second.getCoverBinaryId(), first.getCoverBinaryId());
  ASSERT_EQ(second.getSectionCount(), first.getSectionCount());
  for (int i = 0; i < first.getSectionCount(); i++) {
    EXPECT_EQ(second.getSectionInfo(i).title, first.getSectionInfo(i).title);
    EXPECT_EQ(second.getSectionInfo(i).fileOffset, first.getSectionInfo(i).fileOffset);
    EXPECT_EQ(second.getSectionInfo(i).length, first.getSectionInfo(i).length);
    EXPECT_EQ(second.getSectionInfo(i).level, first.getSectionInfo(i).level);
  }
  // One chapter per section: the TOC is a projection and both mappings are the
  // identity.
  ASSERT_EQ(second.getTocCount(), first.getTocCount());
  EXPECT_EQ(second.getTocCount(), second.getSectionCount());
  for (int i = 0; i < first.getTocCount(); i++) {
    EXPECT_EQ(second.getTocEntry(i).title, first.getTocEntry(i).title);
    EXPECT_EQ(second.getTocEntry(i).level, first.getTocEntry(i).level);
    EXPECT_EQ(second.getSectionIndexForTocIndex(i), i);
    EXPECT_EQ(second.getTocIndexForSectionIndex(i), i);
  }
}

// Nesting levels survive the cache round-trip, so the chapter list can indent
// from a cached book exactly as from a freshly parsed one.
TEST_F(Fb2BookTest, NestedChapterLevelsSurviveTheCacheRoundTrip) {
  Fb2 first(fixturePath("nested-deep.fb2"), tmp.path());
  ASSERT_TRUE(first.load());
  ASSERT_EQ(first.getSectionCount(), 4);

  Fb2 second(fixturePath("nested-deep.fb2"), tmp.path());
  ASSERT_TRUE(second.load(false));
  ASSERT_EQ(second.getSectionCount(), 4);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(second.getSectionInfo(i).level, static_cast<uint8_t>(i)) << "chapter " << i;
    EXPECT_EQ(second.getTocEntry(i).level, static_cast<uint8_t>(i)) << "chapter " << i;
  }
  // An untitled chapter keeps an empty title so the UI substitutes "Unnamed".
  Fb2 wrapper(fixturePath("wrapper-only.fb2"), tmp.path());
  ASSERT_TRUE(wrapper.load());
  ASSERT_EQ(wrapper.getSectionCount(), 3);
  EXPECT_EQ(wrapper.getTocEntry(1).title, "");
}

// A v2 cache (top-level-only chapter numbering, trailing TOC list) must be
// rejected and reparsed: read under v3 rules its chapter indices mean something
// different.
TEST_F(Fb2BookTest, LegacyVersionTwoCacheIsRejectedAndReparsed) {
  Fb2 book(fixturePath("nested-sections.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string legacy;
  legacy.push_back(2);  // the old version byte
  appendString(legacy, "Nesting Dolls");
  appendString(legacy, "");
  appendString(legacy, "en");
  appendString(legacy, "");
  appendPod<uint16_t>(legacy, 2);  // the old top-level-only chapter count
  appendString(legacy, "Part One The Beginning");
  appendPod<uint32_t>(legacy, 0);
  appendPod<uint32_t>(legacy, 100);
  appendString(legacy, "Part Two");
  appendPod<uint32_t>(legacy, 100);
  appendPod<uint32_t>(legacy, 100);
  appendPod<uint16_t>(legacy, 0);  // empty TOC list
  ASSERT_TRUE(writeAll(cacheFile, legacy));

  Fb2 cacheOnly(fixturePath("nested-sections.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  Fb2 rebuilt(fixturePath("nested-sections.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getSectionCount(), 3);  // every section, not just top level
  EXPECT_EQ(rebuilt.getSectionInfo(1).title, "Inner Chapter");
}

TEST_F(Fb2BookTest, MissingCacheWithBuildIfMissingFalseFails) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(book.load(false));
  EXPECT_EQ(book.getTitle(), "");  // unloaded book exposes blank metadata
}

TEST_F(Fb2BookTest, CacheVersionMismatchIsRejectedThenRebuilt) {
  Fb2 first(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(first.load());
  const std::string cacheFile = first.getCachePath() + "/book.bin";

  // Corrupt the version byte.
  std::string cache = readAll(cacheFile);
  ASSERT_FALSE(cache.empty());
  cache[0] = static_cast<char>(0x7F);
  ASSERT_TRUE(writeAll(cacheFile, cache));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getTitle(), "The Crosspoint Chronicle");
  // The cache file was rewritten with the current version byte.
  const std::string fresh = readAll(cacheFile);
  ASSERT_FALSE(fresh.empty());
  EXPECT_EQ(fresh[0], 3);
}

// A book.bin whose tail was replaced by zeros must be rejected (a valid
// cache always stores at least one section) and load() must fall back to
// reparsing the real book instead of trusting the corrupted data.
TEST_F(Fb2BookTest, CorruptedZeroFilledCacheIsRejectedAndReparsed) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string bogus;
  bogus.push_back(2);  // valid version byte
  const std::string title = "Zombie";
  const uint32_t len = static_cast<uint32_t>(title.size());
  bogus.append(reinterpret_cast<const char*>(&len), sizeof(len));
  bogus += title;
  bogus.append(200, '\0');  // author/lang/cover/sections/toc all read as zeros
  ASSERT_TRUE(writeAll(cacheFile, bogus));

  // Cache-only load fails; a full load reparses the real book.
  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  ASSERT_TRUE(book.load(true));
  EXPECT_EQ(book.getTitle(), "The Crosspoint Chronicle");
  EXPECT_EQ(book.getSectionCount(), 2);
}

TEST_F(Fb2BookTest, TruncatedCacheIsRejectedAndReparsed) {
  Fb2 first(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(first.load());
  const std::string cacheFile = first.getCachePath() + "/book.bin";

  // Cut the cache mid-stream (inside the section list).
  const std::string cache = readAll(cacheFile);
  ASSERT_GT(cache.size(), 20u);
  ASSERT_TRUE(writeAll(cacheFile, cache.substr(0, cache.size() / 2)));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getSectionCount(), 2);
}

TEST_F(Fb2BookTest, GarbageStringLengthInCacheIsRejectedNotAllocated) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  // Version byte followed by a ~4GB title length: must be rejected by the
  // bounded string reader, never resize()d into oblivion.
  std::string bogus;
  bogus.push_back(3);
  const uint32_t hugeLen = 0xFFFFFFF0u;
  bogus.append(reinterpret_cast<const char*>(&hugeLen), sizeof(hugeLen));
  bogus += "xx";
  ASSERT_TRUE(writeAll(cacheFile, bogus));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getTitle(), "The Crosspoint Chronicle");
}

// A corrupted section count (0xFFFF) in a tiny cache file must be rejected
// against the remaining file size BEFORE sections.reserve() runs: on the
// ~380KB-RAM device that reserve is a ~2.6MB up-front request, which aborts.
// The allocation cap pins that no such reserve happens on the host either.
TEST_F(Fb2BookTest, HugeSectionCountInTinyCacheIsRejectedWithoutReserving) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string bogus;
  bogus.push_back(3);  // valid version byte
  appendString(bogus, "Tiny");
  appendString(bogus, "Author");
  appendString(bogus, "en");
  appendString(bogus, "");
  appendPod<uint16_t>(bogus, 0xFFFF);  // claims 65535 sections...
  bogus += "xx";                       // ...backed by 2 bytes of data
  ASSERT_TRUE(writeAll(cacheFile, bogus));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  bool loaded = true;
  size_t bytesAllocated = 0;
  {
    alloc_counter::CountingScope scope;
    loaded = cacheOnly.load(false);
    bytesAllocated = scope.bytes();
  }
  EXPECT_FALSE(loaded);
  EXPECT_LT(bytesAllocated, 64u * 1024u) << "corrupt section count allocated " << bytesAllocated << " bytes";

  // The real book still loads via reparse.
  ASSERT_TRUE(book.load(true));
  EXPECT_EQ(book.getSectionCount(), 2);
}

// A chapter count above the cap must be rejected before sections.reserve():
// the cap is what bounds the RAM the chapter list can ever take.
TEST_F(Fb2BookTest, ChapterCountAboveTheCapIsRejectedWithoutReserving) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string bogus;
  bogus.push_back(3);  // valid version byte
  appendString(bogus, "Tiny");
  appendString(bogus, "Author");
  appendString(bogus, "en");
  appendString(bogus, "");
  appendPod<uint16_t>(bogus, Fb2::FB2_MAX_CHAPTERS + 1);  // one past the cap
  ASSERT_TRUE(writeAll(cacheFile, bogus));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  bool loaded = true;
  size_t bytesAllocated = 0;
  {
    alloc_counter::CountingScope scope;
    loaded = cacheOnly.load(false);
    bytesAllocated = scope.bytes();
  }
  EXPECT_FALSE(loaded);
  EXPECT_LT(bytesAllocated, 64u * 1024u) << "over-cap chapter count allocated " << bytesAllocated << " bytes";
}

// The level byte is the new tail field of each chapter record; a level that
// jumps more than one step cannot come from any real book, so it is corruption.
TEST_F(Fb2BookTest, ChapterLevelOutOfSequenceIsRejected) {
  Fb2 first(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(first.load());
  const std::string cacheFile = first.getCachePath() + "/book.bin";

  // The last byte of the file is the final chapter's level.
  std::string cache = readAll(cacheFile);
  ASSERT_FALSE(cache.empty());
  cache[cache.size() - 1] = 0x40;  // level 64 after a level-0 chapter
  ASSERT_TRUE(writeAll(cacheFile, cache));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  // ...and the book still loads by reparsing.
  Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getSectionCount(), 2);
}

// Contract C6 end to end: the chapter list is capped, and no text is lost -
// sections past the cap read as part of the chapter containing them.
TEST_F(Fb2BookTest, ChapterCountIsCappedWhenParsingAHugeBook) {
  const std::string path = tmp.path() + "/tower.fb2";
  ASSERT_TRUE(writeAll(path, fb2test::makeSectionTowerFb2(Fb2::FB2_MAX_CHAPTERS + 80, 2)));

  Fb2 book(path, tmp.path());
  ASSERT_TRUE(book.load());
  EXPECT_EQ(book.getSectionCount(), static_cast<int>(Fb2::FB2_MAX_CHAPTERS));
  EXPECT_EQ(book.getTocCount(), static_cast<int>(Fb2::FB2_MAX_CHAPTERS));
  // The capped tail still belongs to the last chapter's bytes, so the chapter
  // lengths keep covering the whole body.
  EXPECT_GT(book.getBookSize(), 0u);

  // ...and the cap survives the cache round-trip.
  Fb2 cached(path, tmp.path());
  ASSERT_TRUE(cached.load(false));
  EXPECT_EQ(cached.getSectionCount(), static_cast<int>(Fb2::FB2_MAX_CHAPTERS));
}

// FR-015. Chapter metadata is RAM-resident, so two things must hold: one heap
// allocation per stored chapter title (the parallel TOC list that used to hold a
// second copy of every title is gone), and a footprint that stops growing once
// the chapter cap is reached.
//
// Measured on macOS arm64 (clang, Release) 2026-09-20: 40 chapters with
// SBO-exceeding titles -> 57 allocations / 8,944 bytes; 1,104 sections capped at
// 1,024 chapters -> 148,328 bytes. The budgets below sit above those with room
// for allocator differences, and well under what a second copy of every title
// would cost.
TEST_F(Fb2BookTest, ChapterMetadataAllocatesOneTitlePerChapterAndStaysCapped) {
  constexpr int kChapters = 40;
  const std::string smallPath = tmp.path() + "/small-tower.fb2";
  ASSERT_TRUE(writeAll(smallPath, fb2test::makeSectionTowerFb2(kChapters, 2)));

  Fb2 small(smallPath, tmp.path());
  size_t smallCount = 0;
  size_t smallBytes = 0;
  {
    alloc_counter::CountingScope scope;
    small.load();
    smallCount = scope.count();
    smallBytes = scope.bytes();
  }
  ASSERT_EQ(small.getSectionCount(), kChapters);
  // Storing each title twice would put the count at or above 2 per chapter.
  EXPECT_LT(smallCount, static_cast<size_t>(2 * kChapters))
      << "chapter metadata allocated " << smallCount << " blocks for " << kChapters << " chapters";
  EXPECT_LT(smallBytes, 24u * 1024u) << "chapter metadata allocated " << smallBytes << " bytes";

  const std::string bigPath = tmp.path() + "/big-tower.fb2";
  ASSERT_TRUE(writeAll(bigPath, fb2test::makeSectionTowerFb2(Fb2::FB2_MAX_CHAPTERS + 80, 2)));
  Fb2 big(bigPath, tmp.path());
  size_t bigBytes = 0;
  {
    alloc_counter::CountingScope scope;
    big.load();
    bigBytes = scope.bytes();
  }
  ASSERT_EQ(big.getSectionCount(), static_cast<int>(Fb2::FB2_MAX_CHAPTERS));
  // 80 extra sections past the cap must not add per-section metadata.
  // Measured: 8,944 bytes / 57 blocks at 40 chapters, 148,328 bytes at the
  // 1024 cap. The cap is the RAM ceiling for a book's chapter metadata, and on
  // the ~380KB C3 it is most of the budget — the reference book's 66 chapters
  // cost ~15KB.
  EXPECT_LT(bigBytes, 320u * 1024u) << "capped chapter metadata allocated " << bigBytes << " bytes";
}

// Contract C7 end to end: chapter lengths partition the body, so progress rises
// monotonically from 0% to 100% and never overshoots.
TEST_F(Fb2BookTest, ChapterLengthsPartitionTheBodySoProgressIsMonotonic) {
  for (const char* fixture : {"nested-deep.fb2", "trailing-parent-text.fb2", "nested-sections.fb2", "basic.fb2"}) {
    Fb2 book(fixturePath(fixture), tmp.path());
    ASSERT_TRUE(book.load()) << fixture;
    const int count = book.getSectionCount();
    ASSERT_GT(count, 0) << fixture;

    size_t sum = 0;
    for (int i = 0; i < count; i++) {
      sum += book.getSectionInfo(i).length;
    }
    EXPECT_EQ(sum, book.getBookSize()) << fixture;

    float previous = -1.0f;
    for (int i = 0; i < count; i++) {
      const float atStart = book.calculateProgress(i, 0.0f);
      const float atEnd = book.calculateProgress(i, 1.0f);
      EXPECT_GE(atStart, previous) << fixture << " chapter " << i;
      EXPECT_GE(atEnd, atStart) << fixture << " chapter " << i;
      EXPECT_LE(atEnd, 1.0f) << fixture << " chapter " << i;
      previous = atEnd;
    }
    EXPECT_FLOAT_EQ(book.calculateProgress(0, 0.0f), 0.0f) << fixture;
    EXPECT_FLOAT_EQ(book.calculateProgress(count - 1, 1.0f), 1.0f) << fixture;
  }
}

TEST_F(Fb2BookTest, ClearCacheRemovesTheCacheDirectory) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(book.load());
  ASSERT_TRUE(fileExists(book.getCachePath()));
  EXPECT_TRUE(book.clearCache());
  EXPECT_FALSE(fileExists(book.getCachePath()));
  // Clearing an already-missing cache also reports success.
  EXPECT_TRUE(book.clearCache());
}

TEST_F(Fb2BookTest, DifferentBookPathsGetDifferentCacheDirs) {
  Fb2 a(fixturePath("basic.fb2"), tmp.path());
  Fb2 b(fixturePath("no-cover.fb2"), tmp.path());
  EXPECT_NE(a.getCachePath(), b.getCachePath());
}

TEST_F(Fb2BookTest, GenerateCoverBmpDecodesTheDeclaredBinary) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(book.load());
  ASSERT_TRUE(book.generateCoverBmp());
  EXPECT_EQ(readAll(book.getCoverBmpPath()), "STUBJPEG:basic-cover-payload-0123456789");
}

TEST_F(Fb2BookTest, GenerateThumbWithoutCoverWritesEmptyMarkerAndFails) {
  Fb2 book(fixturePath("no-cover.fb2"), tmp.path());
  ASSERT_TRUE(book.load());
  EXPECT_FALSE(book.generateThumbBmp(120));
  // An empty marker file is left so future attempts are skipped...
  EXPECT_TRUE(fileExists(book.getThumbBmpPath(120)));
  EXPECT_EQ(readAll(book.getThumbBmpPath(120)).size(), 0u);
  // ...and the next call short-circuits to success because the file exists.
  EXPECT_TRUE(book.generateThumbBmp(120));
}

// ---------------------------------------------------------------------------
// Progress / TOC math on a book with hand-set sections (private access).
// ---------------------------------------------------------------------------

class Fb2MathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    book.sections = {{"One", 0, 100, 0}, {"Two", 100, 300, 1}, {"Three", 400, 600, 0}};
    book.loaded = true;
  }

  Fb2 book{"unused.fb2", "/tmp"};
};

TEST_F(Fb2MathTest, CumulativeSizesSumSectionLengths) {
  EXPECT_EQ(book.getCumulativeSectionSize(0), 100u);
  EXPECT_EQ(book.getCumulativeSectionSize(1), 400u);
  EXPECT_EQ(book.getCumulativeSectionSize(2), 1000u);
  EXPECT_EQ(book.getBookSize(), 1000u);
  EXPECT_EQ(book.getCumulativeSectionSize(-1), 0u);
  EXPECT_EQ(book.getCumulativeSectionSize(3), 0u);
}

TEST_F(Fb2MathTest, ProgressWeightsSectionsByLength) {
  EXPECT_FLOAT_EQ(book.calculateProgress(0, 0.0f), 0.0f);
  // Halfway through section 1 (300 bytes): (100 + 150) / 1000.
  EXPECT_FLOAT_EQ(book.calculateProgress(1, 0.5f), 0.25f);
  EXPECT_FLOAT_EQ(book.calculateProgress(2, 1.0f), 1.0f);
}

TEST_F(Fb2MathTest, ProgressOnEmptyBookIsZero) {
  book.sections.clear();
  EXPECT_FLOAT_EQ(book.calculateProgress(0, 0.5f), 0.0f);
  EXPECT_EQ(book.getBookSize(), 0u);
}

TEST_F(Fb2MathTest, TocIndexForSectionIsTheIdentity) {
  EXPECT_EQ(book.getTocIndexForSectionIndex(0), 0);
  EXPECT_EQ(book.getTocIndexForSectionIndex(1), 1);
  EXPECT_EQ(book.getTocIndexForSectionIndex(2), 2);
  EXPECT_EQ(book.getTocIndexForSectionIndex(-1), -1);
  EXPECT_EQ(book.getTocIndexForSectionIndex(3), -1);
}

TEST_F(Fb2MathTest, SectionIndexForTocIndexClampsOutOfRangeToZero) {
  EXPECT_EQ(book.getSectionIndexForTocIndex(0), 0);
  EXPECT_EQ(book.getSectionIndexForTocIndex(2), 2);
  EXPECT_EQ(book.getSectionIndexForTocIndex(-1), 0);
  EXPECT_EQ(book.getSectionIndexForTocIndex(99), 0);
}

// Resolves a position saved under the old top-level-only numbering: ordinal N
// means "the (N+1)-th level-0 chapter".
TEST_F(Fb2MathTest, FirstChapterOfTopLevelSkipsNestedChapters) {
  EXPECT_EQ(book.firstChapterOfTopLevel(0), 0);
  EXPECT_EQ(book.firstChapterOfTopLevel(1), 2);  // "Two" is nested, so it is skipped
  EXPECT_EQ(book.firstChapterOfTopLevel(7), 2);  // past the end clamps to the last chapter
}

TEST_F(Fb2MathTest, FirstChapterOfTopLevelIsTheIdentityForAFlatBook) {
  book.sections = {{"One", 0, 100, 0}, {"Two", 100, 100, 0}, {"Three", 200, 100, 0}};
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(book.firstChapterOfTopLevel(i), i) << "ordinal " << i;
  }
  book.sections.clear();
  EXPECT_EQ(book.firstChapterOfTopLevel(2), 0);
}

TEST_F(Fb2MathTest, OutOfRangeSectionInfoIsEmpty) {
  EXPECT_EQ(book.getSectionInfo(-1).title, "");
  EXPECT_EQ(book.getSectionInfo(-1).length, 0u);
  EXPECT_EQ(book.getSectionInfo(3).title, "");
  EXPECT_EQ(book.getSectionInfo(1).title, "Two");
}

}  // namespace
