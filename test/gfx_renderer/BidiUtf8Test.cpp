// lib/MiniBidi/BidiUtils.cpp visual word ordering, paragraph-level detection
// and the BIDI_MAX_LINE overflow guards, plus lib/Utf8/Utf8.cpp decoding of
// malformed and truncated UTF-8.
//
// Arabic shaping, transparent-mark classification and NFC composition are
// pinned by test/minibidi_arabic/ and test/utf8_compose/ and are not repeated.

#include <BidiUtils.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

extern "C" {
#include <minibidi.h>
}
#undef when
#undef otherwise

namespace {

// UTF-8 literals kept as escapes so the file stays plain ASCII.
constexpr const char* kHebrewShalom = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";  // shalom
constexpr const char* kHebrewSefer = "\xd7\xa1\xd7\xa4\xd7\xa8";          // sefer
constexpr const char* kArabicSalam = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85";  // salam

uint32_t decodeOne(const char* s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  return utf8NextCodepoint(&p);
}

std::string repeatUtf8(const char* unit, const size_t times) {
  std::string out;
  for (size_t i = 0; i < times; i++) out += unit;
  return out;
}

// ---------------------------------------------------------------------------
// Paragraph level detection (UAX#9 P2/P3)
// ---------------------------------------------------------------------------

TEST(DetectParagraphLevel, LatinIsLtrAndHebrewOrArabicIsRtl) {
  EXPECT_EQ(BidiUtils::detectParagraphLevel("Hello"), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(kHebrewShalom), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(kArabicSalam), 1);
}

TEST(DetectParagraphLevel, TheFirstStrongCharacterWins) {
  const std::string ltrFirst = std::string("A ") + kHebrewShalom;
  const std::string rtlFirst = std::string(kHebrewShalom) + " A";
  EXPECT_EQ(BidiUtils::detectParagraphLevel(ltrFirst.c_str()), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(rtlFirst.c_str()), 1);
}

TEST(DetectParagraphLevel, NeutralsAreSkippedUntilAStrongCharacterAppears) {
  const std::string neutralsThenRtl = std::string("123 -- ") + kHebrewShalom;
  EXPECT_EQ(BidiUtils::detectParagraphLevel(neutralsThenRtl.c_str()), 1);
}

TEST(DetectParagraphLevel, WithoutAStrongCharacterTheFallbackIsUsedAndMaskedToOneBit) {
  EXPECT_EQ(BidiUtils::detectParagraphLevel("123 !?", 0), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("123 !?", 1), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("123 !?", 2), 0) << "even fallbacks mask to LTR";
  EXPECT_EQ(BidiUtils::detectParagraphLevel("123 !?", 3), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel("", 1), 1);
}

TEST(DetectParagraphLevel, NullTextAndNonPositiveProbeDepthReturnTheFallback) {
  EXPECT_EQ(BidiUtils::detectParagraphLevel(nullptr, 1), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(nullptr, 0), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(kHebrewShalom, 1, 0), 1) << "probe depth 0 -> fallback";
  EXPECT_EQ(BidiUtils::detectParagraphLevel(kHebrewShalom, 0, -1), 0);
}

TEST(DetectParagraphLevel, TheProbeGivesUpAfterTheNeutralBudgetIsSpent) {
  // Three neutrals then a strong RTL letter: a budget of 3 stops one short.
  const std::string text = std::string("   ") + kHebrewShalom;
  EXPECT_EQ(BidiUtils::detectParagraphLevel(text.c_str(), 0, 4), 1);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(text.c_str(), 0, 3), 0);
  EXPECT_EQ(BidiUtils::detectParagraphLevel(text.c_str(), 1, 3), 1) << "the fallback is what is returned";
}

TEST(DetectParagraphLevel, InvalidUtf8StopsTheScanAtTheBadByte) {
  // A stray continuation byte decodes to U+FFFD, which ends the probe before
  // the Hebrew that follows it.
  const std::string text = std::string("\x80") + kHebrewShalom;
  EXPECT_EQ(BidiUtils::detectParagraphLevel(text.c_str(), 0), 0);
}

