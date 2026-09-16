// Host tests for src/util/DictHtmlPages.cpp — StarDict HTML -> XHTML
// normalisation (asserted on the staged file the parser reads back) and the
// entry / retain heap, page-count and element-count gates that decide whether
// a styled layout is kept or the caller falls back to plain text.

#include <Epub/parsers/ChapterHtmlSlimParser.h>  // stub: dhtstub controls
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "PlatformHost.h"
#include "src/util/DictHtmlPages.h"

// Forward-declared by production and only ever passed through by reference.
class GfxRenderer {};

namespace {

namespace fs = std::filesystem;

constexpr size_t KB = 1024;
constexpr const char* WRAP_OPEN = "<html><body>";
constexpr const char* WRAP_CLOSE = "</body></html>";

class DictHtmlPagesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root = fs::absolute(std::string("sandbox_dhtml_") + info->name());
    fs::remove_all(root);
    fs::create_directories(root / ".crosspoint");
    dictstub::storageRoot = root.string();
    dhtstub::reset();
    platform_host::setHeap(0, 0);
  }

  void TearDown() override {
    platform_host::setHeap(0, 0);
    dhtstub::reset();
    dictstub::storageRoot.clear();
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  bool build(const std::string& definition) { return buildDictionaryHtmlPages(renderer, definition, 400, 700, pages); }

  // Normalise through the real writer and return the staged document.
  std::string normalized(const std::string& definition) {
    dhtstub::pagePlan = {1};
    EXPECT_TRUE(build(definition));
    return dhtstub::stagedHtml;
  }

  // The staged document minus the fixed wrapper.
  std::string body(const std::string& definition) {
    const std::string doc = normalized(definition);
    const size_t openLen = std::string(WRAP_OPEN).size();
    const size_t closeLen = std::string(WRAP_CLOSE).size();
    EXPECT_EQ(doc.compare(0, openLen, WRAP_OPEN), 0) << doc;
    EXPECT_GE(doc.size(), openLen + closeLen);
    EXPECT_EQ(doc.compare(doc.size() - closeLen, closeLen, WRAP_CLOSE), 0) << doc;
    return doc.substr(openLen, doc.size() - openLen - closeLen);
  }

  bool stagedFileExists() const { return fs::exists(root / ".crosspoint" / "dicthtml.tmp"); }

  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  fs::path root;
};

// ---------------------------------------------------------------------------
// Normalisation
// ---------------------------------------------------------------------------

TEST_F(DictHtmlPagesTest, EmptyDefinitionIsJustTheWrapper) {
  EXPECT_EQ(normalized(""), std::string(WRAP_OPEN) + WRAP_CLOSE);
}

TEST_F(DictHtmlPagesTest, PlainTextAndUtf8PassThrough) {
  const std::string text =
      "h\xC3\xA9llo \xE2\x80\x94 \xC2\xAB"
      "x\xC2\xBB \xE4\xB8\xAD";
  EXPECT_EQ(body(text), text);
}

TEST_F(DictHtmlPagesTest, TagNamesAreLowercasedAttributesKeptVerbatim) {
  EXPECT_EQ(body("<B>x</B>"), "<b>x</b>");
  EXPECT_EQ(body("<Div Class=\"Note\" ID='K'>y</DIV>"), "<div Class=\"Note\" ID='K'>y</div>");
}

TEST_F(DictHtmlPagesTest, VoidElementsAreSelfClosed) {
  EXPECT_EQ(body("a<br>b"), "a<br/>b");
  EXPECT_EQ(body("<BR>"), "<br/>");
  EXPECT_EQ(body("<hr>"), "<hr/>");
  EXPECT_EQ(body("<img src=\"x.png\">"), "<img src=\"x.png\"/>");
  EXPECT_EQ(body("<Br Class=\"x\">"), "<br Class=\"x\"/>");
}

TEST_F(DictHtmlPagesTest, EveryVoidElementNameIsSelfClosed) {
  for (const char* name : {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param",
                           "source", "track", "wbr"}) {
    EXPECT_EQ(body(std::string("<") + name + ">"), std::string("<") + name + "/>") << name;
  }
}

TEST_F(DictHtmlPagesTest, AlreadySelfClosedVoidIsNotDoubled) {
  EXPECT_EQ(body("<br/>"), "<br/>");
  EXPECT_EQ(body("<br />"), "<br />");
  EXPECT_EQ(body("<img src=\"a/\">"), "<img src=\"a/\"/>");  // slash inside a value does not count
}

