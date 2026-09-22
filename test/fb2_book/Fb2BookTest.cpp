#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#define class struct
#define private public
#include "Fb2.h"
#undef private
#undef class

#include <HalStorage.h>

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
  EXPECT_EQ(fresh[0], 5);
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
  bogus.push_back(5);
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

// A chapter count the file cannot hold must be rejected by the size equation
// BEFORE any record is read, and must drive no allocation.
TEST_F(Fb2BookTest, HugeSectionCountInTinyCacheIsRejectedWithoutReserving) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string bogus;
  bogus.push_back(5);  // valid version byte
  appendString(bogus, "Tiny");
  appendString(bogus, "Author");
  appendString(bogus, "en");
  appendString(bogus, "");
  appendPod<uint16_t>(bogus, Fb2::FB2_CHAPTER_INDEX_LIMIT);  // claims the most chapters allowed...
  appendPod<uint32_t>(bogus, 0u);                            // ...no titles...
  bogus += "xx";                                             // ...backed by 2 bytes of records
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

// A chapter count above the cap cannot come from any build, so it is corruption.
TEST_F(Fb2BookTest, ChapterCountAboveTheCapIsRejectedWithoutReserving) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string bogus;
  bogus.push_back(5);  // valid version byte
  appendString(bogus, "Tiny");
  appendString(bogus, "Author");
  appendString(bogus, "en");
  appendString(bogus, "");
  appendPod<uint16_t>(bogus, Fb2::FB2_CHAPTER_INDEX_LIMIT + 1);  // one past the cap
  appendPod<uint32_t>(bogus, 0u);
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

// FR-001: every section is a chapter, far past the old 256 cap, and the chapter
// lengths still partition the body exactly (checked against the source itself).
TEST_F(Fb2BookTest, EveryChapterOfAHugeBookIsKept) {
  const std::string path = tmp.path() + "/tower.fb2";
  const std::string source = fb2test::makeSectionTowerFb2(4000, 2);
  ASSERT_TRUE(writeAll(path, source));

  Fb2 book(path, tmp.path());
  ASSERT_TRUE(book.load());
  EXPECT_EQ(book.getSectionCount(), 4000);
  EXPECT_EQ(book.getTocCount(), 4000);
  EXPECT_EQ(book.getSectionInfo(3999).title, "Section number 3999 of the tower");
  size_t partitioned = 0;
  for (int i = 0; i < book.getSectionCount(); i++) {
    partitioned += book.getSectionInfo(i).length;
  }
  EXPECT_EQ(partitioned, fb2test::topLevelSectionBytes(source)) << "chapter lengths do not partition the body";

  // ...and every chapter survives the cache round-trip.
  Fb2 cached(path, tmp.path());
  ASSERT_TRUE(cached.load(false));
  EXPECT_EQ(cached.getSectionCount(), 4000);
  EXPECT_EQ(cached.getSectionInfo(3999).title, "Section number 3999 of the tower");
}

// FR-003 / SC-002. Chapter metadata lives in book.bin, so what an open book holds
// in RAM, and the peak while it is first indexed, must not depend on how many
// chapters it has. The two books have the same (flat) nesting and titles short
// enough for the small-string buffer, so the allowance is exactly zero.
TEST_F(Fb2BookTest, ChapterMemoryIsIndependentOfChapterCount) {
  struct Measured {
    size_t held;
    size_t peak;
    int chapters;
  };
  // Both books use the same path (cache cleared in between), so every path-derived
  // string (the book path, the cache dir with its hash) is byte-for-byte the same.
  // libstdc++ allocates strings at their exact length, so even a 3-character name
  // difference showed up in the peak on Linux CI.
  const std::string path = tmp.path() + "/flat.fb2";
  auto measure = [&](const int chapters) {
    Fb2(path, tmp.path()).clearCache();
    EXPECT_TRUE(writeAll(path, fb2test::makeShortTitleTowerFb2(chapters)));
    auto book = std::make_unique<Fb2>(path, tmp.path());
    size_t peak = 0;
    size_t held = 0;
    {
      alloc_counter::CountingScope scope;
      book->load();
      peak = alloc_counter::peakBytes();
      held = alloc_counter::liveBytes();
    }
    return Measured{held, peak, book->getSectionCount()};
  };
  const Measured small = measure(4);
  const Measured big = measure(4000);
  ASSERT_EQ(small.chapters, 4);
  ASSERT_EQ(big.chapters, 4000);
  EXPECT_EQ(big.held, small.held) << "an open book holds " << (big.held - small.held)
                                  << " more bytes for 3,996 more chapters";
  EXPECT_EQ(big.peak, small.peak) << "first indexing peaks " << (big.peak - small.peak)
                                  << " bytes higher for 3,996 more chapters";
}

