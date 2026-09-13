// Host-side tests for lib/EpdFont/SdCardFont.cpp and SdCardFontRegistry.cpp:
// .cpfont v4 loading/validation, prewarm glyph residency, the on-demand
// overflow path, kerning/ligature wiring, the advance table, and SD-card
// font family discovery.
//
// Fixtures are generated at build time by scripts/generate_test_cpfonts.py
// (*.cpfont is gitignored, so they cannot be committed). The glyph formulas
// asserted here mirror that script — see its header comment.

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "EpdFont.h"
#include "EpdFontFamily.h"
#include "HalStorage.h"
#include "SdCardFont.h"
#include "SdCardFontRegistry.h"

namespace {

// Fixture paths are device-style ("/valid_basic.cpfont"): the HalStorage stub
// remaps them into CPFONT_RESOURCES_DIR (halstub::root, set in SetUp). This
// mirrors the device's short absolute SD paths and keeps SdCardFont's
// 128-byte filePath_ limit out of play on deep host build paths.
std::string res(const char* name) { return std::string("/") + name; }
// Host-absolute path for direct stdio access (independent verification).
std::string resHost(const char* name) { return std::string(CPFONT_RESOURCES_DIR "/") + name; }

// --- Fixture glyph formulas (must match scripts/generate_test_cpfonts.py) ---

int32_t fixtureGlyphIndex(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7A) return static_cast<int32_t>(cp - 0x20);
  if (cp == 0xFB01) return 91;
  if (cp == 0xFFFD) return 92;
  return -1;
}
uint8_t expHeight(int32_t i) { return static_cast<uint8_t>(i % 3 + 1); }
uint16_t expAdvance(int32_t i, int base = 100) { return static_cast<uint16_t>(base + i); }
uint8_t expBitmapByte(int32_t i, int k, int base = 0x40) { return static_cast<uint8_t>(base + i * 7 + k * 3); }

// Assert a PREWARMED glyph: metrics from the file plus bitmap bytes resident
// in the mini arena (data->bitmap + rewritten dataOffset).
void expectResidentGlyph(const EpdFont* font, uint32_t cp, int advBase = 100, int bmpBase = 0x40) {
  const int32_t idx = fixtureGlyphIndex(cp);
  ASSERT_GE(idx, 0);
  const EpdGlyph* glyph = font->getGlyph(cp);
  ASSERT_NE(glyph, nullptr) << "U+" << std::hex << cp;
  EXPECT_EQ(glyph->width, 4);
  EXPECT_EQ(glyph->height, expHeight(idx));
  EXPECT_EQ(glyph->advanceX, expAdvance(idx, advBase));
  EXPECT_EQ(glyph->left, static_cast<int16_t>(idx % 3 - 1));
  EXPECT_EQ(glyph->top, 10);
  ASSERT_EQ(glyph->dataLength, expHeight(idx));
  ASSERT_NE(font->data->bitmap, nullptr);
  const uint8_t* bmp = font->data->bitmap + glyph->dataOffset;
  for (int k = 0; k < glyph->dataLength; k++) {
    EXPECT_EQ(bmp[k], expBitmapByte(idx, k, bmpBase)) << "U+" << std::hex << cp << " byte " << k;
  }
}

uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t hash = 2166136261u) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

void makeDirs(const std::string& path) {
  for (size_t pos = path.find('/', 1); pos != std::string::npos; pos = path.find('/', pos + 1)) {
    ::mkdir(path.substr(0, pos).c_str(), 0755);
  }
  ::mkdir(path.c_str(), 0755);
}

void touch(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr) << path;
  std::fclose(f);
}

class SdCardFontTest : public ::testing::Test {
 protected:
  void SetUp() override {
    halstub::root = CPFONT_RESOURCES_DIR;
    ESP = EspHostStub{};
  }
  void TearDown() override {
    halstub::root.clear();
    ESP = EspHostStub{};
  }
};

using SdCardFontLoadTest = SdCardFontTest;
using SdCardFontPrewarmTest = SdCardFontTest;
using SdCardFontKernLigTest = SdCardFontTest;
using SdCardFontAdvanceTest = SdCardFontTest;
using SdCardFontMultiStyleTest = SdCardFontTest;

}  // namespace

