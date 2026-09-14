// Layout suite for the typography core between parser and page:
// ParsedText::layoutAndExtractLines (line breaking, justification, hyphen
// handling, attachment) and TextBlock (the arena-backed line container).
//
// The GfxRenderer stub gives every codepoint a fixed advance (10 px regular,
// 12 bold, 11 italic) and every inter-word gap 5 px, so each expected break
// index and x-position below is computed by hand. Tests assert LINE CONTENT
// (which words landed on which line) and exact positions, not line counts.

#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct CapturedLine {
  std::shared_ptr<TextBlock> block;
  uint32_t visibleOffset;
};

std::vector<CapturedLine> layoutAll(ParsedText& text, const uint16_t viewportWidth, const bool includeLastLine = true) {
  GfxRenderer renderer;
  std::vector<CapturedLine> lines;
  text.layoutAndExtractLines(
      renderer, /*fontId=*/0, viewportWidth,
      [&lines](std::shared_ptr<TextBlock> block, uint32_t offset) { lines.push_back({std::move(block), offset}); },
      includeLastLine);
  return lines;
}

std::vector<std::string> wordsOf(const TextBlock& block) {
  std::vector<std::string> words;
  for (uint16_t i = 0; i < block.wordCount(); ++i) {
    words.emplace_back(block.wordText(i), block.wordTextLen(i));
  }
  return words;
}

std::vector<int> xposOf(const TextBlock& block) {
  std::vector<int> xs;
  for (uint16_t i = 0; i < block.wordCount(); ++i) {
    xs.push_back(block.wordXpos(i));
  }
  return xs;
}

BlockStyle leftAligned() {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  return style;
}

// extraParagraphSpacing=true suppresses the implicit 3-space first-line indent,
// keeping x-positions flush with the margin unless a test opts back in.
ParsedText makeText(const BlockStyle& style, const bool hyphenation = false) {
  return ParsedText(/*extraParagraphSpacing=*/true, hyphenation, /*focusReadingEnabled=*/false, style);
}

class TextBlockLayoutTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Hyphenator language is process-global state; reset so no test inherits
    // patterns from a previous one.
    Hyphenator::setPreferredLanguage("");
  }
};

// ---------------------------------------------------------------------------
// Empty input
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, EmptyBlockProducesNoLines) {
  ParsedText text = makeText(leftAligned());
  EXPECT_TRUE(text.isEmpty());
  const auto lines = layoutAll(text, 100);
  EXPECT_TRUE(lines.empty());
}

TEST_F(TextBlockLayoutTest, EmptyWordIsIgnored) {
  ParsedText text = makeText(leftAligned());
  text.addWord("", EpdFontFamily::REGULAR);
  EXPECT_TRUE(text.isEmpty());
  EXPECT_TRUE(layoutAll(text, 100).empty());
}

// ---------------------------------------------------------------------------
// Exact width boundaries
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, TwoWordsFittingExactlyStayOnOneLine) {
  // "wren"=40 px + gap 5 + "kite"=40 px == viewport 85 px exactly.
  ParsedText text = makeText(leftAligned());
  text.addWord("wren", EpdFontFamily::REGULAR);
  text.addWord("kite", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 85);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"wren", "kite"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 45}));
}

TEST_F(TextBlockLayoutTest, OnePixelNarrowerViewportBreaksTheLine) {
  // Same words, viewport 84 px: 85 px no longer fits, each word gets its own line.
  ParsedText text = makeText(leftAligned());
  text.addWord("wren", EpdFontFamily::REGULAR);
  text.addWord("kite", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 84);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"wren"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"kite"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0}));
}

TEST_F(TextBlockLayoutTest, FourEqualWordsSplitTwoAndTwo) {
  // Four 40 px words at viewport 85: the DP packs word pairs exactly (40+5+40).
  ParsedText text = makeText(leftAligned());
  for (const char* w : {"aaaa", "bbbb", "cccc", "dddd"}) {
    text.addWord(w, EpdFontFamily::REGULAR);
  }

  const auto lines = layoutAll(text, 85);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"aaaa", "bbbb"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"cccc", "dddd"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 45}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0, 45}));
}

