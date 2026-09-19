// Host tests for the two heap-pressure policies inside SdCardFont's per-page
// mini arena (lib/EpdFont/SdCardFont.cpp):
//
//   - the arena retry: the bitmap arena is one contiguous block, so when it
//     fails to allocate prewarmStyle() reports PREWARM_ARENA_TOO_LARGE and
//     prewarm() retries with the prefix that platform::maxAllocHeap() says fits,
//     halving until it does;
//   - the underuse hysteresis: a retained arena is released after several
//     consecutive rebuilds that use less than three quarters of it.
//
// Glyph loading itself, the malformed-file matrix and the overflow ring belong
// to test/sdcard_font. Everything here is driven through wide.cpfont, whose
// 1260 contiguous glyphs let a test pick disjoint sets of a chosen bitmap size
// (dataLength = (globalIndex % 3) + 1 — see scripts/generate_test_cpfonts.py).

#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "FontSystemFixtures.h"
#include "PlatformHost.h"

// --- Array-allocation failure injection ---------------------------------
//
// The arena failure path cannot be reached on a 64-bit host by asking for a
// realistic size, so the array forms of the global allocator are replaced with
// malloc/free plus an exact-size reject list. Replacing new[] obliges us to
// replace every matching delete[] so both halves use the same allocator; the
// scalar forms are left to the platform (and to ASan). Single-threaded only,
// same as test/support/AllocCounter.cpp.
namespace alloc_fail {

constexpr size_t MAX_REJECTS = 4;
size_t rejectSizes[MAX_REJECTS] = {};
size_t rejectCount = 0;

// Fail every nothrow array allocation whose requested size is listed.
inline void arm(const std::vector<size_t>& sizes) {
  rejectCount = sizes.size() < MAX_REJECTS ? sizes.size() : MAX_REJECTS;
  for (size_t i = 0; i < rejectCount; i++) rejectSizes[i] = sizes[i];
}
inline void disarm() { rejectCount = 0; }

inline bool rejects(const size_t size) {
  for (size_t i = 0; i < rejectCount; i++) {
    if (rejectSizes[i] == size) return true;
  }
  return false;
}

}  // namespace alloc_fail

void* operator new[](const std::size_t size) {
  if (void* allocation = std::malloc(size ? size : 1)) return allocation;
  throw std::bad_alloc();
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  if (alloc_fail::rejects(size)) return nullptr;
  return std::malloc(size ? size : 1);
}

void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }

namespace {

// wide.cpfont: U+0100..U+05EB map to global glyph indices 0..1259, so a
// codepoint's bitmap length is ((cp - 0x100) % 3) + 1 bytes.
constexpr uint32_t WIDE_BASE = 0x100;
constexpr uint32_t REPLACEMENT_BYTES = 1;  // index 1260, 1260 % 3 == 0

// `count` codepoints of the given bitmap length, starting `skip` steps in.
std::vector<uint32_t> glyphSet(const uint32_t bytesPerGlyph, const uint32_t count, const uint32_t skip = 0) {
  std::vector<uint32_t> cps;
  cps.reserve(count);
  for (uint32_t k = 0; k < count; k++) {
    cps.push_back(WIDE_BASE + (bytesPerGlyph - 1) + 3 * (skip + k));
  }
  return cps;
}

std::string textOf(const uint32_t bytesPerGlyph, const uint32_t count, const uint32_t skip = 0) {
  return fontfx::utf8Of(glyphSet(bytesPerGlyph, count, skip));
}

// Bitmap bytes a request of `count` glyphs of `bytesPerGlyph` occupies once
// prewarm() has appended the replacement glyph.
constexpr uint32_t arenaBytesFor(const uint32_t bytesPerGlyph, const uint32_t count) {
  return bytesPerGlyph * count + REPLACEMENT_BYTES;
}

class SdCardFontArenaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    halstub::root = FONT_SYSTEM_RESOURCES_DIR;
    platform_host::setHeap(0, 0);
    alloc_fail::disarm();
    ASSERT_TRUE(font.load("/wide.cpfont"));
  }
  void TearDown() override {
    alloc_fail::disarm();
    halstub::root.clear();
    platform_host::setHeap(0, 0);
  }

  uint32_t residentIntervals() { return font.getEpdFont(0)->data->intervalCount; }

  SdCardFont font;
};

constexpr uint32_t TALL = 3;
constexpr uint32_t SHORT = 1;
constexpr uint32_t MEDIUM = 2;
constexpr uint32_t PAGE = 400;

}  // namespace

// --- Arena retry ---

TEST_F(SdCardFontArenaTest, BaselinePrewarmLoadsEveryRequestedGlyph) {
  EXPECT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  // Every requested codepoint is 3 apart, so each forms its own interval, plus
  // the replacement glyph prewarm() always appends.
  EXPECT_EQ(residentIntervals(), PAGE + 1);
}