// --- Loading and format validation ---

TEST_F(SdCardFontLoadTest, LoadValidBasicExposesRegularStyle) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  EXPECT_EQ(font.styleCount(), 1);
  EXPECT_TRUE(font.hasStyle(EpdFontFamily::REGULAR));
  EXPECT_FALSE(font.hasStyle(EpdFontFamily::BOLD));
  ASSERT_NE(font.getEpdFont(EpdFontFamily::REGULAR), nullptr);
  EXPECT_EQ(font.getEpdFont(EpdFontFamily::BOLD), nullptr);

  // Stub font data carries the style header metrics before any prewarm.
  const EpdFontData* data = font.getEpdFont(0)->data;
  EXPECT_EQ(data->advanceY, 20);
  EXPECT_EQ(data->ascender, 14);
  EXPECT_EQ(data->descender, -4);
  EXPECT_TRUE(data->is2Bit);
  EXPECT_EQ(data->intervalCount, 0u);  // glyphs are not resident yet

  // Coverage answers from the RAM interval index without prewarm.
  EXPECT_TRUE(font.getEpdFont(0)->hasCodepoint('A'));
  EXPECT_TRUE(font.getEpdFont(0)->hasCodepoint(0xFFFD));
  EXPECT_FALSE(font.getEpdFont(0)->hasCodepoint(0x2000));
}

TEST_F(SdCardFontLoadTest, MalformedFilesAreRejected) {
  // interval_overlap re-covers codepoints of a previous interval (duplicate
  // codepoints in the font): pinned as a load-time rejection.
  const char* kMalformed[] = {
      "bad_magic.cpfont",
      "bad_version.cpfont",
      "zero_styles.cpfont",
      "too_many_styles.cpfont",
      "style_id_oob.cpfont",
      "truncated_header.cpfont",
      "truncated_toc.cpfont",
      "huge_interval_count.cpfont",
      "huge_glyph_count.cpfont",
      "huge_kern_count.cpfont",
      "interval_first_gt_last.cpfont",
      "interval_overlap.cpfont",
      "interval_offset_mismatch.cpfont",
      "interval_span_too_big.cpfont",
      // interval_offset_overrun under-declares glyphCount so the last
      // interval's (otherwise consistent) offset indexes past the glyph
      // table: pinned as a load-time rejection because ONLY the
      // offset-overrun check can catch it — every other interval invariant
      // (ordering, overlap, span, cumulative offsets) holds.
      "interval_offset_overrun.cpfont",
      "truncated_intervals.cpfont",
  };
  for (const char* name : kMalformed) {
    SdCardFont font;
    EXPECT_FALSE(font.load(res(name).c_str())) << name;
    EXPECT_EQ(font.getEpdFont(0), nullptr) << name;
    EXPECT_EQ(font.styleCount(), 0) << name;
  }
}

TEST_F(SdCardFontLoadTest, NonexistentFileFailsToLoad) {
  SdCardFont font;
  EXPECT_FALSE(font.load(res("does_not_exist.cpfont").c_str()));
}

TEST_F(SdCardFontLoadTest, FailedLoadClearsPreviousFont) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_NE(font.getEpdFont(0), nullptr);

  EXPECT_FALSE(font.load(res("bad_magic.cpfont").c_str()));
  EXPECT_EQ(font.getEpdFont(0), nullptr);
  EXPECT_EQ(font.contentHash(), 0u);

  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  EXPECT_NE(font.getEpdFont(0), nullptr);
}

TEST_F(SdCardFontLoadTest, ContentHashIsFnvOverHeaderAndToc) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));

  // Independent recompute: FNV-1a over the 32-byte header, then each 32-byte
  // style TOC entry.
  std::FILE* f = std::fopen(resHost("valid_basic.cpfont").c_str(), "rb");
  ASSERT_NE(f, nullptr);
  uint8_t buf[64];
  ASSERT_EQ(std::fread(buf, 1, 64, f), 64u);
  std::fclose(f);
  EXPECT_EQ(font.contentHash(), fnv1a(buf, 64));

  // Stable across reloads; different file, different hash.
  const uint32_t first = font.contentHash();
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  EXPECT_EQ(font.contentHash(), first);
  ASSERT_TRUE(font.load(res("valid_multistyle.cpfont").c_str()));
  EXPECT_NE(font.contentHash(), first);
}

