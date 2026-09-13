// ProgressMapper tests: KOReader xpointer <-> CrossPoint position mapping.
//
// The Epub stub streams fixture XHTML; the Section stub serves the pagination
// LUTs (page-start visible offsets, paragraph/li/anchor pages) that the
// device's section cache would provide. Offsets asserted here are the
// mapper's own visible-codepoint space: the count of visible chars up to and
// including the target character.
//
// Tests marked "documents current limitation" pin behavior that upstream
// develop has since changed (#3111 "compare mapped positions", #3174
// "preserve precise upload progress positions"); they assert what THIS branch
// does and carry the upstream-expected behavior in a comment.

#include <Epub/Section.h>
#include <ProgressMapper.h>
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

// Chapter fixtures without inter-tag whitespace so visible-offset math stays
// exact. ch1: 20 visible chars (p[1]: 0-9, p[2]: 10-19). ch2: div-wrapped,
// 10 chars. ch3: list + paragraph, 12 chars. declaredSize 100 each gives
// cumulative byte sizes 100/200/300 for clean percentage math.
std::shared_ptr<Epub> makeThreeChapterBook() {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem(
      "ch1.xhtml",
      "<html><head><title>T</title></head><body><p>0123456789</p><p id=\"second\">abcdefghij</p></body></html>", 100);
  epub->addSpineItem("ch2.xhtml", "<html><body><div class=\"wrap\"><p>11111</p><p>22222</p></div></body></html>", 100);
  epub->addSpineItem("ch3.xhtml", "<html><body><ul><li>aaaa</li><li>bbbb</li></ul><p>cccc</p></body></html>", 100);
  return epub;
}

std::shared_ptr<Epub> makeSingleChapterBook(const std::string& document) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("ch1.xhtml", document, 100);
  return epub;
}

class ProgressMapperTest : public ::testing::Test {
 protected:
  void SetUp() override { SectionStubRegistry::reset(); }
  GfxRenderer renderer;
};

}  // namespace

// --- toCrossPoint: ancestry-mode xpointer resolution -----------------------

TEST_F(ProgressMapperTest, AncestryXPathResolvesOffsetPageAndAnchor) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].3", 0.55f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.spineIndex, 0);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 13u);
  EXPECT_EQ(pos.pageNumber, 1);
  EXPECT_EQ(pos.totalPages, 3);
  ASSERT_TRUE(pos.hasParagraphIndex);
  EXPECT_EQ(pos.paragraphIndex, 2);
  EXPECT_STREQ(pos.xpathAnchorId, "second");
  EXPECT_FALSE(pos.hasLiIndex);
  EXPECT_FALSE(SectionStubRegistry::forSpine(0).lastPreferFirstAtOffset);
}

TEST_F(ProgressMapperTest, AncestryTextNodeIndexSkipsInlineChildText) {
  const auto epub = makeSingleChapterBook("<html><body><p>abc<b>bold</b>xyz</p></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 5};

  // text()[2] of the <p> is "xyz"; its 2nd char is the 9th visible char.
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[1]/text()[2].2", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 9u);
  EXPECT_EQ(pos.pageNumber, 1);
}

TEST_F(ProgressMapperTest, LegacyGlobalParagraphFormResolvesSameOffset) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  // No "]/body/" segment, so ancestry parsing is skipped and the legacy
  // global p-counting mode resolves the same content position.
  const SavedProgressPosition ko{"/body/DocFragment[1]/p[2].3", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 13u);
  EXPECT_EQ(pos.pageNumber, 1);
  EXPECT_FALSE(pos.hasParagraphIndex);  // legacy mode does not report ancestry extras
}

TEST_F(ProgressMapperTest, TextNodeWithoutBracketDefaultsToFirst) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  // KOReader also emits the unindexed "/text().N" form.
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text().3", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 13u);
}

TEST_F(ProgressMapperTest, RelaxedRetryResolvesXPathOmittingWrapperDiv) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(1);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 5};

  // ch2 wraps its paragraphs in a <div> the producer omitted (PR #2777
  // compatibility case): the strict depth match fails, the relaxed retry
  // resolves it.
  const SavedProgressPosition ko{"/body/DocFragment[2]/body/p[2]/text()[1].2", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.spineIndex, 1);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 7u);
  EXPECT_EQ(pos.pageNumber, 1);
}