// book.bin carries the derived-label marker, so a label survives the round trip
// and a book shows the same chapter list on its first and second open.
TEST_F(Fb2BookTest, DerivedLabelAndItsFlagSurviveTheCacheRoundTrip) {
  const std::string path = tmp.path() + "/labels.fb2";
  ASSERT_TRUE(writeAll(path, fb2test::makeUntitledSectionsFb2(4, 1)));

  Fb2 built(path, tmp.path());
  ASSERT_TRUE(built.load(true));
  ASSERT_EQ(built.getSectionCount(), 4);
  ASSERT_EQ(built.getSectionInfo(2).title, "Body of section 2");
  ASSERT_EQ(built.getSectionInfo(2).titleDerived, 1);

  // Second open reads the cache written by the first.
  Fb2 cached(path, tmp.path());
  ASSERT_TRUE(cached.load(false));
  EXPECT_EQ(cached.getSectionInfo(2).title, "Body of section 2");
  EXPECT_EQ(cached.getSectionInfo(2).titleDerived, 1);
  EXPECT_EQ(cached.getSectionInfo(0).titleDerived, 1);
  EXPECT_TRUE(cached.getSectionInfo(1).title.empty());
  EXPECT_EQ(cached.getSectionInfo(1).titleDerived, 0);
}

// A version-3 cache predates the flags byte; reading it as v4 would shift every
// field after the first chapter's level. It must be rejected, not reinterpreted.
TEST_F(Fb2BookTest, VersionThreeCacheIsRejectedAndRebuilt) {
  Fb2 book(fixturePath("basic.fb2"), tmp.path());
  book.setupCacheDir();
  const std::string cacheFile = book.getCachePath() + "/book.bin";

  std::string v3;
  v3.push_back(3);
  appendString(v3, "The Crosspoint Chronicle");
  appendString(v3, "John Doe");
  appendString(v3, "en");
  appendString(v3, "");
  appendPod<uint16_t>(v3, 1);
  appendString(v3, "Chapter One");
  appendPod<uint32_t>(v3, 100u);
  appendPod<uint32_t>(v3, 200u);
  appendPod<uint8_t>(v3, 0u);  // level, and then nothing: v3 has no flags byte
  ASSERT_TRUE(writeAll(cacheFile, v3));

  Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
  EXPECT_FALSE(cacheOnly.load(false));

  Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
  ASSERT_TRUE(rebuilt.load(true));
  EXPECT_EQ(rebuilt.getSectionCount(), 2);
}