// --- Prewarm and the real glyph lookup path ---

TEST_F(SdCardFontPrewarmTest, PrewarmMakesGlyphsAndBitmapsResident) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi"), 0);

  const EpdFont* epd = font.getEpdFont(0);
  expectResidentGlyph(epd, 'H');
  expectResidentGlyph(epd, 'i');
  // The replacement glyph is force-included in every prewarm.
  expectResidentGlyph(epd, 0xFFFD);
}

TEST_F(SdCardFontPrewarmTest, PrewarmReportsUncoveredCodepoints) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  // U+2603 SNOWMAN is outside coverage: reported missing, the rest loads.
  EXPECT_EQ(font.prewarm("Hi\xE2\x98\x83"), 1);
  expectResidentGlyph(font.getEpdFont(0), 'H');
}

TEST_F(SdCardFontPrewarmTest, UncoveredCodepointFallsBackToReplacementGlyph) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi"), 0);

  const EpdFont* epd = font.getEpdFont(0);
  const EpdGlyph* replacement = epd->getGlyph(0xFFFD);
  ASSERT_NE(replacement, nullptr);
  // Covered-by-nothing codepoint resolves to the replacement glyph object.
  EXPECT_EQ(epd->getGlyph(0x2603), replacement);
}

TEST_F(SdCardFontPrewarmTest, OverflowLoadsNonPrewarmedGlyphOnDemand) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi"), 0);

  const EpdFont* epd = font.getEpdFont(0);
  const int32_t idx = fixtureGlyphIndex('Z');
  const EpdGlyph* glyph = epd->getGlyph('Z');  // not prewarmed: SD overflow path
  ASSERT_NE(glyph, nullptr);
  EXPECT_EQ(glyph->advanceX, expAdvance(idx));
  EXPECT_EQ(glyph->height, expHeight(idx));
  EXPECT_TRUE(font.isOverflowGlyph(glyph));

  const uint8_t* bmp = font.getOverflowBitmap(glyph);
  ASSERT_NE(bmp, nullptr);
  for (int k = 0; k < glyph->dataLength; k++) {
    EXPECT_EQ(bmp[k], expBitmapByte(idx, k));
  }

  // Second lookup is served from the overflow ring: same slot object.
  EXPECT_EQ(epd->getGlyph('Z'), glyph);
}

TEST_F(SdCardFontPrewarmTest, OverflowRingEvictionKeepsServingCorrectGlyphs) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi"), 0);
  const EpdFont* epd = font.getEpdFont(0);

  // 9 distinct on-demand glyphs overflow the 8-slot ring.
  const char cps[] = {'!', '"', '#', '$', '%', '&', '\'', '(', ')'};
  for (char c : cps) {
    const uint32_t cp = static_cast<uint32_t>(c);
    const EpdGlyph* glyph = epd->getGlyph(cp);
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->advanceX, expAdvance(fixtureGlyphIndex(cp)));
    const uint8_t* bmp = font.getOverflowBitmap(glyph);
    ASSERT_NE(bmp, nullptr);
    EXPECT_EQ(bmp[0], expBitmapByte(fixtureGlyphIndex(cp), 0));
  }

  // '!' was evicted; a re-request reloads it correctly.
  const EpdGlyph* again = epd->getGlyph('!');
  ASSERT_NE(again, nullptr);
  EXPECT_EQ(again->advanceX, expAdvance(fixtureGlyphIndex('!')));
  const uint8_t* bmp = font.getOverflowBitmap(again);
  ASSERT_NE(bmp, nullptr);
  EXPECT_EQ(bmp[0], expBitmapByte(fixtureGlyphIndex('!'), 0));
}