TEST(StartsWithRtl, MirrorsTheParagraphProbeForTheBooleanCase) {
  EXPECT_TRUE(BidiUtils::startsWithRtl(kHebrewShalom));
  EXPECT_TRUE(BidiUtils::startsWithRtl(kArabicSalam));
  EXPECT_FALSE(BidiUtils::startsWithRtl("Hello"));
  EXPECT_FALSE(BidiUtils::startsWithRtl(""));
  EXPECT_FALSE(BidiUtils::startsWithRtl(nullptr));
  EXPECT_FALSE(BidiUtils::startsWithRtl(kHebrewShalom, 0)) << "non-positive probe depth";
  EXPECT_FALSE(BidiUtils::startsWithRtl(kHebrewShalom, -1));
}

TEST(StartsWithRtl, NeutralsBeforeTheFirstStrongCharacterDoNotDecide) {
  const std::string text = std::string("( ") + kHebrewShalom;
  EXPECT_TRUE(BidiUtils::startsWithRtl(text.c_str()));
  EXPECT_FALSE(BidiUtils::startsWithRtl("( Hello"));
}

// ---------------------------------------------------------------------------
// computeVisualWordOrder
// ---------------------------------------------------------------------------

TEST(VisualWordOrder, ZeroOrOneWordNeedsNoReordering) {
  std::vector<uint16_t> order{7, 7, 7};
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({}, true, order));
  EXPECT_TRUE(order.empty()) << "the output is always cleared first";
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({kHebrewShalom}, true, order));
  EXPECT_TRUE(order.empty());
}

TEST(VisualWordOrder, PureLtrInAnLtrParagraphReportsNothingToDo) {
  std::vector<uint16_t> order;
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({"one", "two", "three"}, false, order));
  EXPECT_TRUE(order.empty());
}

TEST(VisualWordOrder, PureLtrInAnRtlParagraphKeepsLogicalOrderButNeedsThePositioningPath) {
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({"one", "two", "three"}, true, order));
  EXPECT_EQ(order, (std::vector<uint16_t>{0, 1, 2}));
}

TEST(VisualWordOrder, PureRtlInAnRtlParagraphReversesTheWords) {
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({kHebrewShalom, kHebrewSefer, kHebrewShalom}, true, order));
  EXPECT_EQ(order, (std::vector<uint16_t>{2, 1, 0}));
}

TEST(VisualWordOrder, PureRtlInAnLtrParagraphAlsoReversesTheRun) {
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({kHebrewShalom, kHebrewSefer}, false, order));
  EXPECT_EQ(order, (std::vector<uint16_t>{1, 0}));
}

TEST(VisualWordOrder, AnLtrRunInsideAnRtlLineKeepsItsOwnWordsInOrder) {
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({kHebrewShalom, "alpha", "beta", kHebrewSefer}, true, order));
  // The RTL words flank a left-to-right island that stays internally ordered.
  EXPECT_EQ(order, (std::vector<uint16_t>{3, 1, 2, 0}));
}

TEST(VisualWordOrder, EveryWordIndexAppearsExactlyOnce) {
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({kHebrewShalom, "alpha", "1", kHebrewSefer, "beta"}, true, order));
  ASSERT_EQ(order.size(), 5u);
  std::vector<bool> seen(5, false);
  for (const uint16_t w : order) {
    ASSERT_LT(w, 5u);
    EXPECT_FALSE(seen[w]) << "word " << w << " listed twice";
    seen[w] = true;
  }
}

TEST(VisualWordOrder, MoreWordsThanBidiMaxLineIsRefused) {
  std::vector<std::string> words(BIDI_MAX_LINE + 1, "a");
  std::vector<uint16_t> order{1, 2, 3};
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder(words, true, order));
  EXPECT_TRUE(order.empty());
}

