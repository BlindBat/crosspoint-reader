// ChapterXPathResolver tests: KOReader-compatible XPath generation from
// streamed XHTML.
//
// findXPathForParagraph resolves the Nth paragraph-like element (<p> and <li>
// both count) to its real element ancestry; findXPathForProgress resolves an
// intra-spine progress fraction to /.../text()[N].C over visible paragraph
// text, counting UTF-8 codepoints the way the section pagination does.
//
// Tests marked "documents current limitation" pin behavior that upstream
// develop has since changed (the precise-position work around #3174); they
// assert what THIS branch does so a regression is visible, and carry the
// upstream-expected value in a comment for the next sync.

#include <ChapterXPathResolver.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

std::shared_ptr<Epub> makeBook(const std::string& bodyContent) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("ch1.xhtml", "<html><head><title>T</title></head><body>" + bodyContent + "</body></html>");
  return epub;
}

std::shared_ptr<Epub> makeBookRaw(const std::string& document) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("ch1.xhtml", document);
  return epub;
}

}  // namespace

// --- findXPathForParagraph -------------------------------------------------

TEST(FindXPathForParagraph, ResolvesFirstParagraph) {
  const auto epub = makeBook("<p>alpha</p><p>beta</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 1), "/body/DocFragment[1]/body/p[1]");
}

TEST(FindXPathForParagraph, ResolvesSecondParagraph) {
  const auto epub = makeBook("<p>alpha</p><p>beta</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2), "/body/DocFragment[1]/body/p[2]");
}

TEST(FindXPathForParagraph, BuildsNestedAncestryWithSiblingIndices) {
  const auto epub = makeBook("<div><p>a</p></div><div><section><p>b</p><p>c</p></section></div>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 3),
            "/body/DocFragment[1]/body/div[2]/section[1]/p[2]");
}

TEST(FindXPathForParagraph, ListItemsCountAsParagraphPositions) {
  const auto epub = makeBook("<ul><li>x</li><li>y</li></ul><p>z</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2), "/body/DocFragment[1]/body/ul[1]/li[2]");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 3), "/body/DocFragment[1]/body/p[1]");
}

TEST(FindXPathForParagraph, SpineIndexBecomesOneBasedDocFragment) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("ch1.xhtml", "<html><body><p>one</p></body></html>");
  epub->addSpineItem("ch2.xhtml", "<html><body><p>two</p></body></html>");
  epub->addSpineItem("ch3.xhtml", "<html><body><p>three</p></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 2, 1), "/body/DocFragment[3]/body/p[1]");
}

TEST(FindXPathForParagraph, RejectsInvalidArguments) {
  const auto epub = makeBook("<p>alpha</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(nullptr, 0, 1), "");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 0), "");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, -1, 1), "");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 1, 1), "");
}

TEST(FindXPathForParagraph, ParagraphBeyondCountReturnsEmpty) {
  const auto epub = makeBook("<p>alpha</p><p>beta</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 3), "");
}

TEST(FindXPathForParagraph, EmptyHrefReturnsEmpty) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("", "<html><body><p>alpha</p></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 1), "");
}

TEST(FindXPathForParagraph, StreamFailureReturnsEmpty) {
  const auto epub = makeBook("<p>alpha</p>");
  epub->failStreaming = true;
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 1), "");
}

TEST(FindXPathForParagraph, GarbageContentReturnsEmpty) {
  const auto epub = makeBookRaw("\x01\x02<<<this is not xml>>>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 1), "");
}

TEST(FindXPathForParagraph, MatchBeforeMalformedTailStillResolves) {
  // The parser is stopped as soon as the target paragraph opens; garbage
  // after that point must not retract the answer.
  const auto epub = makeBookRaw("<html><body><p>a</p><p>b</p><UNCLOSED \x01\x02");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2), "/body/DocFragment[1]/body/p[2]");
}

TEST(FindXPathForParagraph, TruncatedBeforeMatchReturnsEmpty) {
  const auto epub = makeBookRaw("<html><body><p>abc");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2), "");
}

TEST(FindXPathForParagraph, NamespacePrefixesAreStripped) {
  const auto epub = makeBookRaw("<x:html><x:body><x:p>alpha</x:p></x:body></x:html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 1), "/body/DocFragment[1]/body/p[1]");
}

// --- findXPathForProgress --------------------------------------------------

TEST(FindXPathForProgress, ZeroOrNegativeProgressReturnsBodyBase) {
  const auto epub = makeBook("<p>0123456789</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.0f), "/body/DocFragment[1]/body");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, -0.25f), "/body/DocFragment[1]/body");
}

TEST(FindXPathForProgress, MidProgressResolvesCharOffset) {
  const auto epub = makeBook("<p>0123456789</p>");
  // 10 visible chars, target = ceil(0.5 * 10) = 5.
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "/body/DocFragment[1]/body/p[1]/text()[1].5");
}

TEST(FindXPathForProgress, FullProgressResolvesLastChar) {
  const auto epub = makeBook("<p>0123456789</p><p>abcdefghij</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[2]/text()[1].10");
}

TEST(FindXPathForProgress, TinyProgressClampsToFirstChar) {
  const auto epub = makeBook("<p>0123456789</p>");
  // target = max(1, ceil(0.001 * 10)) = 1.
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.001f), "/body/DocFragment[1]/body/p[1]/text()[1].1");
}