TEST_F(SdCardFontPrewarmTest, SubsetPrewarmHitsResidentMiniWithoutSdReads) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hello"), 0);

  const uint32_t seeksAfterFirst = font.getStats().seekCount;
  EXPECT_GT(seeksAfterFirst, 0u);

  // Every codepoint already resident: the idle-prewarm hit must not touch SD.
  EXPECT_EQ(font.prewarm("ell"), 0);
  EXPECT_EQ(font.getStats().seekCount, seeksAfterFirst);
}

TEST_F(SdCardFontPrewarmTest, PrewarmUnionsWithResidentGlyphs) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("abc"), 0);
  ASSERT_EQ(font.prewarm("xyz"), 0);  // rebuild unions {a,b,c} into the request

  const uint32_t seeks = font.getStats().seekCount;
  // Both the old and the new page are now resident: no further SD reads.
  EXPECT_EQ(font.prewarm("abc"), 0);
  EXPECT_EQ(font.prewarm("xyz"), 0);
  EXPECT_EQ(font.getStats().seekCount, seeks);

  expectResidentGlyph(font.getEpdFont(0), 'a');
  expectResidentGlyph(font.getEpdFont(0), 'z');
}

TEST_F(SdCardFontPrewarmTest, MetadataOnlyPrewarmServesMetricsThenFullRebuildLoadsBitmaps) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi", 0x0F, /*metadataOnly=*/true), 0);

  // Metrics are available without bitmap I/O.
  const EpdGlyph* glyph = font.getEpdFont(0)->getGlyph('H');
  ASSERT_NE(glyph, nullptr);
  EXPECT_EQ(glyph->advanceX, expAdvance(fixtureGlyphIndex('H')));

  // A full (render) prewarm may not reuse the metadata-only mini: it must
  // rebuild from SD and make the bitmaps resident.
  const uint32_t seeksBefore = font.getStats().seekCount;
  ASSERT_EQ(font.prewarm("Hi"), 0);
  EXPECT_GT(font.getStats().seekCount, seeksBefore);
  expectResidentGlyph(font.getEpdFont(0), 'H');
}

TEST_F(SdCardFontPrewarmTest, TruncatedGlyphTablePrewarmFailsGracefully) {
  SdCardFont font;
  // Header + intervals are intact, so load() succeeds...
  ASSERT_TRUE(font.load(res("truncated_glyphs.cpfont").c_str()));
  // ...but the glyph table is cut: all 3 requested glyphs (H, i, U+FFFD) miss.
  EXPECT_EQ(font.prewarm("Hi"), 3);
  // The font falls back to the stub (nothing resident), not a crash.
  EXPECT_EQ(font.getEpdFont(0)->data->intervalCount, 0u);
}

TEST_F(SdCardFontPrewarmTest, BitmapPastEofFailsGracefullyButServesIntactPrefix) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("bitmap_past_eof.cpfont").c_str()));
  // Bitmap section is cut to 4 bytes: the prewarm's bitmap reads fail.
  EXPECT_EQ(font.prewarm("Hi"), 3);

  const EpdFont* epd = font.getEpdFont(0);
  // On-demand load of 'H' also fails (bitmap at offset 79), and the
  // replacement's bitmap is past EOF too — the lookup must yield nullptr,
  // never a glyph pointing at unread memory.
  EXPECT_EQ(epd->getGlyph('H'), nullptr);
  // Glyph 0 (space) has its 1 bitmap byte inside the intact prefix and is
  // still served through the overflow path.
  const EpdGlyph* space = epd->getGlyph(' ');
  ASSERT_NE(space, nullptr);
  EXPECT_TRUE(font.isOverflowGlyph(space));
  const uint8_t* bmp = font.getOverflowBitmap(space);
  ASSERT_NE(bmp, nullptr);
  EXPECT_EQ(bmp[0], expBitmapByte(0, 0));
}

namespace {
const char* twoStringGetter(const void* ctx, uint32_t index) {
  const char* const* strings = static_cast<const char* const*>(ctx);
  return strings[index];
}
}  // namespace

