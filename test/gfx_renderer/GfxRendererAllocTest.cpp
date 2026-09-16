// Allocation guards for the renderer's string-building hot paths:
//   - truncatedText() / wrappedText(), which run on every repaint of a list row, menu
//     entry or dialog and are handed book titles and file names;
//   - ensureSdCardFontReady(), which the layout engine calls once per paragraph to warm
//     an SD font's advance table, and which shapes every RTL token in that paragraph.
//
// The budgets are upper bounds rather than exact counts on purpose: how many of these
// intermediates reach the heap depends on std::string's small-buffer capacity, which is
// 22 bytes on libc++ (macOS) and 15 on libstdc++ (CI). Each budget sits far below what
// the pre-change code allocated on either library, so the guard still fails if the
// per-item allocation comes back. Measured counts on libc++ are quoted per test.
//
// AllocCounter is single-threaded by contract; these tests run on the gtest main thread.

#include <AllocCounter.h>
#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestFonts.h"

namespace {

constexpr int kTiny = 1;
// U+2026 HORIZONTAL ELLIPSIS, the marker truncatedText appends.
constexpr const char* kEllipsis = "\xe2\x80\xa6";

class GfxRendererAllocTest : public ::testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};

  void SetUp() override {
    renderer.begin();
    renderer.insertFont(kTiny, testfonts::tinyFamily());
  }

  // 'A' advances 3.0 px in the tiny test font, as does the ellipsis.
  static std::string longTitle() { return std::string(60, 'A'); }
  static std::string manyWords() {
    std::string words;
    for (int i = 0; i < 12; ++i) words += "ABCDEF ";
    return words;
  }
};

TEST_F(GfxRendererAllocTest, LongTitleIsShavedDownToTheWidestFittingPrefix) {
  // Two 'A's plus the ellipsis measure 9 px; a third would make it exactly 12.
  EXPECT_EQ(renderer.truncatedText(kTiny, longTitle().c_str(), 12), std::string("AA") + kEllipsis);
}

// Fails before the rewrite: the shrink loop built a fresh `item + ellipsis` string on
// every iteration, so shaving a 60-character title down cost 42 heap blocks for one
// repaint. Afterwards it is the working copy plus one reused candidate buffer: 2.
TEST_F(GfxRendererAllocTest, TruncatingALongTitleDoesNotAllocatePerShavedCharacter) {
  const std::string text = longTitle();
  (void)renderer.truncatedText(kTiny, text.c_str(), 12);  // warm

  size_t count = 0;
  {
    alloc_counter::CountingScope scope;
    (void)renderer.truncatedText(kTiny, text.c_str(), 12);
    count = scope.count();
  }
  EXPECT_LE(count, 6u) << "allocations scale with the number of shaved characters again";
}

TEST_F(GfxRendererAllocTest, WrappingFillsEveryLineAndEllipsizesTheLast) {
  const std::vector<std::string> lines = renderer.wrappedText(kTiny, manyWords().c_str(), 30, 4);
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "ABCDEF");
  EXPECT_EQ(lines[1], "ABCDEF");
  EXPECT_EQ(lines[2], "ABCDEF");
  EXPECT_EQ(lines[3], std::string("ABCDEF AB") + kEllipsis);
}

// Fails before the rewrite: `word` and `testLine` were rebuilt per word and the result
// vector grew by doubling — 50 heap blocks for one repaint of a wrapped label, 6 after.
TEST_F(GfxRendererAllocTest, WrappingALongLabelDoesNotAllocatePerWord) {
  const std::string words = manyWords();
  (void)renderer.wrappedText(kTiny, words.c_str(), 30, 4);  // warm

  size_t count = 0;
  {
    alloc_counter::CountingScope scope;
    (void)renderer.wrappedText(kTiny, words.c_str(), 30, 4);
    count = scope.count();
  }
  EXPECT_LE(count, 12u) << "allocations scale with the word count again";
}

// --- SD-font paragraph warm-up ----------------------------------------------

