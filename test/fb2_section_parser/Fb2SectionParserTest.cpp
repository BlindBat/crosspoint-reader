#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>
#include <expat.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#define class struct
#define private public
#include "Fb2/Fb2SectionParser.h"
#undef private
#undef class

#include "CollectingParser.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fixturePath;

// Collect the text of every PageLine word across a set of pages.
std::vector<std::string> collectWords(const std::vector<std::unique_ptr<Page>>& pages) {
  std::vector<std::string> words;
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        words.emplace_back(block->wordText(i));
      }
    }
  }
  return words;
}

bool containsWord(const std::vector<std::string>& words, const std::string& needle) {
  for (const auto& word : words) {
    if (word == needle) return true;
  }
  return false;
}

// Find the TextBlock of the first line containing `needle`, or nullptr.
std::shared_ptr<TextBlock> findLineWith(const std::vector<std::unique_ptr<Page>>& pages, const std::string& needle) {
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        if (needle == block->wordText(i)) return block;
      }
    }
  }
  return nullptr;
}

int styleOfWord(const std::vector<std::unique_ptr<Page>>& pages, const std::string& needle) {
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        if (needle == block->wordText(i)) return block->wordStyle(i);
      }
    }
  }
  return -1;
}

constexpr uint16_t kViewportWidth = 400;
constexpr uint16_t kViewportHeight = 64;  // 4 lines of 16 px per page

ReaderRenderSpec makeSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 0;
  spec.viewportWidth = kViewportWidth;
  spec.viewportHeight = kViewportHeight;
  return spec;
}

// Collects finished pages behind an Fb2PageCompleteFn (function pointer + context).
struct PageSink {
  std::vector<std::unique_ptr<Page>>* pages;

  Fb2PageCompleteFn fn() { return {&PageSink::append, this}; }

  static void append(void* ctx, std::unique_ptr<Page> page) {
    static_cast<PageSink*>(ctx)->pages->push_back(std::move(page));
  }
};

// Drives the real parser against a fixture file and collects finished pages.
struct ParseResult {
  std::vector<std::unique_ptr<Page>> pages;
  bool ok = false;
};

ParseResult parseSection(const std::string& path, int targetSectionIndex, GfxRenderer& renderer,
                         const ReaderRenderSpec& spec) {
  ParseResult result;
  PageSink sink{&result.pages};
  const std::string filepath = path;
  Fb2SectionParser parser(filepath, 0, targetSectionIndex, renderer, spec, sink.fn());
  result.ok = parser.parseAndBuildPages();
  return result;
}

// ---------------------------------------------------------------------------
// Handler-driven tests: feed SAX events directly and inspect the ParsedText
// block under construction (same pattern as ChapterHtmlSlimParserTest).
// ---------------------------------------------------------------------------

class Fb2SectionParserHandlerTest : public ::testing::Test {
 protected:
  std::string filepath = "unused.fb2";
  GfxRenderer renderer;
  ReaderRenderSpec spec = makeSpec();
  std::vector<std::unique_ptr<Page>> pages;
  PageSink sink{&pages};
  Fb2SectionParser parser{filepath, 0, -1, renderer, spec, sink.fn()};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(false, false, false, BlockStyle()); }

  void start(const char* tag) { Fb2SectionParser::startElement(&parser, tag, nullptr); }
  void end(const char* tag) { Fb2SectionParser::endElement(&parser, tag); }
  void text(const std::string& s) { Fb2SectionParser::characterData(&parser, s.data(), static_cast<int>(s.size())); }
};

TEST_F(Fb2SectionParserHandlerTest, ParagraphSplitsTextIntoWords) {
  start("p");
  text("Hello brave world");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 3u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "Hello");
  EXPECT_EQ(parser.currentTextBlock->words[1], "brave");
  EXPECT_EQ(parser.currentTextBlock->words[2], "world");
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);
}

TEST_F(Fb2SectionParserHandlerTest, EmphasisAppliesItalicOnlyToItsOwnWords) {
  start("p");
  text("one ");
  start("emphasis");
  text("two");
  end("emphasis");
  text(" three");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 3u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::ITALIC);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(2), EpdFontFamily::REGULAR);
}