// Contract C7 end to end: chapter lengths partition the body, so progress rises
// monotonically from 0% to 100% and never overshoots.
TEST_F(Fb2BookTest, ChapterLengthsPartitionTheBodySoProgressIsMonotonic) {
  for (const char* fixture : {"nested-deep.fb2", "trailing-parent-text.fb2", "nested-sections.fb2", "basic.fb2"}) {
    Fb2 book(fixturePath(fixture), tmp.path());
    ASSERT_TRUE(book.load()) << fixture;
    const int count = book.getSectionCount();
    ASSERT_GT(count, 0) << fixture;

    // Against the source's own section spans: getBookSize() is itself the sum of
    // the lengths, so comparing with it could never fail.
    size_t sum = 0;
    for (int i = 0; i < count; i++) {
      sum += book.getSectionInfo(i).length;
    }
    EXPECT_EQ(sum, fb2test::topLevelSectionBytes(readAll(fixturePath(fixture)))) << fixture;
    EXPECT_EQ(sum, book.getBookSize()) << fixture;

    float previous = -1.0f;
    for (int i = 0; i < count; i++) {
      const auto chapter = book.getSectionInfo(i);
      const float atStart = book.calculateProgress(chapter, 0.0f);
      const float atEnd = book.calculateProgress(chapter, 1.0f);
      EXPECT_GE(atStart, previous) << fixture << " chapter " << i;
      EXPECT_GE(atEnd, atStart) << fixture << " chapter " << i;
      EXPECT_LE(atEnd, 1.0f) << fixture << " chapter " << i;
      previous = atEnd;
    }
    EXPECT_FLOAT_EQ(book.calculateProgress(book.getSectionInfo(0), 0.0f), 0.0f) << fixture;
    EXPECT_FLOAT_EQ(book.calculateProgress(book.getSectionInfo(count - 1), 1.0f), 1.0f) << fixture;
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

// book.bin v5 round trip: a reopened book reads every chapter field back exactly,
// the lengths partition the source, and a build leaves no temp files behind.
TEST_F(Fb2BookTest, V5CacheRoundTripsEveryChapterField) {
  for (const char* fixture : {"nested-deep.fb2", "nested-sections.fb2", "trailing-parent-text.fb2"}) {
    Fb2 built(fixturePath(fixture), tmp.path());
    ASSERT_TRUE(built.load(true)) << fixture;
    EXPECT_FALSE(fileExists(built.getCachePath() + "/chapters.tmp")) << fixture;
    EXPECT_FALSE(fileExists(built.getCachePath() + "/titles.tmp")) << fixture;

    Fb2 cached(fixturePath(fixture), tmp.path());
    ASSERT_TRUE(cached.load(false)) << fixture;
    ASSERT_EQ(cached.getSectionCount(), built.getSectionCount()) << fixture;
    size_t sum = 0;
    for (int i = 0; i < cached.getSectionCount(); i++) {
      const auto a = built.getSectionInfo(i);
      const auto b = cached.getSectionInfo(i);
      EXPECT_EQ(b.title, a.title) << fixture << " chapter " << i;
      EXPECT_EQ(b.fileOffset, a.fileOffset) << fixture << " chapter " << i;
      EXPECT_EQ(b.length, a.length) << fixture << " chapter " << i;
      EXPECT_EQ(b.level, a.level) << fixture << " chapter " << i;
      EXPECT_EQ(b.titleDerived, a.titleDerived) << fixture << " chapter " << i;
      sum += b.length;
      EXPECT_EQ(b.cumulativeLength, sum) << fixture << " chapter " << i;
    }
    // The independent check: the lengths add up to the source's own section spans.
    EXPECT_EQ(sum, fb2test::topLevelSectionBytes(readAll(fixturePath(fixture)))) << fixture;
    EXPECT_EQ(cached.getBookSize(), sum) << fixture;
  }
}

// FR-011 / Constitution VI: every field of a v5 book.bin is untrusted. Each case
// corrupts exactly one field of an otherwise valid file; each must be rejected so
// load() re-parses the source and writes a valid cache in its place.
TEST_F(Fb2BookTest, MalformedV5CachesAreRejectedAndRebuilt) {
  using Bin = fb2test::V5BookBin;
  const Bin good = Bin::from({{"One", 0, 100, 0, false}, {"Two", 100, 50, 1, true}, {"Three", 150, 25, 0, false}});

  struct Case {
    const char* name;
    std::string bytes;
  };
  std::vector<Case> cases;
  auto with = [&](const char* name, auto mutate) {
    Bin bin = good;
    mutate(bin);
    cases.push_back({name, bin.encode()});
  };
  {
    std::string cut = good.encode();
    cut.pop_back();
    cases.push_back({"title area truncated by one byte", cut});
  }
  with("titlesSize one too large", [](Bin& b) { b.titlesSize += 1; });
  with("titlesSize one too small", [](Bin& b) { b.titlesSize -= 1; });
  with("title past the title area", [](Bin& b) { b.records[2].titleOffset = b.titlesSize; });
  with("title length above the string cap", [](Bin& b) {
    b.records[0].titleLength = 4097;
    b.titles.insert(0, 4097 - 3, 'x');
    b.titlesSize = static_cast<uint32_t>(b.titles.size());
    b.records[0].titleOffset = 0;
  });
  with("wrong running total", [](Bin& b) { b.records[1].cumulativeLength += 1; });
  with("running total overflows u32", [](Bin& b) {
    b.records[1].ownLength = 0xFFFFFFF0u;
    b.records[1].cumulativeLength = b.records[0].cumulativeLength + 0xFFFFFFF0u;  // wraps
  });
  with("level jumps two steps", [](Bin& b) { b.records[1].level = 2; });
  with("first chapter is nested", [](Bin& b) { b.records[0].level = 1; });
  with("undefined flag bit", [](Bin& b) { b.records[0].flags = 0x02; });
  with("derived flag on an empty title", [](Bin& b) {
    b.titles = "OneThree";
    b.titlesSize = 8;
    b.records[1].titleLength = 0;
    b.records[1].titleOffset = 3;
    b.records[2].titleOffset = 3;
  });
  with("zero chapters", [](Bin& b) {
    b.chapterCount = 0;
    b.records.clear();
    b.titles.clear();
    b.titlesSize = 0;
  });
  with("count one above the cap", [](Bin& b) { b.chapterCount = Fb2::FB2_CHAPTER_INDEX_LIMIT + 1; });
  with("count at the cap in a tiny file", [](Bin& b) { b.chapterCount = Fb2::FB2_CHAPTER_INDEX_LIMIT; });
  with("header string above the cap", [](Bin& b) { b.author.assign(4097, 'a'); });

  // The baseline really is valid, so each rejection below is the mutation's doing.
  Fb2 probe(fixturePath("basic.fb2"), tmp.path());
  probe.setupCacheDir();
  const std::string cacheFile = probe.getCachePath() + "/book.bin";
  ASSERT_TRUE(writeAll(cacheFile, good.encode()));
  ASSERT_TRUE(Fb2(fixturePath("basic.fb2"), tmp.path()).load(false)) << "baseline must load";

  for (const auto& c : cases) {
    ASSERT_TRUE(writeAll(cacheFile, c.bytes)) << c.name;
    Fb2 cacheOnly(fixturePath("basic.fb2"), tmp.path());
    EXPECT_FALSE(cacheOnly.load(false)) << c.name;

    Fb2 rebuilt(fixturePath("basic.fb2"), tmp.path());
    ASSERT_TRUE(rebuilt.load(true)) << c.name;
    EXPECT_EQ(rebuilt.getSectionCount(), 2) << c.name;
    EXPECT_EQ(rebuilt.getTitle(), "The Crosspoint Chronicle") << c.name;
  }
}

// Edge case "low SD space": a build that cannot write must fail the open and
// leave nothing behind that a later open could mistake for a cache.
TEST_F(Fb2BookTest, FailedBuildLeavesNoCacheOrTempFiles) {
  // Budgets that run out during the parse (temp files) and during assembly.
  for (const size_t budget : {size_t{10}, size_t{300}}) {
    Fb2 book(fixturePath("nested-deep.fb2"), tmp.path());
    book.setupCacheDir();
    Storage.failWritesAfter(budget);
    const bool loaded = book.load(true);
    Storage.resetOpenCounts();
    EXPECT_FALSE(loaded) << "budget " << budget;
    EXPECT_FALSE(fileExists(book.getCachePath() + "/book.bin")) << "budget " << budget;
    EXPECT_FALSE(fileExists(book.getCachePath() + "/chapters.tmp")) << "budget " << budget;
    EXPECT_FALSE(fileExists(book.getCachePath() + "/titles.tmp")) << "budget " << budget;
  }
  // With the card writable again, the same book opens normally.
  Fb2 book(fixturePath("nested-deep.fb2"), tmp.path());
  EXPECT_TRUE(book.load(true));
}

// US3 / FR-010. Layouts built under the old 256-chapter cap have stale chapter
// boundaries only when the book has more than 256 chapters, so the upgrade from a
// pre-v5 cache drops sections/ for exactly those books and keeps every other
// book's layouts. A corrupt v5 cache was built under the same numbering, so it
// never costs the reader their layouts.
TEST_F(Fb2BookTest, UpgradeDropsLayoutsOnlyForBooksOverTheOldCap) {
  struct Case {
    const char* name;
    int chapters;
    uint8_t oldVersion;
    bool layoutsSurvive;
  };
  for (const Case& c :
       {Case{"v4 cache, 200 chapters", 200, 4, true}, Case{"v4 cache, 300 chapters", 300, 4, false},
        Case{"v3 cache, 300 chapters", 300, 3, false}, Case{"corrupt v5 cache, 300 chapters", 300, 5, true}}) {
    const std::string path =
        tmp.path() + "/upgrade-" + std::to_string(c.chapters) + "-" + std::to_string(c.oldVersion) + ".fb2";
    ASSERT_TRUE(writeAll(path, fb2test::makeSectionTowerFb2(c.chapters, 2))) << c.name;
    Fb2 book(path, tmp.path());
    book.setupCacheDir();
    // An old-format cache: only the version byte matters, the rest is never read.
    std::string old;
    old.push_back(static_cast<char>(c.oldVersion));
    appendString(old, "Old");
    ASSERT_TRUE(writeAll(book.getCachePath() + "/book.bin", old)) << c.name;
    Storage.mkdir((book.getCachePath() + "/sections").c_str());
    const std::string layout = book.getCachePath() + "/sections/0.bin";
    ASSERT_TRUE(writeAll(layout, "layout")) << c.name;

    ASSERT_TRUE(book.load(true)) << c.name;
    EXPECT_EQ(book.getSectionCount(), c.chapters) << c.name;
    EXPECT_EQ(fileExists(layout), c.layoutsSurvive) << c.name;
  }
}

// SC-004: once the reader holds the current chapter's record, progress and book
// size are pure arithmetic, so a page turn opens nothing on the card.
TEST_F(Fb2BookTest, ProgressFromAHeldChapterDoesNoIo) {
  Fb2 book(fixturePath("nested-deep.fb2"), tmp.path());
  ASSERT_TRUE(book.load());
  const auto chapter = book.getSectionInfo(2);
  Storage.resetOpenCounts();
  float sum = 0.0f;
  for (int page = 0; page <= 10; page++) {
    sum += book.calculateProgress(chapter, static_cast<float>(page) / 10.0f);
  }
  const size_t size = book.getBookSize();
  EXPECT_EQ(Storage.openForReadCount(), 0u);
  EXPECT_GT(sum, 0.0f);
  EXPECT_GT(size, 0u);
}

// The chapter list reads a window of chapters in one batch; it must return exactly
// what one-at-a-time lookups return, clamp at the end of the book, and cap at
// TOC_BATCH.
TEST_F(Fb2BookTest, BatchedTocReadMatchesSingleLookups) {
  const std::string path = tmp.path() + "/tower.fb2";
  ASSERT_TRUE(writeAll(path, fb2test::makeUntitledSectionsFb2(60, 3)));  // derived, empty and nested titles
  Fb2 book(path, tmp.path());
  ASSERT_TRUE(book.load());
  ASSERT_EQ(book.getSectionCount(), 60);
  HalFile bookBin;
  ASSERT_TRUE(book.openIndex(bookBin));
  std::vector<Fb2::SectionInfo> window(Fb2::TOC_BATCH);
  for (const int first : {0, 1, 24, 50}) {
    const int read = book.getTocEntries(first, Fb2::TOC_BATCH, window.data(), bookBin);
    EXPECT_EQ(read, std::min(Fb2::TOC_BATCH, 60 - first)) << "first " << first;
    for (int i = 0; i < read; i++) {
      const auto single = book.getSectionInfo(first + i);
      EXPECT_EQ(window[i].title, single.title) << "chapter " << first + i;
      EXPECT_EQ(window[i].level, single.level) << "chapter " << first + i;
      EXPECT_EQ(window[i].titleDerived, single.titleDerived) << "chapter " << first + i;
      EXPECT_EQ(window[i].length, single.length) << "chapter " << first + i;
      EXPECT_EQ(window[i].cumulativeLength, single.cumulativeLength) << "chapter " << first + i;
    }
  }
  EXPECT_EQ(book.getTocEntries(60, Fb2::TOC_BATCH, window.data(), bookBin), 0);
  EXPECT_EQ(book.getTocEntries(-1, Fb2::TOC_BATCH, window.data(), bookBin), 0);
  EXPECT_EQ(book.getTocEntries(0, 1000, window.data(), bookBin), Fb2::TOC_BATCH);
}

// ---------------------------------------------------------------------------
// Progress / TOC math on a book loaded from a hand-built book.bin.
// ---------------------------------------------------------------------------

class Fb2MathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    loadChapters({{"One", 0, 100, 0, false}, {"Two", 100, 300, 1, false}, {"Three", 400, 600, 0, false}});
  }

  // Replaces the book with one whose cache holds exactly these chapters.
  void loadChapters(const std::vector<fb2test::V5BookBin::Chapter>& chapters) {
    book = std::make_unique<Fb2>(tmp.path() + "/unused.fb2", tmp.path());
    book->setupCacheDir();
    ASSERT_TRUE(writeAll(book->getCachePath() + "/book.bin", fb2test::V5BookBin::from(chapters).encode()));
    ASSERT_TRUE(book->load(false));
  }

  fb2test::TempDir tmp;
  std::unique_ptr<Fb2> book;
};