TEST_F(ProgressMapperTest, ListItemXPathSetsLiIndexAndRefinesPageThroughLut) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(2);
  lut.cachedPageCount = 9;
  lut.listItemPages[2] = 7;  // no pageStartOffsets: force the li-LUT refinement path

  const SavedProgressPosition ko{"/body/DocFragment[3]/body/ul[1]/li[2]/text()[1].1", 0.9f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.spineIndex, 2);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 5u);
  ASSERT_TRUE(pos.hasLiIndex);
  EXPECT_EQ(pos.liIndex, 2);
  EXPECT_FALSE(pos.hasParagraphIndex);
  EXPECT_EQ(pos.pageNumber, 7);
  EXPECT_EQ(pos.totalPages, 9);
}

TEST_F(ProgressMapperTest, ImageXPathRequestsFirstPageAtOffset) {
  const auto epub = makeSingleChapterBook("<html><body><p>xx</p><img src=\"i\"/></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 2};

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/img[1].0", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 2u);
  EXPECT_EQ(pos.pageNumber, 1);
  // Image anchors must ask the LUT for the FIRST page at the offset so a
  // full-page image is not skipped.
  EXPECT_TRUE(SectionStubRegistry::forSpine(0).lastPreferFirstAtOffset);
}

TEST_F(ProgressMapperTest, AnchorIdRefinesPageWhenNoOffsetLut) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 4;
  lut.anchorPages["second"] = 3;  // captured from <p id="second"> at the match

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].3", 0.0f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_STREQ(pos.xpathAnchorId, "second");
  EXPECT_EQ(pos.pageNumber, 3);
}

TEST_F(ProgressMapperTest, ParagraphLutRefinementRaisesPage) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 12;
  lut.paragraphPages[2] = 4;
  lut.paragraphPages[3] = 8;

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].3", 0.0f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  // Intra-spine percentage says page 0; the paragraph LUT knows better.
  ASSERT_TRUE(pos.hasParagraphIndex);
  EXPECT_EQ(pos.pageNumber, 4);
}

TEST_F(ProgressMapperTest, ParagraphLutRefinementCapsBelowNextParagraph) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 12;
  lut.paragraphPages[2] = 4;
  lut.paragraphPages[3] = 8;

  // percentage 0.28 -> intra 0.84 -> intra page 9, past paragraph 3's LUT
  // page: capped to 7 (one before the next paragraph).
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].3", 0.28f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.pageNumber, 7);
}

TEST_F(ProgressMapperTest, ParagraphLutSpanOfOneIsNotTrustedForCapping) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 12;
  lut.paragraphPages[2] = 4;
  lut.paragraphPages[3] = 5;  // span of 1: LUT granularity too coarse to cap

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].3", 0.28f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.pageNumber, 9);
}

TEST_F(ProgressMapperTest, ChapterStartXPathFormsResolveToPageZero) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(1);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 5};

  for (const char* xpath : {"/body/DocFragment[2]", "/body/DocFragment[2].0", "/body/DocFragment[2]/body"}) {
    const auto pos = ProgressMapper::toCrossPoint(epub, {xpath, 0.4f}, renderer);
    EXPECT_EQ(pos.spineIndex, 1) << xpath;
    ASSERT_TRUE(pos.hasVisibleTextOffset) << xpath;
    EXPECT_EQ(pos.visibleTextOffset, 0u) << xpath;
    EXPECT_EQ(pos.pageNumber, 0) << xpath;
  }

  // A nonzero terminal offset is NOT a chapter start.
  const auto pos = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[2].5", 0.4f}, renderer);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
}

TEST_F(ProgressMapperTest, UnresolvableElementXPathFallsBackToChapterStart) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  // ch1 has no <div>; the ancestry scan fails but the terminal ".0" keeps it
  // a chapter-start form.
  const auto pos = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[1]/body/div[1].0", 0.4f}, renderer);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 0u);
  EXPECT_EQ(pos.pageNumber, 0);
}

TEST_F(ProgressMapperTest, ParagraphAnchorWithoutOffsetResolvesToParagraphStart) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  // findXPathForParagraph-style output: element path, no text()/offset.
  const auto pos = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[1]/body/p[2]", 0.4f}, renderer);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 10u);
  EXPECT_EQ(pos.pageNumber, 1);
}