TEST_F(Fb2SectionParserHandlerTest, StrongAppliesBold) {
  start("p");
  start("strong");
  text("shout");
  end("strong");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::BOLD);
}

TEST_F(Fb2SectionParserHandlerTest, NestedStrongEmphasisCombineToBoldItalic) {
  start("p");
  start("strong");
  start("emphasis");
  text("both");
  end("emphasis");
  end("strong");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::BOLD_ITALIC);
}

TEST_F(Fb2SectionParserHandlerTest, MidWordStyleChangeAttachesContinuationWord) {
  start("p");
  text("cross");
  start("strong");
  text("point");
  end("strong");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "cross");
  EXPECT_EQ(parser.currentTextBlock->words[1], "point");
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::BOLD);
  // The two halves must stay attached when the line breaks.
  EXPECT_FALSE(parser.currentTextBlock->wordContinues[0]);
  EXPECT_TRUE(parser.currentTextBlock->wordContinues[1]);
}

TEST_F(Fb2SectionParserHandlerTest, TitleBlockIsCenteredBeforeInnerParagraph) {
  start("title");
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Center);
}

TEST_F(Fb2SectionParserHandlerTest, SubtitleIsCenteredItalic) {
  start("subtitle");
  text("subline");
  end("subtitle");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Center);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::ITALIC);
}

TEST_F(Fb2SectionParserHandlerTest, EmptyLineForcesLineHeightTopMargin) {
  start("p");
  text("above");
  end("p");
  start("empty-line");
  end("empty-line");
  // empty-line starts a new (empty) block whose marginTop is one line height.
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginTop, GfxRenderer::kLineHeight);
}

TEST_F(Fb2SectionParserHandlerTest, EpigraphKeepsItalicAndRightAlignment) {
  start("epigraph");
  text("wisdom");
  end("epigraph");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Right);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 30);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::ITALIC);
}

TEST_F(Fb2SectionParserHandlerTest, CiteIsIndented) {
  start("cite");
  text("quoted");
  end("cite");
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 20);
}

// Real FB2 files always wrap container text in <p>; the container's block
// styles (alignment, indents) must survive into those inner paragraphs.
TEST_F(Fb2SectionParserHandlerTest, InnerParagraphInheritsContainerBlockStyle) {
  start("epigraph");
  start("p");
  text("wisdom");
  end("p");
  end("epigraph");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Right);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 30);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::ITALIC);
}

TEST_F(Fb2SectionParserHandlerTest, TitleInnerParagraphStaysCenteredBold) {
  start("title");
  start("p");
  text("Chapter");
  end("p");
  end("title");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Center);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::BOLD);
}

TEST_F(Fb2SectionParserHandlerTest, EverySiblingParagraphInCiteKeepsTheIndent) {
  start("cite");
  start("p");
  text("first");
  end("p");
  start("p");
  text("second");
  end("p");
  end("cite");
  // The second <p> lives in a fresh block (the first was flushed to pages);
  // it must still carry the cite indent, not just the first child.
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "second");
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 20);
}

TEST_F(Fb2SectionParserHandlerTest, ContainerStyleDoesNotLeakPastItsClose) {
  start("epigraph");
  start("p");
  text("wisdom");
  end("p");
  end("epigraph");
  start("p");
  text("plain");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "plain");
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Justify);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 0);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);
}

TEST_F(Fb2SectionParserHandlerTest, NestedContainersAccumulateIndents) {
  start("epigraph");  // marginLeft 30, align Right
  start("cite");      // marginLeft 20
  start("p");
  text("deep");
  end("p");
  end("cite");
  end("epigraph");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().marginLeft, 50);
  // cite defines no alignment of its own, so the epigraph's survives.
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().alignment, CssTextAlign::Right);
}

TEST_F(Fb2SectionParserHandlerTest, UnknownElementIsIgnoredButTextFlows) {
  start("p");
  text("before ");
  start("blink");
  text("inside");
  end("blink");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->words[1], "inside");
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::REGULAR);
}