TEST_F(Fb2MathTest, CumulativeSizesSumSectionLengths) {
  EXPECT_EQ(book->getCumulativeSectionSize(0), 100u);
  EXPECT_EQ(book->getCumulativeSectionSize(1), 400u);
  EXPECT_EQ(book->getCumulativeSectionSize(2), 1000u);
  EXPECT_EQ(book->getBookSize(), 1000u);
  EXPECT_EQ(book->getCumulativeSectionSize(-1), 0u);
  EXPECT_EQ(book->getCumulativeSectionSize(3), 0u);
}

TEST_F(Fb2MathTest, ProgressWeightsSectionsByLength) {
  EXPECT_FLOAT_EQ(book->calculateProgress(book->getSectionInfo(0), 0.0f), 0.0f);
  // Halfway through section 1 (300 bytes): (100 + 150) / 1000.
  EXPECT_FLOAT_EQ(book->calculateProgress(book->getSectionInfo(1), 0.5f), 0.25f);
  EXPECT_FLOAT_EQ(book->calculateProgress(book->getSectionInfo(2), 1.0f), 1.0f);
}

TEST_F(Fb2MathTest, ProgressOnUnloadedBookIsZero) {
  Fb2 unloaded(tmp.path() + "/never-loaded.fb2", tmp.path());
  EXPECT_FLOAT_EQ(unloaded.calculateProgress(book->getSectionInfo(1), 0.5f), 0.0f);
  EXPECT_EQ(unloaded.getBookSize(), 0u);
  EXPECT_EQ(unloaded.getSectionCount(), 0);
}

