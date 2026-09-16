// Host tests for src/util/DictTextUtils.cpp — the pure helpers behind the
// dictionary activities: selectable-token detection and cursor rows for word
// selection, and the greedy plain-text definition wrap.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "src/util/DictTextUtils.h"

namespace {

using DictTextUtils::isSelectableToken;
using DictTextUtils::WrappedLine;

// ---------------------------------------------------------------------------
// isSelectableToken
// ---------------------------------------------------------------------------

TEST(SelectableTokenTest, AsciiWordsAndDigitsAreSelectable) {
  EXPECT_TRUE(isSelectableToken("hello"));
  EXPECT_TRUE(isSelectableToken("42"));
  EXPECT_TRUE(isSelectableToken("x"));
  EXPECT_TRUE(isSelectableToken("\"quoted,\""));  // punctuation around a letter
}

TEST(SelectableTokenTest, AsciiPunctuationAloneIsNot) {
  EXPECT_FALSE(isSelectableToken(""));
  EXPECT_FALSE(isSelectableToken("-"));
  EXPECT_FALSE(isSelectableToken("..."));
  EXPECT_FALSE(isSelectableToken("'"));
  EXPECT_FALSE(isSelectableToken("(*)"));
}

TEST(SelectableTokenTest, NonAsciiLettersAreSelectable) {
  EXPECT_TRUE(isSelectableToken("\xC3\xA9"));          // é
  EXPECT_TRUE(isSelectableToken("\xE4\xB8\xAD"));      // 中
  EXPECT_TRUE(isSelectableToken("\xE2\x82\xAC"));      // € (U+20AC, outside the block)
  EXPECT_TRUE(isSelectableToken("\xF0\x9F\x98\x80"));  // emoji, 4-byte
}

TEST(SelectableTokenTest, GeneralPunctuationAloneIsNot) {
  EXPECT_FALSE(isSelectableToken("\xE2\x80\x94"));                          // — U+2014
  EXPECT_FALSE(isSelectableToken("\xE2\x80\xA2"));                          // • U+2022
  EXPECT_FALSE(isSelectableToken("\xE2\x80\xA6"));                          // … U+2026
  EXPECT_FALSE(isSelectableToken("\xE2\x81\x9F"));                          // U+205F
  EXPECT_FALSE(isSelectableToken("\xE2\x80\x9C\xE2\x80\x9D"));              // “”
  EXPECT_FALSE(isSelectableToken("\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"));  // ———
}

TEST(SelectableTokenTest, GeneralPunctuationAroundALetterIsSelectable) {
  EXPECT_TRUE(isSelectableToken("\xE2\x80\x9Cword\xE2\x80\x9D"));
  EXPECT_TRUE(isSelectableToken("\xE2\x80\x94x"));
  EXPECT_TRUE(isSelectableToken("\xE2\x80\x94\xC3\xA9"));
}

TEST(SelectableTokenTest, TruncatedGeneralPunctuationSequenceStopsSafely) {
  // A lead byte E2 80 with no third byte: the skip must not step past the NUL.
  EXPECT_FALSE(isSelectableToken("\xE2\x80"));
  EXPECT_FALSE(isSelectableToken("-\xE2\x81"));
}

// ---------------------------------------------------------------------------
// closestInRow
// ---------------------------------------------------------------------------

struct Box {
  int16_t x;
  int16_t width;
  uint16_t row;
};

TEST(ClosestInRowTest, EmptyAndMissingRowGiveMinusOne) {
  EXPECT_EQ(DictTextUtils::closestInRow<Box>(nullptr, 0, 0, 100), -1);
  const Box boxes[] = {{0, 10, 0}, {20, 10, 0}};
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 1, 100), -1);
}

TEST(ClosestInRowTest, PicksNearestCenterInRow) {
  const Box boxes[] = {{0, 20, 0}, {100, 20, 0}, {200, 20, 0}};  // centers 10, 110, 210
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 0, 0), 0);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 0, 105), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 0, 170), 2);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 0, 5000), 2);
}