TEST(VisualWordOrder, MoreCharactersThanBidiMaxLineIsRefused) {
  // Well under the word cap, but the flattened character stream (words plus
  // separating spaces) overflows the shared bidi line buffer.
  const std::string longWord(BIDI_MAX_LINE, 'a');
  std::vector<uint16_t> order;
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({longWord, longWord, kHebrewShalom}, true, order));
  EXPECT_TRUE(order.empty());
}

TEST(VisualWordOrder, ExactlyBidiMaxLineCharactersIsStillRefusedByTheSeparator) {
  // 127 characters plus one separator space is exactly BIDI_MAX_LINE, and the
  // guard trips when the *next* word would be appended.
  const std::string a(BIDI_MAX_LINE - 1, 'x');
  std::vector<uint16_t> order;
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({a, kHebrewShalom}, true, order));
  EXPECT_TRUE(order.empty());
}

TEST(VisualWordOrder, AnEmptyWordInAHomogeneousLineStillUsesTheFastPath) {
  // The homogeneous fast path indexes words, not characters, so a word that
  // contributes no codepoints is still placed.
  std::vector<uint16_t> order;
  ASSERT_TRUE(BidiUtils::computeVisualWordOrder({kHebrewShalom, "", kHebrewSefer}, true, order));
  EXPECT_EQ(order, (std::vector<uint16_t>{2, 1, 0}));
}

TEST(VisualWordOrder, AnEmptyWordInAMixedLineAbandonsTheReorder) {
  // On the UAX#9 path a word with no codepoints has no anchor position, so the
  // index mapping comes up short and the caller is told to keep logical order.
  std::vector<uint16_t> order;
  EXPECT_FALSE(BidiUtils::computeVisualWordOrder({kHebrewShalom, "", "alpha"}, true, order));
  EXPECT_TRUE(order.empty());
}

// ---------------------------------------------------------------------------
// applyBidiVisual guards
// ---------------------------------------------------------------------------

TEST(ApplyBidiVisual, NullAndEmptyInputAreRefused) {
  std::string out = "stale";
  EXPECT_FALSE(BidiUtils::applyBidiVisual(nullptr, out));
  EXPECT_FALSE(BidiUtils::applyBidiVisual("", out));
  EXPECT_EQ(out, "stale") << "a refused call leaves the caller's buffer alone";
}

TEST(ApplyBidiVisual, InputLongerThanBidiMaxLineIsRefusedWithoutTouchingTheOutput) {
  const std::string tooLong(BIDI_MAX_LINE + 1, 'a');
  std::string out = "stale";
  EXPECT_FALSE(BidiUtils::applyBidiVisual(tooLong.c_str(), out));
  EXPECT_EQ(out, "stale");
}

TEST(ApplyBidiVisual, ExactlyBidiMaxLineCharactersIsAccepted) {
  const std::string atLimit(BIDI_MAX_LINE, 'a');
  std::string out;
  EXPECT_TRUE(BidiUtils::applyBidiVisual(atLimit.c_str(), out));
  EXPECT_EQ(out.size(), static_cast<size_t>(BIDI_MAX_LINE));
}

TEST(ApplyBidiVisual, MultibyteInputCountsCodepointsNotBytes) {
  // Each Hebrew letter is two bytes, so BIDI_MAX_LINE of them is 256 bytes.
  const std::string atLimit = repeatUtf8("\xd7\x90", BIDI_MAX_LINE);
  std::string out;
  EXPECT_TRUE(BidiUtils::applyBidiVisual(atLimit.c_str(), out));
  const std::string overLimit = repeatUtf8("\xd7\x90", BIDI_MAX_LINE + 1);
  EXPECT_FALSE(BidiUtils::applyBidiVisual(overLimit.c_str(), out));
}

TEST(ApplyBidiVisual, DecodingStopsAtInvalidUtf8) {
  // "AB" then a stray continuation byte then "CD": only the prefix survives.
  std::string out;
  ASSERT_TRUE(BidiUtils::applyBidiVisual("AB\x80"
                                         "CD",
                                         out));
  EXPECT_EQ(out, "AB");
}