TEST_F(ProgressMapperTest, ListItemAnchorWithoutOffsetFallsBackToPercentage) {
  // Documents a current limitation on BOTH this branch and upstream develop:
  // an element xpath ending in li[N] with no terminal offset has no "/p[" for
  // the legacy mode and no trailing ".N" for ancestry parsing, so list-item
  // paragraph anchors (as produced by findXPathForParagraph for lists) sync
  // by raw percentage only.
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(2);
  lut.cachedPageCount = 9;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[3]/body/ul[1]/li[2]", 0.7f}, renderer);
  EXPECT_EQ(pos.spineIndex, 2);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
}

TEST_F(ProgressMapperTest, BodyTextXPathResolvesBareBodyText) {
  const auto epub = makeSingleChapterBook("<html><body>hello world</body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 5};

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/text()[1].6", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 6u);
  EXPECT_EQ(pos.pageNumber, 1);
}

TEST_F(ProgressMapperTest, CommentBeforeElementBreaksAncestryResolution) {
  // PRODUCTION BUG (present on this branch AND upstream develop): the
  // ParagraphStreamer tag scanner treats "<!--" as an opening element named
  // "--" that never closes, desyncing its depth tracking. An xpath whose
  // target follows a comment inside a partially-matched ancestry therefore
  // fails content resolution and falls back to raw percentage. This test
  // pins the current (wrong) behavior; the companion assertions show the
  // identical comment-free markup resolves fine.
  const auto clean = makeSingleChapterBook("<html><body><div><p>abcd</p></div></body></html>");
  const auto commented = makeSingleChapterBook("<html><body><div><!-- note --><p>abcd</p></div></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 1;
  lut.pageStartOffsets = {0};

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/div[1]/p[1]/text()[1].2", 0.1f};

  const auto cleanPos = ProgressMapper::toCrossPoint(clean, ko, renderer);
  ASSERT_TRUE(cleanPos.hasVisibleTextOffset);
  EXPECT_EQ(cleanPos.visibleTextOffset, 2u);

  const auto commentedPos = ProgressMapper::toCrossPoint(commented, ko, renderer);
  EXPECT_FALSE(commentedPos.hasVisibleTextOffset);  // should resolve to offset 2 like cleanPos
}

TEST_F(ProgressMapperTest, StyleTextInsideBodyIsNotCounted) {
  const auto epub = makeSingleChapterBook("<html><body><style>p{color:red}</style><p>abcd</p></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 1;
  lut.pageStartOffsets = {0};

  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[1]/text()[1].2", 0.1f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 2u);
}

TEST_F(ProgressMapperTest, EntitiesCountAsSingleCodepoints) {
  const auto epub = makeSingleChapterBook("<html><body><p>a&amp;b&#233;c</p></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 1;
  lut.pageStartOffsets = {0};

  // Visible text is a & b e-acute c: the 4th char is the numeric reference.
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[1]/text()[1].4", 0.1f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 4u);
}

TEST_F(ProgressMapperTest, CrlfPairsCountAsOneCodepoint) {
  const auto epub = makeSingleChapterBook("<html><body><p>a\r\nb\rc</p></body></html>");
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 1;
  lut.pageStartOffsets = {0};

  // XML line-ending normalization: "\r\n" and lone "\r" are one newline each,
  // so 'c' is the 5th visible char.
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[1]/text()[1].5", 0.1f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 5u);
}

// --- toCrossPoint: percentage fallback and hostile xpaths -------------------

TEST_F(ProgressMapperTest, EmptyXPathAtZeroPercentIsBookStart) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 10;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"", 0.0f}, renderer);
  EXPECT_EQ(pos.spineIndex, 0);
  EXPECT_EQ(pos.pageNumber, 0);
  EXPECT_EQ(pos.totalPages, 10);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
}

TEST_F(ProgressMapperTest, EmptyXPathMidBookSelectsSpineByBytes) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(1).cachedPageCount = 11;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"", 0.5f}, renderer);
  EXPECT_EQ(pos.spineIndex, 1);
  EXPECT_EQ(pos.totalPages, 11);
  EXPECT_EQ(pos.pageNumber, 5);
}

TEST_F(ProgressMapperTest, EmptyXPathAtHundredPercentIsLastPageOfLastSpine) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(2).cachedPageCount = 8;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"", 1.0f}, renderer);
  EXPECT_EQ(pos.spineIndex, 2);
  EXPECT_EQ(pos.pageNumber, 7);
}