TEST(ClosestInRowTest, OtherRowsAreIgnored) {
  const Box boxes[] = {{100, 20, 0}, {0, 20, 1}, {300, 20, 1}};
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 1, 110), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 1, 250), 2);
}

TEST(ClosestInRowTest, TiesGoToTheFirstBox) {
  const Box boxes[] = {{0, 20, 0}, {40, 20, 0}};  // centers 10 and 50; 30 is equidistant
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 0, 30), 0);
}

TEST(ClosestInRowTest, UsesTheBoxCenterNotItsLeftEdge) {
  const Box boxes[] = {{0, 100, 0}, {60, 10, 0}};  // centers 50 and 65
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 0, 58), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 0, 56), 0);
}

// ---------------------------------------------------------------------------
// wrapDefinitionText
// ---------------------------------------------------------------------------

// Fixed-advance fake font: 10px per codepoint (continuation bytes are free).
int measureCodepoints(void*, const char* text, size_t len) {
  int cps = 0;
  for (size_t i = 0; i < len; i++) {
    if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) cps++;
  }
  return cps * 10;
}

// Every span measures the same: models one glyph wider than the screen.
int measureConstant(void*, const char*, size_t) { return 100; }

struct Span {
  uint32_t start;
  uint16_t len;
  bool operator==(const Span& o) const { return start == o.start && len == o.len; }
};

std::vector<Span> wrap(const std::string& text, int maxWidth, int spaceWidth = 10,
                       int (*measure)(void*, const char*, size_t) = &measureCodepoints) {
  std::vector<WrappedLine> lines;
  const DictTextUtils::SpanMeasurer measurer{nullptr, measure};
  DictTextUtils::wrapDefinitionText(text.data(), static_cast<uint32_t>(text.size()), maxWidth, spaceWidth, measurer,
                                    lines);
  std::vector<Span> out;
  out.reserve(lines.size());
  for (const auto& l : lines) out.push_back({l.start, l.len});
  return out;
}

std::vector<std::string> texts(const std::string& text, const std::vector<Span>& spans) {
  std::vector<std::string> out;
  out.reserve(spans.size());
  for (const auto& s : spans) out.push_back(text.substr(s.start, s.len));
  return out;
}

TEST(WrapTextTest, EmptyAndWhitespaceOnlyGiveNoLines) {
  EXPECT_TRUE(wrap("", 100).empty());
  EXPECT_TRUE(wrap("   \t \r ", 100).empty());
  EXPECT_TRUE(wrap("\n\n\n", 100).empty());  // all blank lines are trailing
}

TEST(WrapTextTest, ShortTextIsOneLine) { EXPECT_EQ(wrap("hello world", 1000), (std::vector<Span>{{0, 11}})); }

TEST(WrapTextTest, GreedyWrapAtMaxWidth) {
  // "aaa bbb" = 30 + 10 + 30 = 70 fits exactly; "ccc" would make 110.
  const std::string t = "aaa bbb ccc";
  EXPECT_EQ(texts(t, wrap(t, 70)), (std::vector<std::string>{"aaa bbb", "ccc"}));
  EXPECT_EQ(texts(t, wrap(t, 69)), (std::vector<std::string>{"aaa", "bbb", "ccc"}));
}

TEST(WrapTextTest, NewlineBreaksAndBlankLinesSurviveInTheMiddle) {
  const std::string t = "a\n\nb";
  EXPECT_EQ(wrap(t, 1000), (std::vector<Span>{{0, 1}, {2, 0}, {3, 1}}));
}

TEST(WrapTextTest, TrailingBlankLinesAreTrimmed) {
  EXPECT_EQ(wrap("a\n\n\n", 1000), (std::vector<Span>{{0, 1}}));
  EXPECT_EQ(wrap("a\nb\n", 1000), (std::vector<Span>{{0, 1}, {2, 1}}));
}

TEST(WrapTextTest, EmbeddedNulBreaksLikeNewline) {
  const std::string t("a\0b", 3);
  EXPECT_EQ(wrap(t, 1000), (std::vector<Span>{{0, 1}, {2, 1}}));
}