TEST_F(DictHtmlPagesTest, StrayVoidClosersAreDroppedNonVoidClosersKept) {
  EXPECT_EQ(body("a</br>b"), "ab");
  EXPECT_EQ(body("a</BR>b"), "ab");
  EXPECT_EQ(body("a</hr >b"), "ab");
  EXPECT_EQ(body("<p>a</p>"), "<p>a</p>");
}

TEST_F(DictHtmlPagesTest, CommentsDoctypesAndProcessingInstructionsAreDropped) {
  EXPECT_EQ(body("a<!-- x <b> & -->b"), "ab");
  EXPECT_EQ(body("<!DOCTYPE html>a<?xml version=\"1.0\"?>b"), "ab");
  EXPECT_EQ(body("a<![CDATA[x]]>b"), "ab");
}

TEST_F(DictHtmlPagesTest, UnterminatedCommentOrBangDropsTheRest) {
  EXPECT_EQ(body("a<!-- x"), "a");
  EXPECT_EQ(body("a<!x"), "a");
  EXPECT_EQ(body("a<?pi"), "a");
  EXPECT_EQ(body("a<!-- x > y"), "a");  // '>' alone does not end a comment
}

TEST_F(DictHtmlPagesTest, StrayLessThanIsEscaped) {
  EXPECT_EQ(body("x < y"), "x &lt; y");
  EXPECT_EQ(body("<1>"), "&lt;1>");
  EXPECT_EQ(body("<>"), "&lt;>");
  EXPECT_EQ(body("a<"), "a&lt;");
  EXPECT_EQ(body("< b>"), "&lt; b>");
}

TEST_F(DictHtmlPagesTest, UnterminatedTagIsLiteralText) {
  EXPECT_EQ(body("a<b"), "a&lt;b");
  EXPECT_EQ(body("<b class=\"x"), "&lt;b class=\"x");
  EXPECT_EQ(body("</"), "&lt;/");
}

TEST_F(DictHtmlPagesTest, QuotedGreaterThanInsideAttributesIsHonoured) {
  EXPECT_EQ(body("<A title=\"x>y\">z</A>"), "<a title=\"x>y\">z</a>");
  EXPECT_EQ(body("<a title='x>y'>z</a>"), "<a title='x>y'>z</a>");
  EXPECT_EQ(body("<a t=\"it's\">z</a>"), "<a t=\"it's\">z</a>");  // other quote inside a value
}

TEST_F(DictHtmlPagesTest, BareAmpersandsAreEscaped) {
  EXPECT_EQ(body("Tom & Jerry"), "Tom &amp; Jerry");
  EXPECT_EQ(body("a&"), "a&amp;");
  EXPECT_EQ(body("&&"), "&amp;&amp;");
  EXPECT_EQ(body("& amp;"), "&amp; amp;");
}

TEST_F(DictHtmlPagesTest, WellFormedEntityReferencesAreKept) {
  const std::string refs = "&amp; &lt; &gt; &nbsp; &#123; &#x1F; &#X1f; &eacute;";
  EXPECT_EQ(body(refs), refs);
}

TEST_F(DictHtmlPagesTest, MalformedEntityReferencesAreEscaped) {
  EXPECT_EQ(body("&#;"), "&amp;#;");
  EXPECT_EQ(body("&#x;"), "&amp;#x;");
  EXPECT_EQ(body("&#xZ;"), "&amp;#xZ;");
  EXPECT_EQ(body("&foo"), "&amp;foo");
  EXPECT_EQ(body("&;"), "&amp;;");
  EXPECT_EQ(body("&foo bar;"), "&amp;foo bar;");
}

TEST_F(DictHtmlPagesTest, EntityInsideAttributeIsTreatedLikeText) {
  // Attributes are copied verbatim, so an unescaped '&' in a value survives
  // (pinned: expat would then reject the document and plain text is used).
  EXPECT_EQ(body("<a href=\"?a=1&b=2\">x</a>"), "<a href=\"?a=1&b=2\">x</a>");
}

TEST_F(DictHtmlPagesTest, TagNameIsTruncatedAtFifteenCharacters) {
  // Pinned: the name buffer holds 15 characters; the overflow is dropped.
  EXPECT_EQ(body("<abcdefghijklmnopqrstu>"), "<abcdefghijklmno>");
  EXPECT_EQ(body("</abcdefghijklmnopqrstu>"), "</abcdefghijklmno>");
}