TEST_F(ProgressMapperTest, OutOfRangePercentagesAreClamped) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 10;
  SectionStubRegistry::forSpine(2).cachedPageCount = 8;

  const auto high = ProgressMapper::toCrossPoint(epub, {"", 1.5f}, renderer);
  const auto one = ProgressMapper::toCrossPoint(epub, {"", 1.0f}, renderer);
  EXPECT_EQ(high.spineIndex, one.spineIndex);
  EXPECT_EQ(high.pageNumber, one.pageNumber);

  const auto low = ProgressMapper::toCrossPoint(epub, {"", -0.5f}, renderer);
  const auto zero = ProgressMapper::toCrossPoint(epub, {"", 0.0f}, renderer);
  EXPECT_EQ(low.spineIndex, zero.spineIndex);
  EXPECT_EQ(low.pageNumber, zero.pageNumber);
}

TEST_F(ProgressMapperTest, CurrentSpinePageCountOverridesLut) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(1).cachedPageCount = 11;  // must NOT be used

  const auto pos = ProgressMapper::toCrossPoint(epub, {"", 0.5f}, renderer, /*currentSpineIndex=*/1,
                                                /*totalPagesInCurrentSpine=*/6);
  EXPECT_EQ(pos.spineIndex, 1);
  EXPECT_EQ(pos.totalPages, 6);
  EXPECT_EQ(pos.pageNumber, 3);
}

TEST_F(ProgressMapperTest, PageDensityEstimatedFromCurrentSpine) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("a.xhtml", "<html><body><p>x</p></body></html>", 200);
  epub->addSpineItem("b.xhtml", "<html><body><p>y</p></body></html>", 100);

  // Current spine 0 renders 200 bytes as 10 pages; spine 1 is half the bytes,
  // so its page count is estimated at 5 without any section cache.
  const auto pos = ProgressMapper::toCrossPoint(epub, {"", 0.9f}, renderer, /*currentSpineIndex=*/0,
                                                /*totalPagesInCurrentSpine=*/10);
  EXPECT_EQ(pos.spineIndex, 1);
  EXPECT_EQ(pos.totalPages, 5);
  EXPECT_EQ(pos.pageNumber, 3);
}

TEST_F(ProgressMapperTest, FallbackTotalPagesUsedWhenNoOtherSource) {
  const auto epub = makeThreeChapterBook();

  const auto withFallback = ProgressMapper::toCrossPoint(epub, {"", 0.0f}, renderer, -1, 0, /*fallbackTotalPages=*/7);
  EXPECT_EQ(withFallback.totalPages, 7);

  const auto bare = ProgressMapper::toCrossPoint(epub, {"", 0.0f}, renderer);
  EXPECT_EQ(bare.totalPages, 1);
  EXPECT_EQ(bare.pageNumber, 0);
}

TEST_F(ProgressMapperTest, GarbageXPathBehavesLikeEmptyXPath) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(1).cachedPageCount = 11;

  const auto garbage = ProgressMapper::toCrossPoint(epub, {"garbage !!! not an xpath ////", 0.5f}, renderer);
  const auto empty = ProgressMapper::toCrossPoint(epub, {"", 0.5f}, renderer);

  EXPECT_FALSE(garbage.hasVisibleTextOffset);
  EXPECT_EQ(garbage.spineIndex, empty.spineIndex);
  EXPECT_EQ(garbage.pageNumber, empty.pageNumber);
  EXPECT_EQ(garbage.totalPages, empty.totalPages);
}

TEST_F(ProgressMapperTest, NonNumericIndicesFallBackToPercentage) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(1).cachedPageCount = 11;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[x]/body/p[y]/text()[z].w", 0.5f}, renderer);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.spineIndex, 1);
  EXPECT_EQ(pos.pageNumber, 5);
}

TEST_F(ProgressMapperTest, HugeDocFragmentIndexFallsBackButAncestryStillResolves) {
  // A DocFragment index beyond the spine falls back to byte-based spine
  // selection, but the element steps still resolve within that spine.
  // (Indices with enough digits to overflow int are a separate reported
  // bug -- parseIndex has no overflow guard; this value stays representable.)
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  const SavedProgressPosition ko{"/body/DocFragment[2000000000]/body/p[1]/text()[1].5", 0.0f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);

  EXPECT_EQ(pos.spineIndex, 0);
  ASSERT_TRUE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.visibleTextOffset, 5u);
  EXPECT_EQ(pos.pageNumber, 0);
}