TEST(WrapTextTest, LeadingSeparatorsAreSkippedAndCarriageReturnIsDropped) {
  const std::string t = "  \ta\r\nb";
  EXPECT_EQ(wrap(t, 1000), (std::vector<Span>{{3, 1}, {6, 1}}));
}

TEST(WrapTextTest, SpanCoversInnerSpacesBetweenTokens) {
  const std::string t = "a   b";
  EXPECT_EQ(wrap(t, 1000), (std::vector<Span>{{0, 5}}));
}

TEST(WrapTextTest, UnbreakableTokenIsSplitAtFittingWidth) {
  const std::string t = "abcdefg";
  EXPECT_EQ(texts(t, wrap(t, 30)), (std::vector<std::string>{"abc", "def", "g"}));
}

TEST(WrapTextTest, SplitRemainderJoinsFollowingTokens) {
  const std::string t = "abcdefg h";
  EXPECT_EQ(texts(t, wrap(t, 30)), (std::vector<std::string>{"abc", "def", "g h"}));
}

TEST(WrapTextTest, PrecedingContentIsFlushedBeforeASplit) {
  const std::string t = "ab cdefg";
  EXPECT_EQ(texts(t, wrap(t, 30)), (std::vector<std::string>{"ab", "cde", "fg"}));
}

TEST(WrapTextTest, SplitRespectsUtf8Boundaries) {
  const std::string e = "\xC3\xA9";  // é, 2 bytes, 10px
  const std::string t = e + e + e + e;
  const auto spans = wrap(t, 30);
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (Span{0, 6}));
  EXPECT_EQ(spans[1], (Span{6, 2}));
}

TEST(WrapTextTest, OverWideSingleGlyphStillMakesProgress) {
  // Every span is 100px on a 50px line: each codepoint gets its own line and
  // multi-byte sequences are never cut.
  const std::string t =
      "\xC3\xA9"
      "a";
  EXPECT_EQ(wrap(t, 50, 10, &measureConstant), (std::vector<Span>{{0, 2}, {2, 1}}));
}

TEST(WrapTextTest, TokenIsCappedAtMaxLineBytes) {
  // 300 unbroken bytes at 10px each: the token is measured in a 191-byte
  // piece and a 109-byte piece, which then wrap as separate tokens.
  const std::string t(300, 'a');
  const auto spans = wrap(t, 191 * 10);
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (Span{0, 191}));
  EXPECT_EQ(spans[1], (Span{191, 109}));
}

TEST(WrapTextTest, ByteCapBacksOffToACodepointBoundary) {
  // 190 ASCII bytes then é: a cap at 191 would land inside the 2-byte
  // sequence, so the first piece is 190 bytes.
  const std::string t = std::string(190, 'a') + "\xC3\xA9" + "b";
  const auto spans = wrap(t, 191 * 10);
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0], (Span{0, 190}));
  EXPECT_EQ(spans[1], (Span{190, 3}));
}

TEST(WrapTextTest, ParagraphsWrapIndependently) {
  const std::string t = "aa bb\ncc dd";
  EXPECT_EQ(texts(t, wrap(t, 40)), (std::vector<std::string>{"aa", "bb", "cc", "dd"}));
}

TEST(SelectableTokenTest, InvalidUtf8BytesCountAsWordBytes) {
  // Pinned: anything >= 0x80 that is not the E2 80 / E2 81 General
  // Punctuation prefix is treated as a word codepoint, valid UTF-8 or not.
  EXPECT_TRUE(isSelectableToken("\x80"));      // lone continuation byte
  EXPECT_TRUE(isSelectableToken("\xFF"));      // never-valid lead byte
  EXPECT_TRUE(isSelectableToken("-\xBF-"));    // surrounded by punctuation
  EXPECT_TRUE(isSelectableToken("\xE2"));      // lone E2: p[1] is the NUL
  EXPECT_TRUE(isSelectableToken("\xE2\x82"));  // E2 82 is not the punctuation block
}