TEST(ApplyBidiVisual, AnRtlRunIsEmittedInVisualOrder) {
  std::string out;
  ASSERT_TRUE(BidiUtils::applyBidiVisual(kHebrewSefer, out, 1));
  // Same letters, reversed: visual order is right-to-left.
  EXPECT_EQ(out, "\xd7\xa8\xd7\xa4\xd7\xa1");
}

TEST(ApplyBidiVisual, LatinTextIsUnchangedUnderEveryParagraphLevel) {
  for (const int level : {-1, 0, 1}) {
    std::string out;
    ASSERT_TRUE(BidiUtils::applyBidiVisual("Hello", out, level)) << "level " << level;
    EXPECT_EQ(out, "Hello") << "level " << level;
  }
}

// ---------------------------------------------------------------------------
// UTF-8 decoding: well-formed
// ---------------------------------------------------------------------------

TEST(Utf8Decode, DecodesEveryEncodedLength) {
  EXPECT_EQ(decodeOne("A"), 0x41u);
  EXPECT_EQ(decodeOne("\xc3\xa9"), 0xE9u);              // e-acute
  EXPECT_EQ(decodeOne("\xe2\x82\xac"), 0x20ACu);        // euro sign
  EXPECT_EQ(decodeOne("\xf0\x9f\x98\x80"), 0x1F600u);   // grinning face
  EXPECT_EQ(decodeOne("\xf4\x8f\xbf\xbf"), 0x10FFFFu);  // the last valid codepoint
}

TEST(Utf8Decode, AdvancesTheCursorByTheEncodedLength) {
  const char* text = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  const auto* start = p;
  EXPECT_EQ(utf8NextCodepoint(&p), 0x41u);
  EXPECT_EQ(p - start, 1);
  EXPECT_EQ(utf8NextCodepoint(&p), 0xE9u);
  EXPECT_EQ(p - start, 3);
  EXPECT_EQ(utf8NextCodepoint(&p), 0x20ACu);
  EXPECT_EQ(p - start, 6);
  EXPECT_EQ(utf8NextCodepoint(&p), 0x1F600u);
  EXPECT_EQ(p - start, 10);
  EXPECT_EQ(utf8NextCodepoint(&p), 0u) << "the terminator decodes to zero";
}

TEST(Utf8Decode, AppendRoundTripsEveryEncodedLength) {
  for (const uint32_t cp : {0x41u, 0x7Fu, 0x80u, 0x7FFu, 0x800u, 0xFFFDu, 0x10000u, 0x10FFFFu}) {
    std::string encoded;
    utf8AppendCodepoint(cp, encoded);
    const auto* p = reinterpret_cast<const unsigned char*>(encoded.c_str());
    EXPECT_EQ(utf8NextCodepoint(&p), cp);
    EXPECT_EQ(*p, 0) << "the whole encoding was consumed for codepoint " << cp;
  }
}

// ---------------------------------------------------------------------------
// UTF-8 decoding: malformed (Constitution VI)
// ---------------------------------------------------------------------------

TEST(Utf8Decode, StrayContinuationBytesBecomeTheReplacementGlyphAndAdvanceOne) {
  for (const char bad : {'\x80', '\xA5', '\xBF'}) {
    const char buf[3] = {bad, 'A', 0};
    const auto* p = reinterpret_cast<const unsigned char*>(buf);
    EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH));
    EXPECT_EQ(utf8NextCodepoint(&p), 0x41u) << "the decoder resynchronises on the next byte";
  }
}

TEST(Utf8Decode, InvalidLeadBytesBecomeTheReplacementGlyph) {
  for (const char bad : {'\xFE', '\xFF'}) {
    const char buf[3] = {bad, 'A', 0};
    const auto* p = reinterpret_cast<const unsigned char*>(buf);
    EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH));
    EXPECT_EQ(utf8NextCodepoint(&p), 0x41u);
  }
}