TEST_F(ProgressMapperTest, VeryLongDeepXPathIsHandledWithoutCrash) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 3;

  std::string xpath = "/body/DocFragment[1]/body";
  for (int i = 0; i < 4000; i++) xpath += "/div[1]";
  xpath += "/p[1]/text()[1].2";

  const auto pos = ProgressMapper::toCrossPoint(epub, {xpath, 0.1f}, renderer);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
  EXPECT_EQ(pos.spineIndex, 0);
  EXPECT_GE(pos.pageNumber, 0);
  EXPECT_LT(pos.pageNumber, pos.totalPages);
}

TEST_F(ProgressMapperTest, StepsBeyondMaxDepthDoNotResolve) {
  // parseXPathSteps caps at 16 steps; deeper targets cannot be matched and
  // fall back to percentage without crashing.
  std::string content = "<html><body>";
  for (int i = 0; i < 18; i++) content += "<div>";
  content += "<p>abcd</p>";
  for (int i = 0; i < 18; i++) content += "</div>";
  content += "</body></html>";
  const auto epub = makeSingleChapterBook(content);
  SectionStubRegistry::forSpine(0).cachedPageCount = 1;

  std::string xpath = "/body/DocFragment[1]/body";
  for (int i = 0; i < 17; i++) xpath += "/div[1]";
  xpath += "/p[1]/text()[1].2";

  const auto pos = ProgressMapper::toCrossPoint(epub, {xpath, 0.1f}, renderer);
  EXPECT_FALSE(pos.hasVisibleTextOffset);
}

TEST_F(ProgressMapperTest, NanPercentageDoesNotCrash) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(2).cachedPageCount = 4;

  const auto pos = ProgressMapper::toCrossPoint(epub, {"", std::nanf("")}, renderer);
  // std::min/std::max NaN ordering resolves the clamp to 1.0.
  EXPECT_EQ(pos.spineIndex, 2);
  EXPECT_EQ(pos.pageNumber, 3);
}

// --- fromRichPosition -------------------------------------------------------

TEST_F(ProgressMapperTest, RichPositionSpineOutOfRangeIsRejected) {
  const auto epub = makeThreeChapterBook();
  KOReaderRichPosition rich;
  rich.spineIndex = 3;
  EXPECT_FALSE(ProgressMapper::fromRichPosition(epub, rich, renderer).has_value());
}

TEST_F(ProgressMapperTest, RichPositionResolvesContentAnchorFirst) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  KOReaderRichPosition rich;
  rich.pctQ = 500000;
  rich.spineIndex = 0;
  rich.pageNumber = 9;  // stale layout hint; the xpath must win
  rich.totalPages = 10;
  rich.xpath = "/body/DocFragment[1]/body/p[2]/text()[1].3";

  const auto pos = ProgressMapper::fromRichPosition(epub, rich, renderer);
  ASSERT_TRUE(pos.has_value());
  ASSERT_TRUE(pos->hasVisibleTextOffset);
  EXPECT_EQ(pos->visibleTextOffset, 13u);
  EXPECT_EQ(pos->pageNumber, 1);
}

TEST_F(ProgressMapperTest, RichPositionIdenticalLayoutTransfersPage) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 10;

  KOReaderRichPosition rich;
  rich.spineIndex = 0;
  rich.pageNumber = 9;
  rich.totalPages = 10;
  rich.xpath = "/body/DocFragment[1]/body/p[2]/text()[1].3";

  // xpathAlreadyTried: the caller resolved (and failed) this xpath already;
  // the identical page count transfers the page losslessly.
  const auto pos = ProgressMapper::fromRichPosition(epub, rich, renderer, /*xpathAlreadyTried=*/true);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(pos->pageNumber, 9);
  EXPECT_EQ(pos->totalPages, 10);
  EXPECT_FALSE(pos->hasVisibleTextOffset);
  EXPECT_EQ(SectionStubRegistry::forSpine(0).visibleOffsetQueries, 0);
}

TEST_F(ProgressMapperTest, RichPositionIdenticalLayoutClampsPageIntoRange) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 10;

  KOReaderRichPosition rich;
  rich.spineIndex = 0;
  rich.pageNumber = 99;
  rich.totalPages = 10;

  const auto pos = ProgressMapper::fromRichPosition(epub, rich, renderer, true);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(pos->pageNumber, 9);
}