// Hebrew, so appendShapedRtlTokens() inside ensureSdCardFontReady() takes the shaping
// branch for every token. Deliberately long words: the per-token buffers only reach the
// heap once a word outgrows std::string's small buffer, which is where RTL books live
// (the built-in fonts carry no Hebrew or Arabic, so these readers always use an SD font
// and always come through this path). Glyph coverage is irrelevant — the token loop runs
// over the text before any glyph lookup.
constexpr const char* kHebrewWords[] = {
    "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d\xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d\xd7\xa2\xd7\x95"
    "\xd7\x9c\xd7\x9d",
    "\xd7\xa1\xd7\xa4\xd7\xa8\xd7\x99\xd7\x9d\xd7\x98\xd7\x95\xd7\x91\xd7\x99\xd7\x9d\xd7\xa1\xd7\xa4\xd7\xa8\xd7\x99"
    "\xd7\x9d",
    "\xd7\x9c\xd7\xa7\xd7\xa8\xd7\x95\xd7\x90\xd7\x99\xd7\x9d\xd7\xa8\xd7\x91\xd7\x99\xd7\x9d\xd7\x9c\xd7\xa7\xd7\xa8"
    "\xd7\x95",
};

// --- the invariant the shaping scratch reuse rests on ------------------------

// ensureSdCardFontReady() hands one `visual` buffer to every token of a paragraph and
// appends it only when applyBidiVisual() reports success, so two halves of that
// function's contract are what keep the reuse safe. BidiUtf8Test pins the first half (a
// refused call leaves the caller's buffer untouched). This is the second: a successful
// call must replace the whole buffer, never append to it. If it ever stopped clearing
// first, the renderer would splice the previous word's shaped form onto this one and
// every RTL paragraph would warm — and then measure against — corrupted text.
TEST(GfxRendererShapingContract, SuccessfulShapingReplacesTheWholeOutputBuffer) {
  constexpr const char* kShort = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";  // shalom, 4 letters
  const char* kLong = kHebrewWords[0];                                // 18 letters

  std::string fresh;
  ASSERT_TRUE(BidiUtils::applyBidiVisual(kShort, fresh));

  std::string reused;
  ASSERT_TRUE(BidiUtils::applyBidiVisual(kLong, reused));
  ASSERT_GT(reused.size(), fresh.size());

  ASSERT_TRUE(BidiUtils::applyBidiVisual(kShort, reused));
  EXPECT_EQ(reused, fresh) << "a reused buffer must hold only the latest token's shaped form";
}

class GfxRendererSdFontAllocTest : public ::testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};
  SdCardFont sdFont;
  std::string sandbox;

  void SetUp() override {
    sandbox = std::string(GFX_SANDBOX_DIR) + "/sdfont_" + std::to_string(::getpid());
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
    std::filesystem::create_directories(sandbox, ec);
    halstub::root = sandbox;

    std::ifstream in(std::string(GFX_CPFONT_DIR) + "/valid_basic.cpfont", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(sandbox + "/font.cpfont", std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();

    renderer.begin();
    ASSERT_TRUE(sdFont.load("/font.cpfont"));
    renderer.registerSdCardFont(kTiny, &sdFont);
  }

  void TearDown() override {
    renderer.clearSdCardFonts();
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
    halstub::root.clear();
  }

  static std::deque<std::string> paragraph() {
    std::deque<std::string> words;
    for (int i = 0; i < 10; ++i) {
      for (const char* w : kHebrewWords) words.emplace_back(w);
    }
    return words;
  }
};

TEST_F(GfxRendererSdFontAllocTest, WarmingAnSdFontBuildsItsAdvanceTable) {
  const std::deque<std::string> words = paragraph();
  renderer.ensureSdCardFontReady(kTiny, words, false, 0x01);
  EXPECT_TRUE(sdFont.hasAdvanceTable());
}

// Fails before the scratch hoist: appendShapedRtlTokens() built a fresh token and visual
// buffer per word, so a 30-word RTL paragraph cost 68 heap blocks; 10 afterwards.
TEST_F(GfxRendererSdFontAllocTest, ShapingAnRtlParagraphDoesNotAllocatePerWord) {
  const std::deque<std::string> words = paragraph();
  renderer.ensureSdCardFontReady(kTiny, words, false, 0x01);  // warm

  size_t count = 0;
  {
    alloc_counter::CountingScope scope;
    renderer.ensureSdCardFontReady(kTiny, words, false, 0x01);
    count = scope.count();
  }
  EXPECT_LE(count, 20u) << "shaping allocations scale with the paragraph's word count again";
}

}  // namespace