TEST_F(DictHtmlPagesTest, TagNameStopsAtTheFirstNonAlphanumeric) {
  EXPECT_EQ(body("<B-x>"), "<b-x>");
  EXPECT_EQ(body("<H1>t</H1>"), "<h1>t</h1>");
  EXPECT_EQ(body("<P\n>t</P\t>"), "<p\n>t</p\t>");
}

TEST_F(DictHtmlPagesTest, MixedMarkupEndToEnd) {
  EXPECT_EQ(body("<P>Tom & Jerry<BR>x < y &amp; z</BR></P><!-- c --><HR>"),
            "<p>Tom &amp; Jerry<br/>x &lt; y &amp; z</p><hr/>");
}

TEST_F(DictHtmlPagesTest, LargeDefinitionSurvivesWriterFlushBoundaries) {
  // 5000+ bytes crosses the 128-byte staging buffer many times, with tags,
  // entities and stray characters landing on the boundaries.
  std::string in, expected;
  for (int i = 0; i < 200; i++) {
    in += "<B>word</B> & x<BR>&amp;";
    expected += "<b>word</b> &amp; x<br/>&amp;";
  }
  EXPECT_EQ(body(in), expected);
}

// ---------------------------------------------------------------------------
// Entry gate, staging file, parser outcome
// ---------------------------------------------------------------------------

TEST_F(DictHtmlPagesTest, EntryGateRefusesLowFreeHeapBeforeStaging) {
  platform_host::setHeap(40 * KB - 1, 0);
  dhtstub::pagePlan = {1};
  EXPECT_FALSE(build("<b>x</b>"));
  EXPECT_EQ(dhtstub::parsersConstructed, 0);
  EXPECT_FALSE(stagedFileExists());
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, EntryGateRefusesSmallLargestBlock) {
  platform_host::setHeap(0, 20 * KB - 1);
  dhtstub::pagePlan = {1};
  EXPECT_FALSE(build("<b>x</b>"));
  EXPECT_EQ(dhtstub::parsersConstructed, 0);
}

TEST_F(DictHtmlPagesTest, EntryGatePassesExactlyAtThreshold) {
  platform_host::setHeap(40 * KB, 20 * KB);
  dhtstub::pagePlan = {1};
  EXPECT_TRUE(build("<b>x</b>"));
  ASSERT_EQ(pages.size(), 1u);
}

TEST_F(DictHtmlPagesTest, StagedFileIsRemovedAfterSuccessAndFailure) {
  dhtstub::pagePlan = {1};
  EXPECT_TRUE(build("x"));
  EXPECT_FALSE(stagedFileExists());

  dhtstub::parseResult = false;
  EXPECT_FALSE(build("x"));
  EXPECT_FALSE(stagedFileExists());
}

TEST_F(DictHtmlPagesTest, UnwritableStagingPathFallsBack) {
  fs::remove_all(root / ".crosspoint");
  dhtstub::pagePlan = {1};
  EXPECT_FALSE(build("x"));
  EXPECT_EQ(dhtstub::parsersConstructed, 0);
}

TEST_F(DictHtmlPagesTest, ParseFailureDiscardsCollectedPages) {
  dhtstub::pagePlan = {3, 4};
  dhtstub::parseResult = false;
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, NoPagesEmittedIsAFailure) {
  dhtstub::pagePlan = {};
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, PagesAreRetainedInOrder) {
  pages.push_back(std::make_unique<Page>());  // stale content is replaced
  dhtstub::pagePlan = {3, 5, 2};
  EXPECT_TRUE(build("x"));
  ASSERT_EQ(pages.size(), 3u);
  EXPECT_EQ(pages[0]->elements.size(), 3u);
  EXPECT_EQ(pages[1]->elements.size(), 5u);
  EXPECT_EQ(pages[2]->elements.size(), 2u);
}

// ---------------------------------------------------------------------------
// Retain gates: page count, element count, heap
// ---------------------------------------------------------------------------