TEST_F(ProgressMapperTest, RichPositionWithoutSectionCacheIsRejected) {
  const auto epub = makeThreeChapterBook();
  KOReaderRichPosition rich;
  rich.spineIndex = 0;
  rich.pageNumber = 3;
  rich.totalPages = 10;
  EXPECT_FALSE(ProgressMapper::fromRichPosition(epub, rich, renderer, true).has_value());
}

TEST_F(ProgressMapperTest, RichPositionUsesParagraphLutWhenLayoutDiffers) {
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 20;
  lut.paragraphPages[2] = 6;

  KOReaderRichPosition rich;
  rich.spineIndex = 0;
  rich.pageNumber = 5;
  rich.totalPages = 10;
  rich.paragraphIndex = 2;

  const auto pos = ProgressMapper::fromRichPosition(epub, rich, renderer, true);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(pos->pageNumber, 6);
  ASSERT_TRUE(pos->hasParagraphIndex);
  EXPECT_EQ(pos->paragraphIndex, 2);
  EXPECT_EQ(pos->totalPages, 20);
}

TEST_F(ProgressMapperTest, RichPositionScalesPageFractionAsLastResort) {
  const auto epub = makeThreeChapterBook();
  SectionStubRegistry::forSpine(0).cachedPageCount = 21;

  KOReaderRichPosition rich;
  rich.spineIndex = 0;
  rich.pageNumber = 5;
  rich.totalPages = 11;

  const auto pos = ProgressMapper::fromRichPosition(epub, rich, renderer, true);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(pos->pageNumber, 10);
  EXPECT_EQ(pos->totalPages, 21);
}

// --- toSavedProgress --------------------------------------------------------

namespace {
std::shared_ptr<Epub> makeTwoChapterBook() {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem(
      "ch1.xhtml",
      "<html><head><title>T</title></head><body><p>0123456789</p><p id=\"second\">abcdefghij</p></body></html>", 100);
  epub->addSpineItem("ch2.xhtml", "<html><body><div class=\"wrap\"><p>11111</p><p>22222</p></div></body></html>", 100);
  return epub;
}
}  // namespace

TEST_F(ProgressMapperTest, SavedProgressUsesParagraphIndexWhenAvailable) {
  const auto epub = makeTwoChapterBook();

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 3;
  pos.totalPages = 11;
  pos.hasParagraphIndex = true;
  pos.paragraphIndex = 2;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body/p[2]");
  EXPECT_NEAR(saved.percentage, 0.15f, 1e-4f);  // (0.3 * 100) / 200 bytes
}

TEST_F(ProgressMapperTest, SavedProgressResolvesPageFractionToTextOffset) {
  const auto epub = makeTwoChapterBook();

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 1;
  pos.totalPages = 2;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body/p[2]/text()[1].10");
  EXPECT_NEAR(saved.percentage, 0.5f, 1e-4f);
}

TEST_F(ProgressMapperTest, SavedProgressFirstPageIsChapterStart) {
  const auto epub = makeTwoChapterBook();

  CrossPointPosition pos{};
  pos.spineIndex = 1;
  pos.pageNumber = 0;
  pos.totalPages = 5;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[2]/body");
  EXPECT_NEAR(saved.percentage, 0.5f, 1e-4f);  // spine 1 starts at byte 100 of 200
}

TEST_F(ProgressMapperTest, SavedProgressSinglePageChapterIsChapterStart) {
  const auto epub = makeTwoChapterBook();

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 0;
  pos.totalPages = 1;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body");
  EXPECT_NEAR(saved.percentage, 0.0f, 1e-6f);
}

TEST_F(ProgressMapperTest, SavedProgressEmptyChapterFallsBackToBodyBase) {
  const auto epub = makeSingleChapterBook("<html><body></body></html>");

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 1;
  pos.totalPages = 2;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body");
}

TEST_F(ProgressMapperTest, SavedProgressMalformedChapterFallsBackToRawParagraphCount) {
  // The bare "&" makes expat fail, so the codepoint-exact resolver cannot
  // run; the byte-oriented generateXPath fallback still counts <p> tags.
  const auto epub = makeSingleChapterBook("<html><body><p>abc</p><p>def & raw</p></body></html>");

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 1;
  pos.totalPages = 2;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body/p[2]");
}