TEST_F(SdCardFontPrewarmTest, MultiStringPrewarmFailsCleanlyWhenHeapBudgetIsZero) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));

  // 16KB free == exactly the prewarm headroom: zero glyph budget.
  ESP.freeHeap = 16 * 1024;
  const char* strings[] = {"Hello", "World"};
  EXPECT_EQ(font.prewarm(&twoStringGetter, strings, 2), -1);

  // Restored heap: the same request succeeds.
  ESP.freeHeap = EspHostStub{}.freeHeap;
  EXPECT_EQ(font.prewarm(&twoStringGetter, strings, 2), 0);
  expectResidentGlyph(font.getEpdFont(0), 'W');
}

TEST_F(SdCardFontPrewarmTest, ClearCacheRetainsMiniUnlessHeapIsTight) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("Hi"), 0);

  // Plenty of heap: clearCache keeps the mini, the re-prewarm is SD-free.
  font.clearCache();
  const uint32_t seeks = font.getStats().seekCount;
  EXPECT_EQ(font.prewarm("Hi"), 0);
  EXPECT_EQ(font.getStats().seekCount, seeks);

  // Tight heap (< 40KB retention floor): clearCache drops the mini and the
  // next prewarm must re-read from SD.
  ESP.freeHeap = 30 * 1024;
  font.clearCache();
  ESP.freeHeap = EspHostStub{}.freeHeap;
  EXPECT_EQ(font.prewarm("Hi"), 0);
  EXPECT_GT(font.getStats().seekCount, seeks);
}

// --- Kerning and ligatures through the mini tables ---

TEST_F(SdCardFontKernLigTest, KerningIsServedFromThePerPageMiniMatrix) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("AV."), 0);

  const EpdFont* epd = font.getEpdFont(0);
  // Fixture kern data: A=left class 1, T=left class 2, V=right class 1,
  // .=right class 2; matrix rows [-16, -8], [-4, 0].
  EXPECT_EQ(epd->getKerning('A', 'V'), -16);
  EXPECT_EQ(epd->getKerning('A', '.'), -8);
  // 'T' has a kern class in the FILE but is not on this page: the mini matrix
  // intentionally treats it as unkerned.
  EXPECT_EQ(epd->getKerning('T', 'V'), 0);
  // 'V' has no LEFT class anywhere.
  EXPECT_EQ(epd->getKerning('V', 'A'), 0);
}

TEST_F(SdCardFontKernLigTest, KernFreePrewarmCanBeToppedUpWithoutRebuild) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));

  // UI-path prewarm skips kern data entirely.
  ASSERT_EQ(font.prewarm("AV", 0x0F, false, /*loadKernLig=*/false), 0);
  EXPECT_EQ(font.getEpdFont(0)->getKerning('A', 'V'), 0);

  // Reader-path prewarm over the same codepoints: subset hit + kern top-up.
  ASSERT_EQ(font.prewarm("AV", 0x0F, false, /*loadKernLig=*/true), 0);
  EXPECT_EQ(font.getEpdFont(0)->getKerning('A', 'V'), -16);
}

TEST_F(SdCardFontKernLigTest, LigatureSubstitutionUsesLoadedPairs) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.prewarm("fi"), 0);

  const EpdFont* epd = font.getEpdFont(0);
  EXPECT_EQ(epd->getLigature('f', 'i'), 0xFB01u);
  EXPECT_EQ(epd->getLigature('f', 'x'), 0u);

  // applyLigatures consumes the 'i' and returns the ligature codepoint.
  const char* rest = "i!";
  EXPECT_EQ(epd->applyLigatures('f', rest), 0xFB01u);
  EXPECT_STREQ(rest, "!");

  // The ligature output glyph was prewarmed alongside its input pair.
  expectResidentGlyph(epd, 0xFB01);
}

// --- Advance table ---