// ---------------------------------------------------------------------------
// Justification
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, JustifyDistributesSpareSpaceEvenlyAcrossGaps) {
  // Default BlockStyle alignment is Justify. Line 1 = "aa bb cc":
  // words 3*20=60, natural gaps 2*5=10, spare = 100-70 = 30, 2 gaps -> +15 each.
  ParsedText text = makeText(BlockStyle{});
  text.addWord("aa", EpdFontFamily::REGULAR);
  text.addWord("bb", EpdFontFamily::REGULAR);
  text.addWord("cc", EpdFontFamily::REGULAR);
  text.addWord("dddddddd", EpdFontFamily::REGULAR);  // 80 px, forces its own (last) line

  const auto lines = layoutAll(text, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"aa", "bb", "cc"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 40, 80}));  // right edge lands at 80+20=100

  // The LAST line of a justified paragraph is never stretched.
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"dddddddd"}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0}));
}

TEST_F(TextBlockLayoutTest, JustifyFloorsPerGapExtraAndLeavesRemainderShortOfMargin) {
  // Viewport 103: spare = 103-70 = 33 over 2 gaps -> floor(16.5) = +16 each,
  // the odd pixel is NOT distributed, so the line ends at 82+20=102, 1 px short.
  ParsedText text = makeText(BlockStyle{});
  text.addWord("aa", EpdFontFamily::REGULAR);
  text.addWord("bb", EpdFontFamily::REGULAR);
  text.addWord("cc", EpdFontFamily::REGULAR);
  text.addWord("dddddddd", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 103);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"aa", "bb", "cc"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 41, 82}));
}

TEST_F(TextBlockLayoutTest, CjkZeroWidthGapsStretchUnderJustification) {
  // A glued CJK run splits into one token per character with zero-width
  // breakable gaps (TokenBoundary: continues=false, noSpaceBefore=true).
  // Six 10 px chars at viewport 45: DP picks 4+2 (cost 25 beats 3+3's 225).
  // Line 1 justify: spare = 45-40 = 5 over 3 zero-width gaps -> +1 px each.
  ParsedText text = makeText(BlockStyle{});
  text.addWord("你好世界你好", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 45);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"你", "好", "世", "界"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 11, 22, 33}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"你", "好"}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0, 10}));  // last line: gaps stay zero-width
}

// ---------------------------------------------------------------------------
// Attached punctuation
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, CjkClosingPunctuationCannotStartALine) {
  // "。" may not begin a line, so tokenization glues it to the preceding
  // character: tokens are ["你", "好。"], never a dangling "。".
  ParsedText text = makeText(leftAligned());
  text.addWord("你好。", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 20);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"你"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"好。"}));
}

TEST_F(TextBlockLayoutTest, AttachedPunctuationMovesToTheNextLineWithItsWord) {
  // "?" is glued to "question" (attachToPrevious). "ask question ?" = 125 px
  // does not fit in 100, and the break directly before "?" is forbidden, so
  // "question ?" moves as a unit. On its line "?" sits flush after "question"
  // (kerning-only gap, no 5 px space).
  ParsedText text = makeText(leftAligned());
  text.addWord("ask", EpdFontFamily::REGULAR);
  text.addWord("question", EpdFontFamily::REGULAR);
  text.addWord("?", EpdFontFamily::REGULAR, false, /*attachToPrevious=*/true);

  const auto lines = layoutAll(text, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"ask"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"question", "?"}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0, 80}));
}