TEST_F(DictHtmlPagesTest, SixtyFourPagesAreTheCeiling) {
  dhtstub::pagePlan.assign(64, 1);
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 64u);

  dhtstub::pagePlan.assign(65, 1);
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, ElementBudgetIs512AcrossAllPages) {
  dhtstub::pagePlan = {512};
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 1u);

  dhtstub::pagePlan = {513};
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());

  dhtstub::pagePlan = {256, 256};
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 2u);

  dhtstub::pagePlan = {500, 12, 1};
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, EmptyPagesCostNoElements) {
  dhtstub::pagePlan.assign(64, 0);
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 64u);
}

TEST_F(DictHtmlPagesTest, RetainGateStopsOnLowFreeHeap) {
  dhtstub::pagePlan = {1, 1, 1};
  dhtstub::beforePage = [](size_t i) {
    if (i == 1) platform_host::setHeap(16 * KB - 1, 0);
  };
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, RetainGateStopsOnSmallLargestBlock) {
  dhtstub::pagePlan = {1, 1};
  dhtstub::beforePage = [](size_t i) {
    if (i == 1) platform_host::setHeap(0, 8 * KB - 1);
  };
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, RetainGatePassesExactlyAtThreshold) {
  dhtstub::pagePlan = {1, 1};
  dhtstub::beforePage = [](size_t i) {
    if (i == 1) platform_host::setHeap(16 * KB, 8 * KB);
  };
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 2u);
}

TEST_F(DictHtmlPagesTest, RetainGateIsLowerThanTheEntryGate) {
  // Mid-layout the parser's working set is live: a heap that would fail the
  // entry gate must still let pages be kept.
  dhtstub::pagePlan = {1, 1, 1};
  dhtstub::beforePage = [](size_t i) {
    if (i == 1) platform_host::setHeap(30 * KB, 15 * KB);
  };
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 3u);
}

TEST_F(DictHtmlPagesTest, RetainGateIsCheckedOnTheFirstPageToo) {
  dhtstub::pagePlan = {1};
  dhtstub::beforePage = [](size_t) { platform_host::setHeap(16 * KB - 1, 0); };
  EXPECT_FALSE(build("x"));
}

TEST_F(DictHtmlPagesTest, HeapRecoveringAfterALimitDoesNotResumeRetention) {
  // Once a limit fires, later pages are ignored even if the heap comes back.
  dhtstub::pagePlan = {1, 1, 1};
  dhtstub::beforePage = [](size_t i) {
    if (i == 1) platform_host::setHeap(16 * KB - 1, 0);
    if (i == 2) platform_host::setHeap(0, 0);
  };
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, EmbeddedNulIsStagedVerbatim) {
  // Multi-type StarDict entries carry NUL separators. writeNormalizedXhtml
  // walks the std::string by size, so the NUL is staged like any other byte.
  const std::string def("a\0<B>b</B>", 10);
  const std::string out = body(def);
  ASSERT_EQ(out.size(), 10u);
  EXPECT_EQ(out, std::string("a\0<b>b</b>", 10));
}

TEST_F(DictHtmlPagesTest, InvalidUtf8BytesPassThrough) {
  // Normalisation is byte-oriented: broken sequences are neither repaired nor
  // dropped (expat rejects the document later and plain text is used).
  EXPECT_EQ(body("\x80\xBF"), "\x80\xBF");          // lone continuation bytes
  EXPECT_EQ(body("\xFF\xFE"), "\xFF\xFE");          // never-valid lead bytes
  EXPECT_EQ(body("a\xE2\x80"), "a\xE2\x80");        // truncated 3-byte sequence
  EXPECT_EQ(body("<b>\xC3(</b>"), "<b>\xC3(</b>");  // truncated inside a tag
  EXPECT_EQ(body("\xED\xA0\x80"), "\xED\xA0\x80");  // surrogate half
}

TEST_F(DictHtmlPagesTest, HighBytesAreNotTreatedAsTagNames) {
  // isalpha() is called on unsigned char, so a high byte never opens a tag.
  EXPECT_EQ(body("<\xC3\xA9>"), "&lt;\xC3\xA9>");
  EXPECT_EQ(body("</\xC3\xA9>"), "</\xC3\xA9>");  // '/' does open one; the name is empty
}

TEST_F(DictHtmlPagesTest, ClosingTagWithNoNameKeepsTheSlash) {
  // Pinned: "</>" has no name to lowercase and no void match, so it is copied
  // through as an (invalid) empty close tag rather than dropped.
  EXPECT_EQ(body("a</>b"), "a</>b");
  EXPECT_EQ(body("</ >"), "</ >");
}