TEST(Utf8Decode, AMissingContinuationByteConsumesOnlyTheBytesItValidated) {
  // 0xE2 promises two continuation bytes but the second is an ASCII 'A'.
  const char* text = "\xe2\x82"
                     "AB";
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  const auto* start = p;
  EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH));
  EXPECT_EQ(p - start, 2) << "the two valid bytes are consumed, the 'A' is not";
  EXPECT_EQ(utf8NextCodepoint(&p), 0x41u);
  EXPECT_EQ(utf8NextCodepoint(&p), 0x42u);
}

TEST(Utf8Decode, ASequenceTruncatedByTheTerminatorDoesNotReadPastIt) {
  const char* text = "A\xe2\x82";  // three-byte sequence cut short by the NUL
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  EXPECT_EQ(utf8NextCodepoint(&p), 0x41u);
  EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH));
  EXPECT_EQ(utf8NextCodepoint(&p), 0u) << "decoding ends at the terminator";
}

TEST(Utf8Decode, AFourByteSequenceTruncatedAfterOneContinuationByteIsRejected) {
  const char* text = "\xf0\x9f";
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH));
  EXPECT_EQ(*p, 0);
}

TEST(Utf8Decode, OverlongEncodingsAreRejectedAtEveryLength) {
  const char* overlongs[] = {
      "\xc0\x80",              // NUL encoded in two bytes
      "\xc1\xbf",              // U+007F encoded in two bytes
      "\xe0\x80\x80",          // three-byte overlong
      "\xe0\x9f\xbf",          // U+07FF encoded in three bytes
      "\xf0\x80\x80\x80",      // four-byte overlong
      "\xf0\x8f\xbf\xbf",      // U+FFFF encoded in four bytes
  };
  for (const char* bad : overlongs) {
    const auto* p = reinterpret_cast<const unsigned char*>(bad);
    const auto* start = p;
    EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH)) << bad;
    EXPECT_EQ(p - start, 1) << "rejected sequences advance one byte so the scan resynchronises";
  }
}

TEST(Utf8Decode, SurrogatesAndOutOfRangeValuesAreRejected) {
  const char* bad[] = {
      "\xed\xa0\x80",      // U+D800, high surrogate
      "\xed\xbf\xbf",      // U+DFFF, low surrogate
      "\xf4\x90\x80\x80",  // U+110000, past the Unicode range
      "\xf7\xbf\xbf\xbf",  // U+1FFFFF
  };
  for (const char* s : bad) {
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    EXPECT_EQ(utf8NextCodepoint(&p), static_cast<uint32_t>(REPLACEMENT_GLYPH)) << s;
  }
}

TEST(Utf8Decode, AStringOfNothingButGarbageTerminates) {
  const char* garbage = "\x80\xBF\xFE\xFF\xC0\xC0";
  const auto* p = reinterpret_cast<const unsigned char*>(garbage);
  int decoded = 0;
  while (utf8NextCodepoint(&p) != 0) {
    decoded++;
    ASSERT_LT(decoded, 32) << "the decoder must always make forward progress";
  }
  EXPECT_GT(decoded, 0);
}

// ---------------------------------------------------------------------------
// UTF-8 truncation helpers
// ---------------------------------------------------------------------------

TEST(Utf8Truncate, SafeTruncateKeepsACompleteBuffer) {
  const char* text = "A\xc3\xa9\xe2\x82\xac";
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 6), 6);
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 3), 3) << "ends on a complete two-byte sequence";
}

TEST(Utf8Truncate, SafeTruncateDropsAnIncompleteTrailingSequence) {
  const char* text = "A\xc3\xa9\xe2\x82\xac";
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 5), 3) << "the three-byte euro is cut, so it is dropped";
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 4), 3);
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 2), 1) << "the two-byte e-acute is cut, so it is dropped";
}

TEST(Utf8Truncate, SafeTruncateGuardsNonPositiveLengths) {
  const char* text = "ABC";
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 0), 0);
  EXPECT_EQ(utf8SafeTruncateBuffer(text, -4), 0);
}