TEST_F(Fb2SectionParserHandlerTest, ImageBecomesPlaceholderAndContentIsSkipped) {
  start("p");
  start("image");
  text("must-not-appear");
  end("image");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "[Image]");
}

TEST_F(Fb2SectionParserHandlerTest, TableIsReplacedByOmissionPlaceholder) {
  start("table");
  start("tr");
  start("td");
  text("cell");
  end("td");
  end("tr");
  end("table");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "[Table");
  EXPECT_EQ(parser.currentTextBlock->words[1], "omitted]");
  EXPECT_FALSE(containsWord({parser.currentTextBlock->words.begin(), parser.currentTextBlock->words.end()}, "cell"));
}

TEST_F(Fb2SectionParserHandlerTest, Utf8BomBytesAreStripped) {
  start("p");
  text("\xEF\xBB\xBFword");
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "word");
}

TEST_F(Fb2SectionParserHandlerTest, OverlongWordIsSplitAtBufferLimit) {
  std::string longWord(FB2_MAX_WORD_SIZE + 50, 'x');
  start("p");
  text(longWord);
  end("p");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->words[0].size(), static_cast<size_t>(FB2_MAX_WORD_SIZE));
  EXPECT_EQ(parser.currentTextBlock->words[1].size(), 50u);
}

TEST_F(Fb2SectionParserHandlerTest, DescriptionContentIsSkipped) {
  start("description");
  start("book-title");
  text("MetadataTitle");
  end("book-title");
  end("description");
  start("body");
  start("p");
  text("bodyword");
  end("p");
  EXPECT_FALSE(
      containsWord({parser.currentTextBlock->words.begin(), parser.currentTextBlock->words.end()}, "MetadataTitle"));
  EXPECT_TRUE(containsWord({parser.currentTextBlock->words.begin(), parser.currentTextBlock->words.end()}, "bodyword"));
}

// ---------------------------------------------------------------------------
// File-driven tests: full parseAndBuildPages() runs over fixture files.
// ---------------------------------------------------------------------------

TEST(Fb2SectionParserFile, TargetSectionZeroKeepsOtherSectionsOut) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  ASSERT_FALSE(result.pages.empty());
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "plainword"));
  EXPECT_TRUE(containsWord(words, "closingword"));
  EXPECT_FALSE(containsWord(words, "decoyword"));
}

TEST(Fb2SectionParserFile, TargetSectionOneGetsOnlyItsOwnText) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "decoyword"));
  EXPECT_FALSE(containsWord(words, "plainword"));
}

TEST(Fb2SectionParserFile, StylesSurviveLayoutIntoPageLines) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(styleOfWord(result.pages, "italicword"), EpdFontFamily::ITALIC);
  EXPECT_EQ(styleOfWord(result.pages, "boldword"), EpdFontFamily::BOLD);
  EXPECT_EQ(styleOfWord(result.pages, "bolditalicword"), EpdFontFamily::BOLD_ITALIC);
  EXPECT_EQ(styleOfWord(result.pages, "subtitleword"), EpdFontFamily::ITALIC);
  EXPECT_EQ(styleOfWord(result.pages, "epigraphword"), EpdFontFamily::ITALIC);
  EXPECT_EQ(styleOfWord(result.pages, "plainword"), EpdFontFamily::REGULAR);
}

TEST(Fb2SectionParserFile, VerseLinesStayCentered) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto verse = findLineWith(result.pages, "verseline");
  ASSERT_NE(verse, nullptr);
  EXPECT_EQ(verse->getBlockStyle().alignment, CssTextAlign::Center);
}

TEST(Fb2SectionParserFile, UnknownElementsAndPlaceholdersFlowThroughWholeFile) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "unknownword"));
  EXPECT_TRUE(containsWord(words, "[Image]"));
  EXPECT_TRUE(containsWord(words, "[Table"));
  EXPECT_FALSE(containsWord(words, "cellword"));
}

// Contract C3: a chapter renders its section's OWN direct content. The nested
// child is a chapter of its own, so its text must NOT appear in the parent's.
TEST(Fb2SectionParserFile, NestedChildContentIsExcludedFromTheParentChapter) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("nested-sections.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "Outer"));       // the parent's own paragraphs
  EXPECT_FALSE(containsWord(words, "innerword"));  // belongs to chapter 1
  EXPECT_FALSE(containsWord(words, "level"));      // "Second top level paragraph." is chapter 2
}