TEST_F(Fb2MathTest, TocIndexForSectionIsTheIdentity) {
  EXPECT_EQ(book->getTocIndexForSectionIndex(0), 0);
  EXPECT_EQ(book->getTocIndexForSectionIndex(1), 1);
  EXPECT_EQ(book->getTocIndexForSectionIndex(2), 2);
  EXPECT_EQ(book->getTocIndexForSectionIndex(-1), -1);
  EXPECT_EQ(book->getTocIndexForSectionIndex(3), -1);
}

TEST_F(Fb2MathTest, SectionIndexForTocIndexClampsOutOfRangeToZero) {
  EXPECT_EQ(book->getSectionIndexForTocIndex(0), 0);
  EXPECT_EQ(book->getSectionIndexForTocIndex(2), 2);
  EXPECT_EQ(book->getSectionIndexForTocIndex(-1), 0);
  EXPECT_EQ(book->getSectionIndexForTocIndex(99), 0);
}

// Resolves a position saved under the old top-level-only numbering: ordinal N
// means "the (N+1)-th level-0 chapter".
TEST_F(Fb2MathTest, FirstChapterOfTopLevelSkipsNestedChapters) {
  EXPECT_EQ(book->firstChapterOfTopLevel(0), 0);
  EXPECT_EQ(book->firstChapterOfTopLevel(1), 2);  // "Two" is nested, so it is skipped
  EXPECT_EQ(book->firstChapterOfTopLevel(7), 2);  // past the end clamps to the last chapter
}