TEST(Utf8Truncate, SafeTruncateKeepsALeadByteThatStartsTheBuffer) {
  // Current behaviour: a sequence whose lead byte is at offset 0 is never
  // dropped, so a buffer that is a single cut sequence comes back unchanged.
  const char* text = "\xe2\x82";
  EXPECT_EQ(utf8SafeTruncateBuffer(text, 2), 2);
  EXPECT_EQ(utf8SafeTruncateBuffer("\xc3", 1), 1);
}

TEST(Utf8Truncate, RemoveLastCharDropsWholeCodepoints) {
  std::string s = "A\xc3\xa9\xe2\x82\xac";
  EXPECT_EQ(utf8RemoveLastChar(s), 3u);
  EXPECT_EQ(s, "A\xc3\xa9");
  EXPECT_EQ(utf8RemoveLastChar(s), 1u);
  EXPECT_EQ(s, "A");
  EXPECT_EQ(utf8RemoveLastChar(s), 0u);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(utf8RemoveLastChar(s), 0u) << "removing from an empty string is safe";
}

TEST(Utf8Truncate, TruncateCharsRemovesNCodepointsAndStopsAtEmpty) {
  std::string s = "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac"
                  "A";
  utf8TruncateChars(s, 2);
  EXPECT_EQ(s, "\xe2\x82\xac\xe2\x82\xac");
  utf8TruncateChars(s, 99);
  EXPECT_TRUE(s.empty());
  utf8TruncateChars(s, 3);
  EXPECT_TRUE(s.empty());
}

TEST(Utf8Truncate, RemoveLastCharOnAStrayContinuationByteRunEmptiesTheString) {
  std::string s = "\x80\x80\x80";
  EXPECT_EQ(utf8RemoveLastChar(s), 0u);
  EXPECT_TRUE(s.empty());
}

// ---------------------------------------------------------------------------
// Script classification helpers used by the layout and fallback paths
// ---------------------------------------------------------------------------

TEST(Utf8Classify, CjkBreakableCoversTheLineBreakRangesOnly) {
  EXPECT_TRUE(utf8IsCjkBreakable(0x4E00));
  EXPECT_TRUE(utf8IsCjkBreakable(0x9FFF));
  EXPECT_FALSE(utf8IsCjkBreakable(0x4DFF));
  EXPECT_FALSE(utf8IsCjkBreakable(0xA000));
  EXPECT_TRUE(utf8IsCjkBreakable(0x3040));
  EXPECT_FALSE(utf8IsCjkBreakable('A'));
  EXPECT_FALSE(utf8IsCjkBreakable(0x2E80)) << "radicals are not a break opportunity";
}

TEST(Utf8Classify, CjkCodepointIsBroaderThanCjkBreakable) {
  EXPECT_TRUE(utf8IsCjkCodepoint(0x2E80)) << "radicals still select the CJK fallback font";
  EXPECT_TRUE(utf8IsCjkCodepoint(0x4E2D));
  EXPECT_FALSE(utf8IsCjkCodepoint('A'));
  EXPECT_FALSE(utf8IsCjkCodepoint(0x0301));
}

TEST(Utf8Classify, CombiningMarkRangesExcludeRtlNonSpacingMarks) {
  EXPECT_TRUE(utf8IsCombiningMark(0x0300));
  EXPECT_TRUE(utf8IsCombiningMark(0x036F));
  EXPECT_FALSE(utf8IsCombiningMark(0x02FF));
  EXPECT_FALSE(utf8IsCombiningMark(0x0370));
  EXPECT_FALSE(utf8IsCombiningMark(0x05B4)) << "Hebrew niqqud ride the transparent-mark path";
  EXPECT_TRUE(BidiUtils::isTransparentMark(0x05B4));
  EXPECT_FALSE(BidiUtils::isTransparentMark(0x0301));
}

}  // namespace