// ---------------------------------------------------------------------------
// Style runs
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, BoldRunWidensWordsAndChangesTheBreak) {
  // "one"(reg)=30, "two"(bold)=36, "three"(italic)=55.
  // At 70 px: 30+5+36=71 overflows, so every word gets its own line.
  ParsedText boldText = makeText(leftAligned());
  boldText.addWord("one", EpdFontFamily::REGULAR);
  boldText.addWord("two", EpdFontFamily::BOLD);
  boldText.addWord("three", EpdFontFamily::ITALIC);

  const auto boldLines = layoutAll(boldText, 70);
  ASSERT_EQ(boldLines.size(), 3u);
  EXPECT_EQ(wordsOf(*boldLines[0].block), (std::vector<std::string>{"one"}));
  EXPECT_EQ(wordsOf(*boldLines[1].block), (std::vector<std::string>{"two"}));
  EXPECT_EQ(wordsOf(*boldLines[2].block), (std::vector<std::string>{"three"}));
  EXPECT_EQ(boldLines[1].block->wordStyle(0), EpdFontFamily::BOLD);
  EXPECT_EQ(boldLines[2].block->wordStyle(0), EpdFontFamily::ITALIC);

  // Identical text all-regular at the same width: "one two"=65 now fits.
  ParsedText regularText = makeText(leftAligned());
  regularText.addWord("one", EpdFontFamily::REGULAR);
  regularText.addWord("two", EpdFontFamily::REGULAR);
  regularText.addWord("three", EpdFontFamily::REGULAR);

  const auto regularLines = layoutAll(regularText, 70);
  ASSERT_EQ(regularLines.size(), 2u);
  EXPECT_EQ(wordsOf(*regularLines[0].block), (std::vector<std::string>{"one", "two"}));
  EXPECT_EQ(wordsOf(*regularLines[1].block), (std::vector<std::string>{"three"}));
}

TEST_F(TextBlockLayoutTest, MixedStyleLineUsesPerStyleWidthsForPositions) {
  // One pixel wider (71): "one"+"two"(bold) fit exactly; "two" starts at 30+5.
  ParsedText text = makeText(leftAligned());
  text.addWord("one", EpdFontFamily::REGULAR);
  text.addWord("two", EpdFontFamily::BOLD);
  text.addWord("three", EpdFontFamily::ITALIC);

  const auto lines = layoutAll(text, 71);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"one", "two"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 35}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"three"}));
}

// ---------------------------------------------------------------------------
// Hyphens: soft, explicit, pattern-inserted, fallback
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, SoftHyphensAreInvisibleWhenNoBreakIsTaken) {
  // "hy­phen": the soft hyphen is excluded from measurement (60 px, not
  // 70) and stripped from the rendered text.
  ParsedText text = makeText(leftAligned());
  text.addWord("hy\xC2\xADphen", EpdFontFamily::REGULAR);
  text.addWord("next", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 200);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"hyphen", "next"}));
  // "next" at 60+5: proves the soft hyphen contributed no width.
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0, 65}));
}

TEST_F(TextBlockLayoutTest, SoftHyphenBreakSubstitutesAVisibleHyphen) {
  // "so­da" at viewport 30: the only break point is the soft hyphen; the
  // prefix renders as "so-" (visible '-' substituted) and "da" wraps.
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/true);
  text.addWord(
      "so\xC2\xAD"
      "da",
      EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 30);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"so-"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"da"}));
}

TEST_F(TextBlockLayoutTest, ExplicitHyphenBreaksAfterTheHyphenWithoutInsertingOne) {
  // "well-known" breaks after the existing '-' -- no second hyphen appears.
  // Visible-offset bookkeeping: the remainder starts 5 codepoints after the
  // word's own offset (100 -> 105).
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/true);
  text.addWord("well-known", EpdFontFamily::REGULAR, false, false, /*visibleTextOffset=*/100);

  const auto lines = layoutAll(text, 50);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"well-"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"known"}));
  EXPECT_EQ(lines[0].visibleOffset, 100u);
  EXPECT_EQ(lines[1].visibleOffset, 105u);
}

TEST_F(TextBlockLayoutTest, NonBreakingHyphenForbidsTheBreak) {
  // U+2011 NON-BREAKING HYPHEN exists to forbid a break, matching
  // TokenBoundary::allowsBreakAfterExplicitHyphen(). Even though "no‑go"
  // (50 px) overflows the 30 px viewport, neither the explicit-hyphen break
  // nor a fallback break may split it: the word stays whole on one line.
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/true);
  text.addWord("no\xE2\x80\x91go", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 30);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"no\xE2\x80\x91go"}));
}