TEST(FindXPathForProgress, ProgressAboveOneClampsToEnd) {
  const auto epub = makeBook("<p>0123456789</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 2.0f), "/body/DocFragment[1]/body/p[1]/text()[1].10");
}

TEST(FindXPathForProgress, TargetAtChunkBoundaryResolvesInclusively) {
  // Two paragraphs of 3 chars each; progress 0.5 -> target char 3, which is
  // exactly the last char of p[1]. The boundary is inclusive: the position
  // belongs to the chunk that ends there, not to the start of p[2].
  const auto epub = makeBook("<p>abc</p><p>def</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(FindXPathForProgress, InlineElementSplitsTextNodes) {
  // "abc"(1st text node of p) "def"(inside b) "ghi"(2nd text node of p).
  // 9 visible chars; progress 0.8 -> target = ceil(7.2) = 8, i.e. the 2nd
  // char inside "ghi".
  const auto epub = makeBook("<p>abc<b>def</b>ghi</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.8f), "/body/DocFragment[1]/body/p[1]/text()[2].2");
}

TEST(FindXPathForProgress, TextOutsideParagraphsIsNotCounted) {
  const auto epub = makeBook("ignored text<p>abcde</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].5");
}

TEST(FindXPathForProgress, EmptyBodyReturnsEmpty) {
  const auto epub = makeBook("");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "");
}

TEST(FindXPathForProgress, TrailingListTextIsOutsideTheProgressScale) {
  // Current behavior on both this branch and upstream develop: the total
  // visible count includes only <p> text, while the position scan would also
  // count <li> text. 100% therefore resolves to the end of the last <p>,
  // never inside a trailing list.
  const auto epub = makeBook("<p>aaaa</p><ul><li>bbbb</li></ul>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].4");
}

TEST(FindXPathForProgress, CountsRubyFallbackTextAsVisible) {
  // Documents current limitation, fixed upstream (develop skips head/style/
  // script/title/rp via VisibleTextUtils as part of the #3174 precise-position
  // work): <rp> ruby fallback text is counted as visible here, so mid-chapter
  // progress can resolve INSIDE the invisible <rp> element. Upstream develop
  // counts only "abcd" and resolves 0.5 to p[1]/text()[1].2.
  const auto epub = makeBook("<p>ab<rp>XX</rp>cd</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f),
            "/body/DocFragment[1]/body/p[1]/rp[1]/text()[1].1");
}

TEST(FindXPathForProgress, CommentDoesNotSplitTextNodes) {
  // Documents current limitation, fixed upstream (develop registers comment/
  // PI/CDATA handlers as part of the #3174 precise-position work): a comment
  // between two text runs does not start a new text node here, so the whole
  // paragraph reads as text()[1]. KOReader's DOM (and upstream develop) treats
  // the runs as separate nodes: text()[2].3.
  const auto epub = makeBook("<p>abc<!--x-->def</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].6");
}

TEST(FindXPathForProgress, CountsCodepointsNotBytes) {
  const auto epub = makeBook("<p>\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82</p>");  // "привет"
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(FindXPathForProgress, NumericCharacterReferenceCountsAsOneCodepoint) {
  const auto epub = makeBook("<p>a&#233;b</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(FindXPathForProgress, PredefinedEntityCountsAsOneCodepoint) {
  const auto epub = makeBook("<p>a&amp;b</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(FindXPathForProgress, UndefinedEntityFailsGracefully) {
  // In-tree expat is built with XML_GE=0 and no DTD: &nbsp; is an undefined
  // entity, the parse errors out, and the resolver reports failure instead of
  // a bogus position.
  const auto epub = makeBook("<p>a&nbsp;b</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "");
}

TEST(FindXPathForProgress, DeeplyNestedAncestryResolves) {
  std::string content;
  std::string expected = "/body/DocFragment[1]/body";
  for (int i = 0; i < 40; i++) {
    content += "<div>";
    expected += "/div[1]";
  }
  content += "<p>abcd</p>";
  expected += "/p[1]/text()[1].4";
  for (int i = 0; i < 40; i++) content += "</div>";
  const auto epub = makeBook(content);
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), expected);
}

TEST(FindXPathForProgress, LargeChapterResolvesExactOffset) {
  const auto epub = makeBook("<p>" + std::string(50000, 'a') + "</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f),
            "/body/DocFragment[1]/body/p[1]/text()[1].25000");
}

TEST(FindXPathForProgress, MalformedTailFailsBecauseCountingNeedsWholeChapter) {
  // Unlike findXPathForParagraph (which stops at the match), progress
  // resolution first counts ALL visible chars, so a malformed tail fails the
  // whole resolution even when the target would land before the damage.
  const auto epub = makeBookRaw("<html><body><p>0123456789</p><p>abc\x01");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.3f), "");
}

TEST(FindXPathForProgress, GarbageContentReturnsEmpty) {
  const auto epub = makeBookRaw("\x01\x02 binary \xFF junk");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f), "");
}

TEST(FindXPathForProgress, RejectsInvalidArguments) {
  const auto epub = makeBook("<p>alpha</p>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(nullptr, 0, 0.5f), "");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, -1, 0.5f), "");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 1, 0.5f), "");
}

TEST(FindXPathForProgress, EmptyHrefReturnsEmptyEvenAtZeroProgress) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("", "<html><body><p>alpha</p></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.0f), "");
}