TEST(SelectableTokenTest, RepeatedPunctuationSequencesAdvanceByThreeBytes) {
  // The skip must land on the next lead byte, not inside the sequence.
  EXPECT_TRUE(isSelectableToken("\xE2\x80\x94\xE2\x80\x94x"));
  EXPECT_FALSE(isSelectableToken("\xE2\x80\x94\xE2\x81\x9F\xE2\x80\xA6"));
  EXPECT_TRUE(isSelectableToken("\xE2\x80\x94\xC3\xA9\xE2\x80\x94"));
}

TEST(SelectableTokenTest, UnderscoreAndSymbolsAreNotAlphanumeric) {
  EXPECT_FALSE(isSelectableToken("_"));
  EXPECT_FALSE(isSelectableToken("+-*/"));
  EXPECT_FALSE(isSelectableToken(" \t"));
  EXPECT_TRUE(isSelectableToken("_a_"));
}

TEST(ClosestInRowTest, SingleBoxInTheRowIsAlwaysTheAnswer) {
  const Box boxes[] = {{0, 10, 0}, {-500, 4, 7}};
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 7, 10000), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 7, -10000), 1);
}

TEST(ClosestInRowTest, NegativeCoordinatesAndZeroWidthBoxes) {
  const Box boxes[] = {{-100, 0, 3}, {-20, 0, 3}, {40, 0, 3}};  // centers -100, -20, 40
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 3, -90), 0);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 3, -30), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 3, 0), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 3, 3, 30), 2);
}

TEST(ClosestInRowTest, TheHighestRowIndexIsUsable) {
  const Box boxes[] = {{0, 10, 0}, {50, 10, 65535}};
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 65535, 0), 1);
  EXPECT_EQ(DictTextUtils::closestInRow(boxes, 2, 65534, 0), -1);
}

TEST(WrapTextTest, ZeroMaxWidthStillMakesProgressOneCodepointPerLine) {
  const std::string t = "ab";
  EXPECT_EQ(wrap(t, 0), (std::vector<Span>{{0, 1}, {1, 1}}));
  const std::string u =
      "\xC3\xA9"
      "z";
  EXPECT_EQ(wrap(u, 0), (std::vector<Span>{{0, 2}, {2, 1}}));  // sequence never cut
}

TEST(WrapTextTest, NulOnlyInputGivesNoLines) { EXPECT_TRUE(wrap(std::string("\0\0\0", 3), 100).empty()); }

TEST(WrapTextTest, WideSpaceWidthForcesOneTokenPerLine) {
  const std::string t = "aa bb cc";
  EXPECT_EQ(texts(t, wrap(t, 100, 1000)), (std::vector<std::string>{"aa", "bb", "cc"}));
}

TEST(WrapTextTest, InvalidUtf8BytesWrapAsOpaqueBytes) {
  // Lead bytes with no continuation still count as codepoint starts, so the
  // wrap neither loops nor merges them into a neighbouring token.
  const std::string t = "\xFF\xFE ab";
  EXPECT_EQ(wrap(t, 20), (std::vector<Span>{{0, 2}, {3, 2}}));
}

TEST(WrapTextTest, CarriageReturnParagraphBreaksBehaveLikeNewlines) {
  const std::string t = "a\r\n\r\nb";
  EXPECT_EQ(wrap(t, 1000), (std::vector<Span>{{0, 1}, {3, 0}, {5, 1}}));
}

TEST(WrapTextTest, LengthArgumentBoundsTheScanNotTheTerminator) {
  // wrapDefinitionText takes an explicit length: bytes past n are invisible.
  const std::string t = "ab cd";
  std::vector<WrappedLine> lines;
  const DictTextUtils::SpanMeasurer measurer{nullptr, &measureCodepoints};
  DictTextUtils::wrapDefinitionText(t.data(), 2, 1000, 10, measurer, lines);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].start, 0u);
  EXPECT_EQ(lines[0].len, 2u);
}

TEST(WrapTextTest, ReusedOutputVectorIsClearedFirst) {
  std::vector<WrappedLine> lines{{99, 99}, {98, 98}};
  const DictTextUtils::SpanMeasurer measurer{nullptr, &measureCodepoints};
  const std::string t = "hi";
  DictTextUtils::wrapDefinitionText(t.data(), 2, 1000, 10, measurer, lines);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].start, 0u);
}

}  // namespace