TEST_F(TextBlockLayoutTest, RegularHyphenStillBreaksNextToANonBreakingOne) {
  // "ab-cd‑ef": the ASCII '-' after "ab" is a break opportunity, the U+2011
  // between "cd" and "ef" is not. At 40 px the layout takes the only legal
  // break ("ab-" = 30 px fits) and the U+2011 keeps "cd‑ef" whole even
  // though it overflows (50 px).
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/true);
  text.addWord(
      "ab-cd\xE2\x80\x91"
      "ef",
      EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 40);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"ab-"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"cd\xE2\x80\x91"
                                                                "ef"}));
}

TEST_F(TextBlockLayoutTest, EnglishPatternsInsertAHyphenAtALegalBreak) {
  // With the real English Liang patterns (min prefix/suffix 3), "hyphenation"
  // offers a break at "hyphen|ation"; at 70 px the widest fitting prefix is
  // "hyphen-" (7 codepoints).
  Hyphenator::setPreferredLanguage("en");
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/true);
  text.addWord("hyphenation", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 70);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"hyphen-"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"ation"}));
}

TEST_F(TextBlockLayoutTest, OversizedWordFallsBackToCharacterSplitsEvenWithoutHyphenation) {
  // Hyphenation disabled, no language: a word wider than the viewport still
  // must be broken. Fallback splitting (min prefix/suffix 2) picks the widest
  // prefix + '-' that fits: "abc-"(40), then "def-"(40), then "gh".
  ParsedText text = makeText(leftAligned(), /*hyphenation=*/false);
  text.addWord("abcdefgh", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 45);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"abc-"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"def-"}));
  EXPECT_EQ(wordsOf(*lines[2].block), (std::vector<std::string>{"gh"}));
}

// ---------------------------------------------------------------------------
// Viewport extremes
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, ViewportSmallerThanOneGlyphForcesOneWordPerLine) {
  // 5 px viewport, 10 px single-char words, nothing can be split further:
  // each word overflows but is still emitted, one per line at x=0.
  ParsedText text = makeText(leftAligned());
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 5);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"a"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"b"}));
  EXPECT_EQ(wordsOf(*lines[2].block), (std::vector<std::string>{"c"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{0}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0}));
  EXPECT_EQ(xposOf(*lines[2].block), (std::vector<int>{0}));
}

TEST_F(TextBlockLayoutTest, ZeroWidthViewportStillEmitsEveryWord) {
  ParsedText text = makeText(leftAligned());
  text.addWord("ab", EpdFontFamily::REGULAR);
  text.addWord("cd", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 0);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"ab"}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"cd"}));
}

