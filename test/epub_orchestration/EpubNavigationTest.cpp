// Navigation surface of Epub: resolveHrefToSpineIndex (fragments,
// percent-encoding, ../ segments), calculateProgress, the spine/TOC accessors
// and the guide `start` reference (FR-044, FR-045).

#include "EpubFixture.h"

namespace {

using epubtest::ManifestItem;
using epubtest::OpfSpec;
using epubtest::ZipBuilder;

// A book whose chapter hrefs exercise every resolution path: a nested folder,
// a percent-encoded space, a percent-encoded '#', and a duplicate filename in
// two different folders.
std::unique_ptr<Epub> makeHrefBook(const std::string& cacheRoot, const std::string& epubPath) {
  OpfSpec opf;
  opf.manifest = {
      {"c1", "chap1.xhtml", "application/xhtml+xml", ""},    {"c2", "text/chap2.xhtml", "application/xhtml+xml", ""},
      {"c3", "chap%201.xhtml", "application/xhtml+xml", ""}, {"c4", "odd%23name.xhtml", "application/xhtml+xml", ""},
      {"c5", "a/dup.xhtml", "application/xhtml+xml", ""},    {"c6", "b/dup.xhtml", "application/xhtml+xml", ""},
  };
  opf.spine = {"c1", "c2", "c3", "c4", "c5", "c6"};

  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", std::string(10, 'a'))
      .add("OEBPS/text/chap2.xhtml", std::string(20, 'b'))
      .add("OEBPS/chap 1.xhtml", std::string(30, 'c'))
      .add("OEBPS/odd#name.xhtml", std::string(40, 'd'))
      .add("OEBPS/a/dup.xhtml", std::string(50, 'e'))
      .add("OEBPS/b/dup.xhtml", std::string(60, 'f'));

  epubtest::writeBytes(epubPath, zip.build());
  return std::make_unique<Epub>(epubPath, cacheRoot);
}

TEST_F(EpubFixture, ResolveHrefMatchesTheExactSpinePath) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap1.xhtml"), 0);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/text/chap2.xhtml"), 1);
}

TEST_F(EpubFixture, ResolveHrefStripsTheFragmentBeforeMatching) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap1.xhtml#section-2"), 0);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/text/chap2.xhtml#top"), 1);
}

TEST_F(EpubFixture, ResolveHrefReturnsMinusOneForAnAnchorOnlyReference) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("#footnote-1"), -1);
  EXPECT_EQ(epub->resolveHrefToSpineIndex(""), -1);
}

TEST_F(EpubFixture, ResolveHrefDecodesPercentEscapes) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap%201.xhtml"), 2);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap 1.xhtml"), 2);
}

TEST_F(EpubFixture, ResolveHrefSplitsOnTheFragmentBeforeDecodingSoEscapedHashesSurvive) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  // %23 is part of the filename, not a fragment separator.
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/odd%23name.xhtml"), 3);
  // A real fragment after the escaped hash still gets stripped.
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/odd%23name.xhtml#here"), 3);
}

TEST_F(EpubFixture, ResolveHrefNormalisesParentSegments) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/text/../chap1.xhtml"), 0);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/./text/chap2.xhtml"), 1);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/a/../b/dup.xhtml"), 4);
}

TEST_F(EpubFixture, ResolveHrefFallsBackToAFilenameOnlyMatch) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  // No directory prefix at all: the filename alone identifies the spine item.
  EXPECT_EQ(epub->resolveHrefToSpineIndex("chap1.xhtml"), 0);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("chap2.xhtml"), 1);
  // Wrong folder, right filename: still resolves (EPUBs link loosely).
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/elsewhere/chap1.xhtml"), 0);
}

TEST_F(EpubFixture, ResolveHrefPrefersTheEarliestFilenameMatchOverALaterExactMatch) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  // Current behaviour: the scan tests exact-then-filename per spine item, so
  // "a/dup.xhtml" (index 4) wins for a target that exactly names index 5.
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/b/dup.xhtml"), 4);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/a/dup.xhtml"), 4);
}

TEST_F(EpubFixture, ResolveHrefReturnsMinusOneForAnUnknownTarget) {
  auto epub = makeHrefBook(cacheRoot, tmp.at("href.epub"));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/missing.xhtml"), -1);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("../../etc/passwd"), -1);
}