TEST_F(SdCardFontAdvanceTest, BuildAdvanceTableServesFixedPointAdvances) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  EXPECT_FALSE(font.hasAdvanceTable());

  ASSERT_EQ(font.buildAdvanceTable("Hio"), 0);
  EXPECT_TRUE(font.hasAdvanceTable());
  EXPECT_EQ(font.getAdvance('H', 0), expAdvance(fixtureGlyphIndex('H')));
  EXPECT_EQ(font.getAdvance('i', 0), expAdvance(fixtureGlyphIndex('i')));
  EXPECT_EQ(font.getAdvance('o', 0), expAdvance(fixtureGlyphIndex('o')));
  // Covered but never fetched: not in the table.
  EXPECT_EQ(font.getAdvance('z', 0), 0);
}

TEST_F(SdCardFontAdvanceTest, UncoveredCodepointGetsReplacementAdvance) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  // U+03B8 is outside coverage: buildAdvanceTable maps it to the replacement
  // glyph's advance rather than dropping it (layout still gets a width).
  ASSERT_EQ(font.buildAdvanceTable("H\xCE\xB8"), 0);
  EXPECT_EQ(font.getAdvance(0x3B8, 0), expAdvance(fixtureGlyphIndex(0xFFFD)));
}

TEST_F(SdCardFontAdvanceTest, AdvanceTablePersistsAcrossClearCacheUntilPersistentClear) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_basic.cpfont").c_str()));
  ASSERT_EQ(font.buildAdvanceTable("H"), 0);

  font.clearCache();  // must NOT drop the advance table
  EXPECT_EQ(font.getAdvance('H', 0), expAdvance(fixtureGlyphIndex('H')));

  font.clearPersistentCache();
  EXPECT_EQ(font.getAdvance('H', 0), 0);
  EXPECT_FALSE(font.hasAdvanceTable());
}

// --- Multi-style files ---

TEST_F(SdCardFontMultiStyleTest, StylePresenceAndFallbackResolution) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_multistyle.cpfont").c_str()));
  EXPECT_EQ(font.styleCount(), 2);
  EXPECT_TRUE(font.hasStyle(EpdFontFamily::REGULAR));
  EXPECT_FALSE(font.hasStyle(EpdFontFamily::BOLD));
  EXPECT_TRUE(font.hasStyle(EpdFontFamily::ITALIC));
  EXPECT_FALSE(font.hasStyle(EpdFontFamily::BOLD_ITALIC));

  EXPECT_EQ(font.resolveStyle(EpdFontFamily::ITALIC), EpdFontFamily::ITALIC);
  // Bold is absent: falls back to regular per the fallback table.
  EXPECT_EQ(font.resolveStyle(EpdFontFamily::BOLD), EpdFontFamily::REGULAR);
  EXPECT_EQ(font.resolveStyleMask(1 << EpdFontFamily::BOLD), 1 << EpdFontFamily::REGULAR);

  // Style headers are per-style: italic fixture uses advanceY 21.
  EXPECT_EQ(font.getEpdFont(EpdFontFamily::REGULAR)->data->advanceY, 20);
  EXPECT_EQ(font.getEpdFont(EpdFontFamily::ITALIC)->data->advanceY, 21);
}

TEST_F(SdCardFontMultiStyleTest, PerStylePrewarmLoadsThatStylesData) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_multistyle.cpfont").c_str()));

  // Prewarm only the italic style. Italic fixture: advance base 200,
  // bitmap base 0x80.
  ASSERT_EQ(font.prewarm("Hi", 1 << EpdFontFamily::ITALIC), 0);
  expectResidentGlyph(font.getEpdFont(EpdFontFamily::ITALIC), 'H', 200, 0x80);

  // Regular was not prewarmed; its glyph still arrives via the overflow path
  // with regular-style metrics.
  const EpdGlyph* regular = font.getEpdFont(EpdFontFamily::REGULAR)->getGlyph('H');
  ASSERT_NE(regular, nullptr);
  EXPECT_EQ(regular->advanceX, expAdvance(fixtureGlyphIndex('H'), 100));
  EXPECT_TRUE(font.isOverflowGlyph(regular));
}

