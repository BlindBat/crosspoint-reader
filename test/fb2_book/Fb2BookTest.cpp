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

#include "Fb2TestSupport.h"

namespace {

using fb2test::fileExists;
using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

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
  }
  ASSERT_EQ(second.getTocCount(), first.getTocCount());
  for (int i = 0; i < first.getTocCount(); i++) {
    EXPECT_EQ(second.getTocEntry(i).title, first.getTocEntry(i).title);
    EXPECT_EQ(second.getTocEntry(i).sectionIndex, first.getTocEntry(i).sectionIndex);
  }
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
  EXPECT_EQ(fresh[0], 2);
}

// KNOWN BUG (documents current behavior): loadMetadataCache never checks that
// reads succeed or that the data is sane. A book.bin whose tail was replaced
// by zeros still "loads": the title survives, every later field reads as
// empty/zero, and load() reports success with ZERO sections instead of
// falling back to a reparse. (Worse on device: a garbage string-length field
// makes readString resize() by up to 4GB.)
TEST_F(Fb2BookTest, CorruptedZeroFilledCacheIsTrustedInsteadOfReparsed) {
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

  ASSERT_TRUE(book.load(true));  // would reparse if the cache were rejected
  EXPECT_EQ(book.getTitle(), "Zombie");
  EXPECT_EQ(book.getSectionCount(), 0);  // real file has 2 sections
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
    book.sections = {{"One", 0, 100}, {"Two", 100, 300}, {"Three", 400, 600}};
    book.tocEntries = {{"One", 0}, {"Three", 2}};
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

TEST_F(Fb2MathTest, TocIndexForSectionFallsBackToClosestLowerEntry) {
  EXPECT_EQ(book.getTocIndexForSectionIndex(0), 0);  // exact
  EXPECT_EQ(book.getTocIndexForSectionIndex(2), 1);  // exact
  EXPECT_EQ(book.getTocIndexForSectionIndex(1), 0);  // between entries -> lower
  EXPECT_EQ(book.getTocIndexForSectionIndex(-1), -1);
}

TEST_F(Fb2MathTest, SectionIndexForTocIndexClampsOutOfRangeToZero) {
  EXPECT_EQ(book.getSectionIndexForTocIndex(0), 0);
  EXPECT_EQ(book.getSectionIndexForTocIndex(1), 2);
  EXPECT_EQ(book.getSectionIndexForTocIndex(-1), 0);
  EXPECT_EQ(book.getSectionIndexForTocIndex(99), 0);
}

TEST_F(Fb2MathTest, OutOfRangeSectionInfoIsEmpty) {
  EXPECT_EQ(book.getSectionInfo(-1).title, "");
  EXPECT_EQ(book.getSectionInfo(-1).length, 0u);
  EXPECT_EQ(book.getSectionInfo(3).title, "");
  EXPECT_EQ(book.getSectionInfo(1).title, "Two");
}

}  // namespace