TEST_F(DictHtmlPagesTest, ControlCharactersAndWhitespaceSurvive) {
  EXPECT_EQ(body("a\tb\r\nc"), "a\tb\r\nc");
  EXPECT_EQ(body("\x01\x1F"), "\x01\x1F");
}

TEST_F(DictHtmlPagesTest, LongAttributeValueIsCopiedVerbatimAcrossFlushes) {
  const std::string value(1000, 'v');
  EXPECT_EQ(body("<a href=\"" + value + "\">x</a>"), "<a href=\"" + value + "\">x</a>");
}

TEST_F(DictHtmlPagesTest, SixteenKilobyteDefinitionIsStagedWhole) {
  // The activity caps HTML definitions at 16KB; the writer must carry that
  // much through the 128-byte staging buffer without dropping bytes.
  const std::string def(16 * KB, 'x');
  EXPECT_EQ(body(def), def);
}

TEST_F(DictHtmlPagesTest, StagingTruncatesAPreviousLongerDocument) {
  // The staging file is reopened with O_TRUNC; a shorter second definition
  // must not leave the first one's tail behind.
  dhtstub::pagePlan = {1};
  ASSERT_TRUE(build(std::string(4000, 'L')));
  const std::string shortDoc = normalized("s");
  EXPECT_EQ(shortDoc, std::string(WRAP_OPEN) + "s" + WRAP_CLOSE);
}

TEST_F(DictHtmlPagesTest, EntryGateLeavesThePreviousPagesUntouched) {
  // Pinned: the entry gate returns before pagesOut.clear(), so a refused
  // styled layout does not disturb what the caller already holds.
  pages.push_back(std::make_unique<Page>());
  platform_host::setHeap(40 * KB - 1, 0);
  dhtstub::pagePlan = {1};
  EXPECT_FALSE(build("x"));
  EXPECT_EQ(pages.size(), 1u);
}

TEST_F(DictHtmlPagesTest, PageCountCeilingFiresEvenForEmptyPages) {
  // The page cap is checked before the element budget, so a 65th page costs
  // the whole layout even when it carries nothing.
  dhtstub::pagePlan.assign(64, 1);
  dhtstub::pagePlan.push_back(0);
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());
}

TEST_F(DictHtmlPagesTest, DeeplyNestedMarkupIsRewrittenWithoutRecursion) {
  // Hostile nesting: the writer is a flat byte scan, so 2000 open/close pairs
  // cost no stack and come out lowercased, not collapsed or dropped.
  std::string in, expected;
  for (int i = 0; i < 2000; i++) {
    in += "<B>";
    expected += "<b>";
  }
  in += "x";
  expected += "x";
  for (int i = 0; i < 2000; i++) {
    in += "</B>";
    expected += "</b>";
  }
  EXPECT_EQ(body(in), expected);
}

TEST_F(DictHtmlPagesTest, PathologicalUnterminatedMarkupTerminates) {
  // A tag opened with a quote that never closes swallows the rest of the input
  // as one unterminated tag, so only the leading '<' is escaped.
  const std::string open = "<a title=\"" + std::string(4000, 'q');
  EXPECT_EQ(body(open), "&lt;" + open.substr(1));

  // 4000 bare '<' bytes each escape independently.
  const std::string angles(4000, '<');
  std::string escaped;
  for (int i = 0; i < 4000; i++) escaped += "&lt;";
  EXPECT_EQ(body(angles), escaped);
}

TEST_F(DictHtmlPagesTest, LongRunOfCommentsAndAmpersandsIsDropped) {
  // Comment scanning uses find(), not a per-byte state machine: a long run of
  // adjacent comments and bare ampersands must still normalise exactly.
  std::string in, expected;
  for (int i = 0; i < 500; i++) {
    in += "<!-- drop <b> & me -->&";
    expected += "&amp;";
  }
  EXPECT_EQ(body(in), expected);
}

TEST_F(DictHtmlPagesTest, ElementBudgetCountsTheOfferedPageNotJustRetained) {
  // 511 retained + a 2-element page overflows, even though 511 < 512.
  dhtstub::pagePlan = {511, 2};
  EXPECT_FALSE(build("x"));
  EXPECT_TRUE(pages.empty());

  dhtstub::pagePlan = {511, 1};
  EXPECT_TRUE(build("x"));
  EXPECT_EQ(pages.size(), 2u);
}

}  // namespace