// Section indices must match Fb2MetadataParser's numbering, which counts EVERY
// section in document-start order. Index 1 of nested-sections.fb2 is therefore
// the nested "Inner Chapter", and index 2 is "Part Two".
TEST(Fb2SectionParserFile, NestedSectionsAreAddressableByTheirOwnIndex) {
  GfxRenderer renderer;
  auto inner = parseSection(fixturePath("nested-sections.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(inner.ok);
  const auto innerWords = collectWords(inner.pages);
  EXPECT_TRUE(containsWord(innerWords, "innerword"));
  EXPECT_FALSE(containsWord(innerWords, "Outer"));

  auto second = parseSection(fixturePath("nested-sections.fb2"), 2, renderer, makeSpec());
  ASSERT_TRUE(second.ok);
  const auto secondWords = collectWords(second.pages);
  EXPECT_TRUE(containsWord(secondWords, "level"));  // "Second top level paragraph."
  EXPECT_FALSE(containsWord(secondWords, "innerword"));
}

TEST(Fb2SectionParserFile, IndexPastTheLastChapterRendersNothing) {
  // nested-sections.fb2 has exactly 3 chapters (0, 1, 2); index 3 selects none.
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("nested-sections.fb2"), 3, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(collectWords(result.pages).empty());
}

// Contract C3 over three levels: each chapter carries only its own marker word.
TEST(Fb2SectionParserFile, DeepNestingKeepsEachLevelInItsOwnChapter) {
  GfxRenderer renderer;
  const char* markers[] = {"levelzeroword", "leveloneword", "leveltwoword", "levelthreeword"};
  for (int chapter = 0; chapter < 4; chapter++) {
    auto result = parseSection(fixturePath("nested-deep.fb2"), chapter, renderer, makeSpec());
    ASSERT_TRUE(result.ok) << "chapter " << chapter;
    const auto words = collectWords(result.pages);
    for (int other = 0; other < 4; other++) {
      EXPECT_EQ(containsWord(words, markers[other]), other == chapter)
          << "chapter " << chapter << " vs marker " << markers[other];
    }
  }
  // The <poem> is the first chapter's own content, so its verse stays there.
  auto first = parseSection(fixturePath("nested-deep.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(first.ok);
  EXPECT_TRUE(containsWord(collectWords(first.pages), "poemword"));
}

// Documented ceiling (research.md Decision 1): a parent's own text that follows
// a child section reads with the parent, ahead of the child - never dropped.
TEST(Fb2SectionParserFile, ParentTextAfterAChildStaysInTheParentChapter) {
  GfxRenderer renderer;
  auto parent = parseSection(fixturePath("trailing-parent-text.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(parent.ok);
  const auto parentWords = collectWords(parent.pages);
  EXPECT_TRUE(containsWord(parentWords, "beforeword"));
  EXPECT_TRUE(containsWord(parentWords, "afterword"));
  EXPECT_FALSE(containsWord(parentWords, "childword"));

  auto child = parseSection(fixturePath("trailing-parent-text.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(child.ok);
  const auto childWords = collectWords(child.pages);
  EXPECT_TRUE(containsWord(childWords, "childword"));
  EXPECT_FALSE(containsWord(childWords, "afterword"));
}

// Contract C8: a section whose only content is another section still renders one
// page, so the reader never faces a zero-page chapter.
TEST(Fb2SectionParserFile, WrapperChapterStillProducesOnePage) {
  GfxRenderer renderer;
  auto wrapper = parseSection(fixturePath("wrapper-only.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(wrapper.ok);
  EXPECT_EQ(wrapper.pages.size(), 1u);
  EXPECT_TRUE(collectWords(wrapper.pages).empty());
}

// FR-003/FR-004: a <body> may carry its own <title> and <epigraph> ahead of its
// first <section>. That text is in no section, so it reads with the first
// chapter of its body rather than being dropped.
TEST(Fb2SectionParserFile, BodyOwnContentReadsWithTheFirstChapterOfItsBody) {
  GfxRenderer renderer;
  auto first = parseSection(fixturePath("body-prefix.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(first.ok);
  const auto firstWords = collectWords(first.pages);
  EXPECT_TRUE(containsWord(firstWords, "Bodytitleword"));
  EXPECT_TRUE(containsWord(firstWords, "Bodyepigraphword"));
  EXPECT_TRUE(containsWord(firstWords, "Firstword"));
  EXPECT_FALSE(containsWord(firstWords, "Secondword"));

  auto second = parseSection(fixturePath("body-prefix.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(second.ok);
  const auto secondWords = collectWords(second.pages);
  EXPECT_TRUE(containsWord(secondWords, "Secondword"));
  EXPECT_FALSE(containsWord(secondWords, "Bodytitleword"));
  EXPECT_FALSE(containsWord(secondWords, "Bodyepigraphword"));
}

// The parity + completeness property from contracts/chapter-model.md: the
// renderer and the metadata parser must agree on numbering, and the chapters
// together must contain every reading-body word exactly once.
//
// The numbering-agreement half is what the title check below asserts: chapter i's
// rendered text must contain chapter i's own title, because a section's <title>
// is part of its direct content. Word-multiset equality alone does NOT catch a
// numbering divergence - reverting the renderer to top-level-only counting still
// partitions the body, it just hands back the wrong chapter.
//
// Mutation check (Principle V): adding `&& sectionNesting == 0` to the renderer's
// chapter counter (the pre-change rule) while Fb2MetadataParser keeps the new
// numbering fails this test on the title assertion.
TEST(Fb2SectionParserFile, ChapterTextsPartitionTheBodyForEveryFixture) {
  static const char* kFixtures[] = {
      "basic.fb2",        "nested-sections.fb2", "nested-deep.fb2", "trailing-parent-text.fb2",
      "wrapper-only.fb2", "styles.fb2",          "no-sections.fb2", "long.fb2",
      "notes-body.fb2",   "unicode-titles.fb2",  "body-prefix.fb2", "multi-reading-body.fb2"};
  for (const char* fixture : kFixtures) {
    CollectingParser metadata(fixturePath(fixture));
    ASSERT_TRUE(metadata.parse()) << fixture;
    const auto& sections = metadata.getSections();
    ASSERT_FALSE(sections.empty()) << fixture;

    GfxRenderer renderer;
    std::vector<std::string> all;
    for (size_t i = 0; i < sections.size(); i++) {
      const int target = (sections.size() == 1 && sections[0].fileOffset == 0) ? -1 : static_cast<int>(i);
      auto result = parseSection(fixturePath(fixture), target, renderer, makeSpec());
      ASSERT_TRUE(result.ok) << fixture << " chapter " << i;
      EXPECT_FALSE(result.pages.empty()) << fixture << " chapter " << i << " produced no page";
      const auto words = collectWords(result.pages);

      // Numbering agreement: the chapter the renderer returns for index i must be
      // the chapter the metadata parser described at index i.
      const std::string& title = sections[i].title;
      if (target >= 0 && !title.empty()) {
        const size_t space = title.find(' ');
        const std::string firstTitleWord = space == std::string::npos ? title : title.substr(0, space);
        EXPECT_TRUE(containsWord(words, firstTitleWord))
            << fixture << " chapter " << i << ": rendered text does not contain its own title \"" << title << "\"";
      }

      all.insert(all.end(), words.begin(), words.end());
    }

    // One pass over the whole body, which is what the reader would see if the
    // book were a single chapter: the per-chapter texts must add up to it.
    auto whole = parseSection(fixturePath(fixture), -1, renderer, makeSpec());
    ASSERT_TRUE(whole.ok) << fixture;
    auto wholeWords = collectWords(whole.pages);
    std::sort(all.begin(), all.end());
    std::sort(wholeWords.begin(), wholeWords.end());
    EXPECT_EQ(all, wholeWords) << fixture << ": chapter texts do not partition the reading body";

    // No off-by-one at the end of the numbering.
    auto past = parseSection(fixturePath(fixture), static_cast<int>(sections.size()), renderer, makeSpec());
    ASSERT_TRUE(past.ok) << fixture;
    EXPECT_TRUE(collectWords(past.pages).empty()) << fixture << ": index past the last chapter rendered text";
  }
}

TEST(Fb2SectionParserFile, DeclaredWindows1251BodyTextDecodesToUtf8) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("cp1251-declared.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  // cp1251 bytes must come out as UTF-8, including 'ё' (0xB8) and '№' (0xB9).
  EXPECT_TRUE(containsWord(words, "Привет,"));
  EXPECT_TRUE(containsWord(words, "мир"));
  EXPECT_TRUE(containsWord(words, "объём"));
  EXPECT_TRUE(containsWord(words, "№7"));
  EXPECT_FALSE(containsWord(words, "Второй"));  // section 1 stays out
}

TEST(Fb2SectionParserFile, NotesBodyContentNeverBecomesAChapter) {
  GfxRenderer renderer;
  // Only one reading section exists; its content excludes the notes body.
  auto story = parseSection(fixturePath("notes-body.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(story.ok);
  const auto storyWords = collectWords(story.pages);
  EXPECT_TRUE(containsWord(storyWords, "narrative"));
  EXPECT_FALSE(containsWord(storyWords, "footnote"));

  // Index 1 (the first notes section under the old numbering) selects nothing.
  auto beyond = parseSection(fixturePath("notes-body.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(beyond.ok);
  EXPECT_TRUE(collectWords(beyond.pages).empty());
}

TEST(Fb2SectionParserFile, PagesBreakAtViewportHeight) {
  GfxRenderer renderer;
  const auto spec = makeSpec();  // 64 px viewport, 16 px lines -> 4 lines/page
  auto result = parseSection(fixturePath("long.fb2"), 0, renderer, spec);
  ASSERT_TRUE(result.ok);
  EXPECT_GT(result.pages.size(), 1u);
  for (const auto& page : result.pages) {
    size_t lines = 0;
    for (const auto& element : page->elements) {
      if (element->getTag() == TAG_PageLine) lines++;
    }
    EXPECT_LE(lines, 4u);
    EXPECT_GT(lines, 0u);
  }
  // All 60 paragraphs must have made it through.
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "pageword000"));
  EXPECT_TRUE(containsWord(words, "pageword059"));
}

TEST(Fb2SectionParserFile, MalformedTruncatedXmlFailsWithoutCrash) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("malformed-truncated.fb2"), 0, renderer, makeSpec());
  EXPECT_FALSE(result.ok);
}

// The same malformed input, fed one buffer at a time, must fail the same way:
// Failed, never Finished. Returning Finished here would let an incremental
// caller commit a truncated section file as if it were complete.
TEST(Fb2SectionParserFile, MalformedTruncatedXmlFailsThroughSlicesToo) {
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  PageSink sink{&pages};
  const std::string path = fixturePath("malformed-truncated.fb2");
  Fb2SectionParser parser(path, 0, 0, renderer, makeSpec(), sink.fn());
  ASSERT_TRUE(parser.beginParse());

  Fb2SectionParser::ParseStatus status;
  int guard = 0;
  do {
    status = parser.parseSome(0, 1024);
  } while (status == Fb2SectionParser::ParseStatus::Paused && ++guard < 10000);

  EXPECT_EQ(status, Fb2SectionParser::ParseStatus::Failed);
  parser.finishParse();
  parser.finishParse();  // idempotent
}

TEST(Fb2SectionParserFile, MissingFileFailsWithoutCrash) {
  GfxRenderer renderer;
  auto result = parseSection("/nonexistent/nope.fb2", 0, renderer, makeSpec());
  EXPECT_FALSE(result.ok);
}

TEST(Fb2SectionParserFile, MinusOneTargetProcessesWholeSectionlessBody) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("no-sections.fb2"), -1, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "Paragraph"));
  EXPECT_TRUE(containsWord(words, "flowing."));
}

TEST(Fb2SectionParserFile, OutOfRangeSectionIndexYieldsNoPages) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("styles.fb2"), 7, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(collectWords(result.pages).empty());
}

}  // namespace
