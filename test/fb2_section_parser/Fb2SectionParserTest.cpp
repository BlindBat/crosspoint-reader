#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>
#include <expat.h>
#include <gtest/gtest.h>

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

// Drives the real parser against a fixture file and collects finished pages.
struct ParseResult {
  std::vector<std::unique_ptr<Page>> pages;
  bool ok = false;
};

ParseResult parseSection(const std::string& path, int targetSectionIndex, GfxRenderer& renderer,
                         const ReaderRenderSpec& spec) {
  ParseResult result;
  const std::string filepath = path;
  Fb2SectionParser parser(filepath, 0, targetSectionIndex, renderer, spec,
                          [&result](std::unique_ptr<Page> page) { result.pages.push_back(std::move(page)); });
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
  Fb2SectionParser parser{filepath, 0,    -1,
                          renderer, spec, [this](std::unique_ptr<Page> page) { pages.push_back(std::move(page)); }};

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

TEST(Fb2SectionParserFile, NestedSectionContentStaysInsideTargetSectionZero) {
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("nested-sections.fb2"), 0, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  const auto words = collectWords(result.pages);
  EXPECT_TRUE(containsWord(words, "innerword"));  // nested child of section 0
  EXPECT_FALSE(containsWord(words, "level"));     // "Second top level paragraph." is section 1
}

// Section indices must match Fb2MetadataParser's numbering, which counts only
// depth-1 sections: nested <section>s inside an earlier chapter are that
// chapter's content, never chapters of their own. Selecting top-level section
// 1 of nested-sections.fb2 must return "Part Two", not the nested "Inner
// Chapter" of section 0.
TEST(Fb2SectionParserFile, NestedSectionsDoNotSkewTargetSectionIndexing) {
  GfxRenderer renderer;
  auto second = parseSection(fixturePath("nested-sections.fb2"), 1, renderer, makeSpec());
  ASSERT_TRUE(second.ok);
  const auto secondWords = collectWords(second.pages);
  EXPECT_TRUE(containsWord(secondWords, "level"));       // "Second top level paragraph."
  EXPECT_FALSE(containsWord(secondWords, "innerword"));  // nested child of section 0
}

TEST(Fb2SectionParserFile, NestedSectionsDoNotExtendTheTopLevelIndexRange) {
  // Only 2 top-level sections exist; index 2 must select nothing even though
  // three <section> tags appear in the file.
  GfxRenderer renderer;
  auto result = parseSection(fixturePath("nested-sections.fb2"), 2, renderer, makeSpec());
  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(collectWords(result.pages).empty());
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