// ---------------------------------------------------------------------------
// First-line indent and pagination continuation
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, ImplicitFirstLineIndentIsThreeSpacesWide) {
  // Without extra paragraph spacing, a naturally-aligned paragraph gets a
  // 3 * spaceWidth = 15 px first-line indent: the first line lays out in
  // 100-15=85 px and starts at x=15; continuation lines start at x=0.
  ParsedText text(/*extraParagraphSpacing=*/false, /*hyphenationEnabled=*/false,
                  /*focusReadingEnabled=*/false, BlockStyle{});
  text.addWord("aaaa", EpdFontFamily::REGULAR);
  text.addWord("bbbb", EpdFontFamily::REGULAR);
  text.addWord("cccc", EpdFontFamily::REGULAR);

  const auto lines = layoutAll(text, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(wordsOf(*lines[0].block), (std::vector<std::string>{"aaaa", "bbbb"}));
  EXPECT_EQ(xposOf(*lines[0].block), (std::vector<int>{15, 60}));
  EXPECT_EQ(wordsOf(*lines[1].block), (std::vector<std::string>{"cccc"}));
  EXPECT_EQ(xposOf(*lines[1].block), (std::vector<int>{0}));
}

TEST_F(TextBlockLayoutTest, ExcludingTheLastLineLeavesItsWordsForTheNextPass) {
  ParsedText text = makeText(leftAligned());
  text.addWord("wren", EpdFontFamily::REGULAR, false, false, /*visibleTextOffset=*/0);
  text.addWord("kite", EpdFontFamily::REGULAR, false, false, /*visibleTextOffset=*/5);

  const auto firstPass = layoutAll(text, 40, /*includeLastLine=*/false);
  ASSERT_EQ(firstPass.size(), 1u);
  EXPECT_EQ(wordsOf(*firstPass[0].block), (std::vector<std::string>{"wren"}));
  EXPECT_EQ(firstPass[0].visibleOffset, 0u);
  EXPECT_EQ(text.size(), 1u);  // "kite" was not consumed

  const auto secondPass = layoutAll(text, 40, /*includeLastLine=*/true);
  ASSERT_EQ(secondPass.size(), 1u);
  EXPECT_EQ(wordsOf(*secondPass[0].block), (std::vector<std::string>{"kite"}));
  EXPECT_EQ(secondPass[0].visibleOffset, 5u);
  EXPECT_TRUE(text.isEmpty());
}

// ---------------------------------------------------------------------------
// TextBlock arena container
// ---------------------------------------------------------------------------

TEST_F(TextBlockLayoutTest, TextBlockStoresWordsPositionsAndStylesInArena) {
  const std::vector<std::string> words{"h\xC3\xA9llo", "x"};
  const std::vector<int16_t> xpos{0, 60};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::BOLD};

  TextBlock block(words, xpos, styles, {}, {});
  ASSERT_TRUE(block.valid());
  ASSERT_EQ(block.wordCount(), 2u);
  EXPECT_STREQ(block.wordText(0), "h\xC3\xA9llo");
  EXPECT_EQ(block.wordTextLen(0), 6u);  // UTF-8 bytes, NUL excluded
  EXPECT_STREQ(block.wordText(1), "x");
  EXPECT_EQ(block.wordTextLen(1), 1u);
  EXPECT_EQ(block.wordXpos(0), 0);
  EXPECT_EQ(block.wordXpos(1), 60);
  EXPECT_EQ(block.wordStyle(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(block.wordStyle(1), EpdFontFamily::BOLD);
  // No focus annotations were provided: accessors must report none.
  EXPECT_EQ(block.focusBoundary(0), 0u);
  EXPECT_EQ(block.focusSuffixX(0), 0u);
  EXPECT_FALSE(block.isEmpty());
}

TEST_F(TextBlockLayoutTest, TextBlockCarriesFocusAnnotationsWhenPresent) {
  const std::vector<std::string> words{"reading", "on"};
  const std::vector<int16_t> xpos{0, 80};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::REGULAR};
  const std::vector<uint8_t> boundaries{3, 0};
  const std::vector<uint16_t> suffixX{36, 0};

  TextBlock block(words, xpos, styles, boundaries, suffixX);
  ASSERT_TRUE(block.valid());
  EXPECT_EQ(block.focusBoundary(0), 3u);
  EXPECT_EQ(block.focusSuffixX(0), 36u);
  EXPECT_EQ(block.focusBoundary(1), 0u);
  EXPECT_EQ(block.focusSuffixX(1), 0u);
}

TEST_F(TextBlockLayoutTest, TextBlockRejectsMismatchedVectorSizes) {
  const std::vector<std::string> words{"a", "b"};
  const std::vector<int16_t> xpos{0};  // one entry short
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::REGULAR};

  TextBlock block(words, xpos, styles, {}, {});
  EXPECT_FALSE(block.valid());
  EXPECT_TRUE(block.isEmpty());
}

TEST_F(TextBlockLayoutTest, TextBlockRejectsTextLargerThanArenaLimit) {
  // Total text (incl. per-word NUL) above UINT16_MAX must be refused, not
  // truncated: offsets are 16-bit.
  const std::vector<std::string> words{std::string(70000, 'a')};
  const std::vector<int16_t> xpos{0};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR};

  TextBlock block(words, xpos, styles, {}, {});
  EXPECT_FALSE(block.valid());
  EXPECT_TRUE(block.isEmpty());
}

TEST_F(TextBlockLayoutTest, EmptyTextBlockIsValidAndEmpty) {
  TextBlock block({}, {}, {}, {}, {});
  EXPECT_TRUE(block.valid());
  EXPECT_TRUE(block.isEmpty());
  EXPECT_EQ(block.wordCount(), 0u);
}

}  // namespace