TEST_F(ProgressMapperTest, RoundTripPreservesPageOnSameLayout) {
  const auto epub = makeTwoChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 10};

  for (int page : {0, 1}) {
    CrossPointPosition pos{};
    pos.spineIndex = 0;
    pos.pageNumber = page;
    pos.totalPages = 2;

    const auto saved = ProgressMapper::toSavedProgress(epub, pos);
    const auto back = ProgressMapper::toCrossPoint(epub, saved, renderer);
    EXPECT_EQ(back.spineIndex, 0) << "page " << page;
    EXPECT_EQ(back.pageNumber, page) << "page " << page;
  }
}

TEST_F(ProgressMapperTest, RoundTripThroughNestedChapterPreservesPage) {
  const auto epub = makeTwoChapterBook();
  auto& lut = SectionStubRegistry::forSpine(1);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 5};

  CrossPointPosition pos{};
  pos.spineIndex = 1;
  pos.pageNumber = 1;
  pos.totalPages = 2;

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  EXPECT_EQ(saved.xpath, "/body/DocFragment[2]/body/div[1]/p[2]/text()[1].5");

  const auto back = ProgressMapper::toCrossPoint(epub, saved, renderer);
  EXPECT_EQ(back.spineIndex, 1);
  ASSERT_TRUE(back.hasVisibleTextOffset);
  EXPECT_EQ(back.visibleTextOffset, 10u);
  EXPECT_EQ(back.pageNumber, 1);
}

TEST_F(ProgressMapperTest, SavedProgressDiscardsPreciseVisibleTextOffset) {
  // Documents current limitation, fixed upstream in #3174 ("preserve precise
  // upload progress positions"): toSavedProgress ignores the authoritative
  // visibleTextOffset and quantizes the upload to the page fraction. Upstream
  // resolves the offset itself via findXPathForVisibleTextOffset.
  const auto epub = makeTwoChapterBook();

  CrossPointPosition pos{};
  pos.spineIndex = 0;
  pos.pageNumber = 0;
  pos.totalPages = 2;
  pos.hasVisibleTextOffset = true;
  pos.visibleTextOffset = 17;  // precise position, deep into p[2]

  const auto saved = ProgressMapper::toSavedProgress(epub, pos);
  // Page 0 of 2 -> intra 0 -> chapter start; the known offset 17 is lost.
  EXPECT_EQ(saved.xpath, "/body/DocFragment[1]/body");

  // Same page, different precise offsets: identical uploads.
  CrossPointPosition a = pos;
  a.pageNumber = 1;
  a.visibleTextOffset = 12;
  CrossPointPosition b = pos;
  b.pageNumber = 1;
  b.visibleTextOffset = 19;
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, a).xpath, ProgressMapper::toSavedProgress(epub, b).xpath);
}

TEST_F(ProgressMapperTest, PercentageOrderingCanContradictContentOrdering) {
  // Documents current limitation, addressed upstream by #3111 ("compare
  // mapped positions", which added ProgressComparison to this library): on
  // this branch callers can only rank a local and a remote position by their
  // raw percentages, which come from DIFFERENT layouts. Here remote record A
  // carries a higher percentage than B even though B is further into the
  // content; mapping both locally proves the content order, which upstream's
  // compareProgress would use instead of the percentages.
  const auto epub = makeThreeChapterBook();
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 3;
  lut.pageStartOffsets = {0, 10, 15};

  const SavedProgressPosition recordA{"/body/DocFragment[1]/body/p[1]/text()[1].1", 0.80f};
  const SavedProgressPosition recordB{"/body/DocFragment[1]/body/p[2]/text()[1].8", 0.70f};

  const auto posA = ProgressMapper::toCrossPoint(epub, recordA, renderer);
  const auto posB = ProgressMapper::toCrossPoint(epub, recordB, renderer);

  ASSERT_TRUE(posA.hasVisibleTextOffset);
  ASSERT_TRUE(posB.hasVisibleTextOffset);
  EXPECT_EQ(posA.visibleTextOffset, 1u);
  EXPECT_EQ(posB.visibleTextOffset, 18u);
  EXPECT_LT(posA.visibleTextOffset, posB.visibleTextOffset);  // B is truly ahead...
  EXPECT_LT(posA.pageNumber, posB.pageNumber);
  EXPECT_GT(recordA.percentage, recordB.percentage);  // ...but percentage says A.
}