TEST_F(SdCardFontArenaTest, ArenaFailureRetriesWithThePrefixMaxAllocHeapAllows) {
  const std::string text = textOf(TALL, PAGE);
  const std::vector<uint32_t> cps = glyphSet(TALL, PAGE);

  // 4 KB of the largest block is reserved as working headroom; the rest sizes
  // the retry at 900 / 3 bytes-per-glyph = 300 glyphs.
  platform_host::setHeap(0, 4096 + 900);
  alloc_fail::arm({arenaBytesFor(TALL, PAGE)});
  const int missed = font.prewarm(text.c_str());
  alloc_fail::disarm();

  // 300 of the 401 requested codepoints stayed; the dropped suffix counts as
  // missed.
  EXPECT_EQ(missed, static_cast<int>(PAGE + 1 - 300));
  ASSERT_EQ(residentIntervals(), 300u);
  const EpdFontData* data = font.getEpdFont(0)->data;
  EXPECT_EQ(data->intervals[0].first, cps.front());
  EXPECT_EQ(data->intervals[299].last, cps[299]);
  EXPECT_NE(font.getEpdFont(0)->getGlyph(cps[0]), nullptr);
}

TEST_F(SdCardFontArenaTest, ArenaRetryHalvesUntilTheBlockFits) {
  const std::string text = textOf(TALL, PAGE);

  platform_host::setHeap(0, 4096 + 900);
  // Both the full request and the first retry estimate fail; the retry halves
  // 300 -> 150, which fits.
  alloc_fail::arm({arenaBytesFor(TALL, PAGE), TALL * 300});
  const int missed = font.prewarm(text.c_str());
  alloc_fail::disarm();

  EXPECT_EQ(missed, static_cast<int>(PAGE + 1 - 150));
  EXPECT_EQ(residentIntervals(), 150u);
}

TEST_F(SdCardFontArenaTest, ArenaRetryGivesUpWhenNoBlockIsLargeEnough) {
  const std::string text = textOf(TALL, PAGE);

  // Largest block below the reserve: the retry cannot size even one glyph.
  platform_host::setHeap(0, 1024);
  alloc_fail::arm({arenaBytesFor(TALL, PAGE)});
  const int missed = font.prewarm(text.c_str());
  alloc_fail::disarm();

  EXPECT_EQ(missed, static_cast<int>(PAGE + 1));
  EXPECT_EQ(residentIntervals(), 0u);
  EXPECT_EQ(font.getEpdFont(0)->data->bitmap, nullptr);
}

// --- Underuse hysteresis ---

TEST_F(SdCardFontArenaTest, RetainedArenaSurvivesAFullyUsedRebuild) {
  ASSERT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  font.clearCache();

  // The scope used the whole arena, so the data is kept for the next page.
  EXPECT_EQ(residentIntervals(), PAGE + 1);
}

TEST_F(SdCardFontArenaTest, SustainedUnderuseReleasesTheArenaOnTheThirdLowRebuild) {
  // 1. A dense page of tall glyphs sizes the arena at 1201 bytes.
  ASSERT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(residentIntervals(), PAGE + 1);

  // 2. A disjoint page of short glyphs: the union would exceed MAX_PAGE_GLYPHS,
  //    so the rebuild is request-only and uses 401 of the 1201 bytes.
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE).c_str()), 0);
  font.clearCache();
  EXPECT_EQ(residentIntervals(), PAGE + 1) << "one low-use rebuild must not release the arena";

  // 3. An overlapping page of short glyphs: the union fits, still far under
  //    capacity.
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE, 20).c_str()), 0);
  font.clearCache();
  EXPECT_EQ(residentIntervals(), PAGE + 21) << "two low-use rebuilds must not release the arena";

  // 4. Third consecutive low-use rebuild: the arena is released.
  ASSERT_EQ(font.prewarm(textOf(MEDIUM, PAGE).c_str()), 0);
  font.clearCache();
  EXPECT_EQ(residentIntervals(), 0u);
  EXPECT_EQ(font.getEpdFont(0)->data->bitmap, nullptr);
}

TEST_F(SdCardFontArenaTest, AFullyUsedRebuildResetsTheUnderuseRun) {
  ASSERT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE, 20).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(residentIntervals(), PAGE + 21);

  // A dense page again fills the arena and clears the run counter...
  ASSERT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(residentIntervals(), PAGE + 1);

  // ...so the next two low-use rebuilds are not enough to release it, where
  // without the reset this would have been the third and fourth.
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE).c_str()), 0);
  font.clearCache();
  ASSERT_EQ(font.prewarm(textOf(SHORT, PAGE, 20).c_str()), 0);
  font.clearCache();
  EXPECT_EQ(residentIntervals(), PAGE + 21);
}

TEST_F(SdCardFontArenaTest, MetadataOnlyRebuildsNeverAdvanceTheUnderuseRun) {
  ASSERT_EQ(font.prewarm(textOf(TALL, PAGE).c_str()), 0);
  font.clearCache();

  // The same three low-use rebuilds that released the arena above, but
  // measurement-only: they load no bitmaps, so there is nothing to judge.
  for (const std::string& text :
       {textOf(SHORT, PAGE), textOf(SHORT, PAGE, 20), textOf(MEDIUM, PAGE), textOf(MEDIUM, PAGE, 20)}) {
    ASSERT_EQ(font.prewarm(text.c_str(), 0x0F, /*metadataOnly=*/true), 0);
    font.clearCache();
  }

  EXPECT_GT(residentIntervals(), 0u);
}