TEST_F(Fb2MathTest, FirstChapterOfTopLevelIsTheIdentityForAFlatBook) {
  loadChapters({{"One", 0, 100, 0, false}, {"Two", 100, 100, 0, false}, {"Three", 200, 100, 0, false}});
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(book->firstChapterOfTopLevel(i), i) << "ordinal " << i;
  }
  Fb2 unloaded(tmp.path() + "/never-loaded.fb2", tmp.path());
  EXPECT_EQ(unloaded.firstChapterOfTopLevel(2), 0);
}

TEST_F(Fb2MathTest, OutOfRangeSectionInfoIsEmpty) {
  EXPECT_EQ(book->getSectionInfo(-1).title, "");
  EXPECT_EQ(book->getSectionInfo(-1).length, 0u);
  EXPECT_EQ(book->getSectionInfo(3).title, "");
  EXPECT_EQ(book->getSectionInfo(1).title, "Two");
}

// FR-012: a lookup that cannot be served is an empty chapter (shown as
// "Unnamed"), never a crash or a stale value.
TEST_F(Fb2MathTest, LookupWithoutItsBookBinIsEmpty) {
  ASSERT_EQ(book->getSectionInfo(1).title, "Two");
  std::remove((book->getCachePath() + "/book.bin").c_str());
  const auto missing = book->getSectionInfo(1);
  EXPECT_EQ(missing.title, "");
  EXPECT_EQ(missing.length, 0u);
  EXPECT_EQ(book->getCumulativeSectionSize(1), 0u);
}

// A book.bin that shrank after load (card fault, external edit) degrades each
// lookup to an empty chapter instead of reading past the end.
TEST_F(Fb2MathTest, LookupIntoATruncatedBookBinIsEmpty) {
  const std::string bin = book->getCachePath() + "/book.bin";
  const std::string whole = readAll(bin);
  ASSERT_TRUE(writeAll(bin, whole.substr(0, whole.size() - 4)));  // cut into "Three"
  EXPECT_EQ(book->getSectionInfo(2).title, "");
  EXPECT_EQ(book->getSectionInfo(0).title, "One");  // intact chapters still read
}

}  // namespace