TEST_F(SdCardFontMultiStyleTest, AstralPlaneFontLoadsAndServesGlyphs) {
  SdCardFont font;
  ASSERT_TRUE(font.load(res("valid_astral.cpfont").c_str()));

  const EpdFont* epd = font.getEpdFont(0);
  EXPECT_TRUE(epd->hasCodepoint(0x1F600));
  EXPECT_TRUE(epd->hasCodepoint(0x1F603));
  EXPECT_FALSE(epd->hasCodepoint(0x1F604));
  EXPECT_TRUE(epd->hasCodepoint(0xFFFD));

  // U+1F600 GRINNING FACE (UTF-8 F0 9F 98 80) is glyph 1 (U+FFFD is glyph 0):
  // advance base 300 + index.
  ASSERT_EQ(font.prewarm("\xF0\x9F\x98\x80"), 0);
  const EpdGlyph* glyph = epd->getGlyph(0x1F600);
  ASSERT_NE(glyph, nullptr);
  EXPECT_EQ(glyph->advanceX, 301);
  EXPECT_EQ(glyph->height, 2);  // index 1 -> height 2
}

// --- SdCardFontRegistry: discovery and filename parsing ---

namespace {

class SdCardFontRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sandbox_ = std::string(CPFONT_SANDBOX_DIR "/") + ::testing::UnitTest::GetInstance()->current_test_info()->name();
    makeDirs(sandbox_);
    halstub::root = sandbox_;
    ESP = EspHostStub{};
  }
  void TearDown() override { halstub::root.clear(); }

  // Create <root>/<family>/<file> inside the sandbox. `root` is the
  // device-absolute font root ("/.fonts" or "/fonts").
  void addFontFile(const std::string& root, const std::string& family, const std::string& file) {
    makeDirs(sandbox_ + root + "/" + family);
    touch(sandbox_ + root + "/" + family + "/" + file);
  }

  std::string sandbox_;
};

}  // namespace

TEST_F(SdCardFontRegistryTest, DiscoverMergesRootsWithHiddenWinningDuplicates) {
  addFontFile("/.fonts", "FamA", "FamA_12.cpfont");
  addFontFile("/.fonts", "FamA", "FamA_14.cpfont");
  addFontFile("/fonts", "FamB", "FamB_12.cpfont");
  // Same family name in the visible root: the hidden root's copy must win.
  addFontFile("/fonts", "FamA", "FamA_16.cpfont");
  // Hidden/system directories inside a root are skipped.
  addFontFile("/.fonts", ".Trashes", "Junk_12.cpfont");
  // A family directory with no valid .cpfont files is not listed.
  addFontFile("/fonts", "Empty", "readme.txt");

  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  ASSERT_EQ(registry.getFamilyCount(), 2);
  EXPECT_EQ(registry.getFamilies()[0].name, "FamA");
  EXPECT_EQ(registry.getFamilies()[1].name, "FamB");
  EXPECT_EQ(registry.getFamilyIndex("FamB"), 1);
  EXPECT_EQ(registry.findFamily("Empty"), nullptr);

  const SdCardFontFamilyInfo* famA = registry.findFamily("FamA");
  ASSERT_NE(famA, nullptr);
  ASSERT_EQ(famA->files.size(), 2u);
  EXPECT_TRUE(famA->hasSize(12));
  EXPECT_TRUE(famA->hasSize(14));
  EXPECT_FALSE(famA->hasSize(16));  // visible-root duplicate was ignored
  EXPECT_EQ(famA->files[0].path, "/.fonts/FamA/FamA_12.cpfont");
}

TEST_F(SdCardFontRegistryTest, MalformedFilenamesAreSkipped) {
  const char* names[] = {
      "Ok_8.cpfont",         // the single valid one
      "NoScore.cpfont",      // no underscore before the size
      "Tmp_14.cpfont.tmp",   // in-progress download
      "Back_14.cpfont~",     // editor backup
      "._Hidden_12.cpfont",  // macOS resource fork
      "_12.cpfont",          // empty family part
      "Zero_0.cpfont",       // size below 1
      "Big_300.cpfont",      // size above uint8
      "Alpha_ab.cpfont",     // non-numeric size
  };
  for (const char* name : names) addFontFile("/fonts", "Edge", name);

  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  const SdCardFontFamilyInfo* edge = registry.findFamily("Edge");
  ASSERT_NE(edge, nullptr);
  ASSERT_EQ(edge->files.size(), 1u);
  EXPECT_EQ(edge->files[0].pointSize, 8);
}