TEST_F(EpubFixture, CalculateProgressInterpolatesWithinTheCurrentSpineItem) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());
  ASSERT_EQ(epub->getBookSize(), 600u);

  EXPECT_FLOAT_EQ(epub->calculateProgress(0, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(epub->calculateProgress(0, 0.5f), 50.0f / 600.0f);
  EXPECT_FLOAT_EQ(epub->calculateProgress(0, 1.0f), 100.0f / 600.0f);
  EXPECT_FLOAT_EQ(epub->calculateProgress(1, 0.0f), 100.0f / 600.0f);
  EXPECT_FLOAT_EQ(epub->calculateProgress(1, 0.5f), 200.0f / 600.0f);
  EXPECT_FLOAT_EQ(epub->calculateProgress(2, 1.0f), 1.0f);
}

TEST_F(EpubFixture, CalculateProgressIsZeroWithoutABookSize) {
  const Epub unloaded(tmp.at("book.epub"), cacheRoot);
  EXPECT_FLOAT_EQ(unloaded.calculateProgress(1, 0.5f), 0.0f);

  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());
  // A negative index has no previous chapter and no size of its own.
  EXPECT_FLOAT_EQ(epub->calculateProgress(-1, 0.5f), 0.0f);
}

TEST_F(EpubFixture, ProgressPastTheLastSpineItemWrapsTheChapterSize) {
  // Pinned, not endorsed: getCumulativeSpineItemSize() returns 0 out of range,
  // so `cumulative(i) - cumulative(i-1)` underflows size_t for an index past
  // the end of the spine and the fraction is scaled by a nonsense chapter size.
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  EXPECT_FLOAT_EQ(epub->calculateProgress(3, 0.0f), 1.0f);
  EXPECT_GT(epub->calculateProgress(3, 0.5f), 1.0f);
  // Far out of range there is no previous chapter either, so it reads as 0.
  EXPECT_FLOAT_EQ(epub->calculateProgress(99, 0.5f), 0.0f);
}

TEST_F(EpubFixture, SpineAndTocAccessorsClampOutOfRangeIndexes) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  // Out-of-range spine reads fall back to the first entry.
  EXPECT_EQ(epub->getSpineItem(-1).href, "OEBPS/chap1.xhtml");
  EXPECT_EQ(epub->getSpineItem(99).href, "OEBPS/chap1.xhtml");
  // Out-of-range TOC reads yield a default-constructed entry.
  EXPECT_EQ(epub->getTocItem(-1).title, "");
  EXPECT_EQ(epub->getTocItem(99).title, "");
  EXPECT_EQ(epub->getTocItem(99).spineIndex, -1);
  // Out-of-range TOC->spine mapping falls back to the first chapter.
  EXPECT_EQ(epub->getSpineIndexForTocIndex(-1), 0);
  EXPECT_EQ(epub->getSpineIndexForTocIndex(99), 0);
  // Cumulative sizes are 0 outside the spine.
  EXPECT_EQ(epub->getCumulativeSpineItemSize(-1), 0u);
  EXPECT_EQ(epub->getCumulativeSpineItemSize(99), 0u);
}

TEST_F(EpubFixture, SpineItemsInheritTheTocIndexOfThePrecedingEntry) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  // TOC points at chapters 1 and 3; chapter 2 inherits chapter 1's TOC index.
  EXPECT_EQ(epub->getTocIndexForSpineIndex(0), 0);
  EXPECT_EQ(epub->getTocIndexForSpineIndex(1), 0);
  EXPECT_EQ(epub->getTocIndexForSpineIndex(2), 1);
}

TEST_F(EpubFixture, TextReferenceDefaultsToTheFirstSpineItem) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineIndexForTextReference(), 0);
}

TEST_F(EpubFixture, GuideStartReferenceSelectsTheOpeningSpineItem) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""},
                  {"c2", "chap2.xhtml", "application/xhtml+xml", ""},
                  {"c3", "chap3.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1", "c2", "c3"};
  opf.guide = "  <guide>\n    <reference type=\"start\" href=\"chap2.xhtml\" title=\"Begin\"/>\n  </guide>\n";
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1)
      .add("OEBPS/chap2.xhtml", book.chapter2)
      .add("OEBPS/chap3.xhtml", book.chapter3);

  auto epub = make(writeEpub("start.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineIndexForTextReference(), 1);
}

TEST_F(EpubFixture, GuideTextReferencesAreIgnored) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""},
                  {"c2", "chap2.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1", "c2"};
  opf.guide = "  <guide>\n    <reference type=\"text\" href=\"chap2.xhtml\" title=\"Text\"/>\n  </guide>\n";
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1)
      .add("OEBPS/chap2.xhtml", book.chapter2);

  auto epub = make(writeEpub("textref.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineIndexForTextReference(), 0);
}

TEST_F(EpubFixture, AStartReferenceOutsideTheSpineFallsBackToTheFirstItem) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  opf.guide = "  <guide>\n    <reference type=\"start\" href=\"nowhere.xhtml\"/>\n  </guide>\n";
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1);

  auto epub = make(writeEpub("badstart.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineIndexForTextReference(), 0);
}

}  // namespace