TEST_F(SdCardFontRegistryTest, DuplicateSizeWithinFamilyIsSkipped) {
  // Both parse to (size 12, style 0); the second (sorted order) must be
  // dropped so findFile() cannot be shadowed.
  addFontFile("/fonts", "Dup", "Aaa_12.cpfont");
  addFontFile("/fonts", "Dup", "Bbb_12.cpfont");

  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  const SdCardFontFamilyInfo* dup = registry.findFamily("Dup");
  ASSERT_NE(dup, nullptr);
  ASSERT_EQ(dup->files.size(), 1u);
  EXPECT_EQ(dup->files[0].path, "/fonts/Dup/Aaa_12.cpfont");
}

TEST_F(SdCardFontRegistryTest, FindNearestSizeTiesResolveToSmaller) {
  // The 14pt file sorts BEFORE the 12pt file, so the tie at request 13 is
  // only resolved to 12 by the explicit smaller-size tie-break (first-seen
  // would keep 14).
  addFontFile("/fonts", "Sizes", "Aaa_14.cpfont");
  addFontFile("/fonts", "Sizes", "Bbb_12.cpfont");

  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  const SdCardFontFamilyInfo* fam = registry.findFamily("Sizes");
  ASSERT_NE(fam, nullptr);

  EXPECT_EQ(fam->findNearestSize(13)->pointSize, 12);  // tie: smaller wins
  EXPECT_EQ(fam->findNearestSize(20)->pointSize, 14);
  EXPECT_EQ(fam->findNearestSize(8)->pointSize, 12);
  EXPECT_EQ(fam->findNearestSize(14)->pointSize, 14);         // exact match
  EXPECT_EQ(fam->findNearestSize(12, /*style=*/1), nullptr);  // no style-1 files

  EXPECT_EQ(fam->findFile(12)->path, "/fonts/Sizes/Bbb_12.cpfont");
  EXPECT_EQ(fam->findFile(13), nullptr);

  const std::vector<uint8_t> sizes = fam->availableSizes();
  ASSERT_EQ(sizes.size(), 2u);
  EXPECT_EQ(sizes[0], 12);
  EXPECT_EQ(sizes[1], 14);
}

TEST_F(SdCardFontRegistryTest, FamilyRootLookupAndDefaultWriteRoot) {
  addFontFile("/.fonts", "HiddenFam", "HiddenFam_12.cpfont");
  addFontFile("/fonts", "VisibleFam", "VisibleFam_12.cpfont");

  EXPECT_STREQ(SdCardFontRegistry::findFamilyRoot("HiddenFam"), "/.fonts");
  EXPECT_STREQ(SdCardFontRegistry::findFamilyRoot("VisibleFam"), "/fonts");
  EXPECT_EQ(SdCardFontRegistry::findFamilyRoot("Missing"), nullptr);
  EXPECT_EQ(SdCardFontRegistry::findFamilyRoot(nullptr), nullptr);
  EXPECT_EQ(SdCardFontRegistry::findFamilyRoot(""), nullptr);

  // Both roots exist here: hidden preferred for new installs.
  EXPECT_STREQ(SdCardFontRegistry::defaultWriteRoot(), "/.fonts");
}

TEST_F(SdCardFontRegistryTest, DefaultWriteRootFollowsTheOnlyExistingRoot) {
  addFontFile("/fonts", "OnlyVisible", "OnlyVisible_12.cpfont");
  EXPECT_STREQ(SdCardFontRegistry::defaultWriteRoot(), "/fonts");
}

TEST_F(SdCardFontRegistryTest, DiscoverReturnsFalseWhenNoRootsExist) {
  SdCardFontRegistry registry;
  EXPECT_FALSE(registry.discover());
  EXPECT_EQ(registry.getFamilyCount(), 0);
  // Neither root exists: new installs default to the hidden root.
  EXPECT_STREQ(SdCardFontRegistry::defaultWriteRoot(), "/.fonts");
}
