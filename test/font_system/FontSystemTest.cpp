// Host tests for the style-fallback core and the SD font plumbing above it:
//   - EpdFontFamily::getFont() style resolution (lib/EpdFont/EpdFontFamily.cpp)
//   - ReaderFontSizes (src/ReaderFontSizes.cpp)
//   - SdCardFontManager load/unload/extra-size bookkeeping
//     (lib/EpdFont/SdCardFontManager.cpp)
//
// Registry discovery itself, .cpfont parsing and the prewarm/overflow paths
// belong to test/sdcard_font; this suite only drives them as collaborators.

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <ReaderFontSizes.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "FontSystemFixtures.h"

namespace {

// --- EpdFontFamily style-resolution fixtures ---

// A minimal in-RAM font whose identity is readable from any accessor:
// advanceY tags the font, glyph advances tag it again, and each style owns a
// private codepoint so hasCodepoint()/getGlyph() reveal which one answered.
class FakeFont {
 public:
  FakeFont(const uint16_t advanceBase, const uint32_t privateCp, const uint8_t advanceY) {
    for (int i = 0; i < 4; i++) {
      glyphs_[i] = EpdGlyph{4, 2, static_cast<uint16_t>(advanceBase + i), 0, 8, 2, 0};
    }
    intervals_[0] = EpdUnicodeInterval{0x41, 0x44, 0};  // 'A'..'D', shared by every style
    intervals_[1] = EpdUnicodeInterval{privateCp, privateCp, 3};
    data_.bitmap = bitmap_;
    data_.glyph = glyphs_;
    data_.intervals = intervals_;
    data_.intervalCount = 2;
    data_.advanceY = advanceY;
    data_.ascender = advanceY;
    data_.descender = -2;
  }
  FakeFont(const FakeFont&) = delete;
  FakeFont& operator=(const FakeFont&) = delete;

  void addKerning(const int8_t value) {
    kernLeft_[0] = EpdKernClassEntry{0x41, 1};
    kernRight_[0] = EpdKernClassEntry{0x42, 1};
    kernMatrix_[0] = value;
    data_.kernLeftClasses = kernLeft_;
    data_.kernRightClasses = kernRight_;
    data_.kernLeftEntryCount = 1;
    data_.kernRightEntryCount = 1;
    data_.kernLeftClassCount = 1;
    data_.kernRightClassCount = 1;
    data_.kernMatrix = kernMatrix_;
  }

  void addLigature(const uint32_t left, const uint32_t right, const uint32_t out) {
    ligatures_[0] = EpdLigaturePair{(left << 16) | right, out};
    data_.ligaturePairs = ligatures_;
    data_.ligaturePairCount = 1;
  }

  const EpdFont* font() const { return &font_; }
  const EpdFontData* data() const { return &data_; }

 private:
  uint8_t bitmap_[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  EpdGlyph glyphs_[4]{};
  EpdUnicodeInterval intervals_[2]{};
  EpdKernClassEntry kernLeft_[1]{};
  EpdKernClassEntry kernRight_[1]{};
  int8_t kernMatrix_[1]{};
  EpdLigaturePair ligatures_[1]{};
  EpdFontData data_{};
  EpdFont font_{&data_};
};

// Style tag = advanceY. Private codepoints: regular U+0050, bold U+0051,
// italic U+0052, bold-italic U+0053.
constexpr uint8_t REGULAR_TAG = 11;
constexpr uint8_t BOLD_TAG = 21;
constexpr uint8_t ITALIC_TAG = 31;
constexpr uint8_t BOLD_ITALIC_TAG = 41;

class FamilySet {
 public:
  FamilySet() {
    bold_.addLigature('f', 'i', 0xFB01);
    italic_.addKerning(-16);
  }
  const EpdFont* regular() const { return regular_.font(); }
  const EpdFont* bold() const { return bold_.font(); }
  const EpdFont* italic() const { return italic_.font(); }
  const EpdFont* boldItalic() const { return boldItalic_.font(); }

 private:
  FakeFont regular_{100, 0x50, REGULAR_TAG};
  FakeFont bold_{200, 0x51, BOLD_TAG};
  FakeFont italic_{300, 0x52, ITALIC_TAG};
  FakeFont boldItalic_{400, 0x53, BOLD_ITALIC_TAG};
};

uint8_t tagOf(const EpdFontFamily& family, const EpdFontFamily::Style style) {
  return family.getData(style)->advanceY;
}

// --- SD-backed fixtures ---

class SdFontTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fontfx::resetSandbox();
    ESP = EspHostStub{};
  }
  void TearDown() override {
    fontfx::teardownSandbox();
    ESP = EspHostStub{};
  }
};

using ReaderFontSizesTest = SdFontTest;
using SdCardFontManagerTest = SdFontTest;

}  // namespace

// --- EpdFontFamily: style resolution ---

TEST(EpdFontFamilyStyle, RegularOnlyFamilyResolvesEveryStyleToRegular) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular());

  EXPECT_EQ(tagOf(family, EpdFontFamily::REGULAR), REGULAR_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD), REGULAR_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::ITALIC), REGULAR_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD_ITALIC), REGULAR_TAG);
}

TEST(EpdFontFamilyStyle, FullFamilyResolvesEachStyleToItsOwnFont) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), fonts.bold(), fonts.italic(), fonts.boldItalic());

  EXPECT_EQ(tagOf(family, EpdFontFamily::REGULAR), REGULAR_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD), BOLD_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::ITALIC), ITALIC_TAG);
  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD_ITALIC), BOLD_ITALIC_TAG);
}

TEST(EpdFontFamilyStyle, BoldItalicFallsBackToBoldBeforeItalic) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), fonts.bold(), fonts.italic(), nullptr);

  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD_ITALIC), BOLD_TAG);
}

TEST(EpdFontFamilyStyle, BoldItalicFallsBackToItalicWhenBoldIsMissing) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), nullptr, fonts.italic(), nullptr);

  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD_ITALIC), ITALIC_TAG);
}

TEST(EpdFontFamilyStyle, BoldItalicFallsBackToRegularWhenNoSlantedFaceExists) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), nullptr, nullptr, nullptr);

  EXPECT_EQ(tagOf(family, EpdFontFamily::BOLD_ITALIC), REGULAR_TAG);
}

TEST(EpdFontFamilyStyle, SingleStyleRequestsFallBackToRegularWhenAbsent) {
  const FamilySet fonts;
  const EpdFontFamily noBold(fonts.regular(), nullptr, fonts.italic(), fonts.boldItalic());
  const EpdFontFamily noItalic(fonts.regular(), fonts.bold(), nullptr, fonts.boldItalic());

  EXPECT_EQ(tagOf(noBold, EpdFontFamily::BOLD), REGULAR_TAG);
  EXPECT_EQ(tagOf(noItalic, EpdFontFamily::ITALIC), REGULAR_TAG);
  // The bold-italic face is never used to satisfy a plain BOLD/ITALIC request.
  EXPECT_NE(tagOf(noBold, EpdFontFamily::BOLD), BOLD_ITALIC_TAG);
  EXPECT_NE(tagOf(noItalic, EpdFontFamily::ITALIC), BOLD_ITALIC_TAG);
}

TEST(EpdFontFamilyStyle, DecorationOverlayBitsDoNotAffectFontSelection) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), fonts.bold(), fonts.italic(), fonts.boldItalic());

  const auto decorated = [](const int bits) { return static_cast<EpdFontFamily::Style>(bits); };
  EXPECT_EQ(tagOf(family, decorated(EpdFontFamily::BOLD | EpdFontFamily::UNDERLINE)), BOLD_TAG);
  EXPECT_EQ(tagOf(family, decorated(EpdFontFamily::ITALIC | EpdFontFamily::STRIKETHROUGH)), ITALIC_TAG);
  EXPECT_EQ(tagOf(family, decorated(EpdFontFamily::BOLD_ITALIC | EpdFontFamily::SUP)), BOLD_ITALIC_TAG);
  EXPECT_EQ(tagOf(family, decorated(EpdFontFamily::REGULAR | EpdFontFamily::SUB)), REGULAR_TAG);
  EXPECT_EQ(tagOf(family, decorated(EpdFontFamily::BOLD | EpdFontFamily::RUBY_CONTINUE | EpdFontFamily::UNDERLINE |
                                    EpdFontFamily::STRIKETHROUGH | EpdFontFamily::SUP | EpdFontFamily::SUB)),
            BOLD_TAG);
}

TEST(EpdFontFamilyStyle, TextDecorationMaskCoversOnlyUnderlineAndStrikethrough) {
  EXPECT_TRUE(EpdFontFamily::hasTextDecoration(EpdFontFamily::UNDERLINE));
  EXPECT_TRUE(EpdFontFamily::hasTextDecoration(EpdFontFamily::STRIKETHROUGH));
  EXPECT_TRUE(EpdFontFamily::hasTextDecoration(
      static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::UNDERLINE)));
  EXPECT_FALSE(EpdFontFamily::hasTextDecoration(EpdFontFamily::REGULAR));
  EXPECT_FALSE(EpdFontFamily::hasTextDecoration(EpdFontFamily::BOLD_ITALIC));
  EXPECT_FALSE(EpdFontFamily::hasTextDecoration(EpdFontFamily::SUP));
  EXPECT_FALSE(EpdFontFamily::hasTextDecoration(EpdFontFamily::SUB));
  EXPECT_FALSE(EpdFontFamily::hasTextDecoration(EpdFontFamily::RUBY_CONTINUE));
}

TEST(EpdFontFamilyStyle, EveryAccessorRoutesThroughTheSameResolution) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), fonts.bold(), nullptr, nullptr);

  // getGlyph / hasCodepoint: each face owns a private codepoint.
  EXPECT_TRUE(family.hasCodepoint(0x50, EpdFontFamily::REGULAR));
  EXPECT_FALSE(family.hasCodepoint(0x51, EpdFontFamily::REGULAR));
  EXPECT_TRUE(family.hasCodepoint(0x51, EpdFontFamily::BOLD));
  // Italic is absent, so an italic request lands on regular's coverage.
  EXPECT_TRUE(family.hasCodepoint(0x50, EpdFontFamily::ITALIC));
  EXPECT_FALSE(family.hasCodepoint(0x51, EpdFontFamily::ITALIC));

  EXPECT_EQ(family.getGlyph(0x41, EpdFontFamily::REGULAR)->advanceX, 100);
  EXPECT_EQ(family.getGlyph(0x41, EpdFontFamily::BOLD)->advanceX, 200);
  // BOLD|ITALIC with no bold-italic face falls through to bold.
  EXPECT_EQ(family.getGlyph(0x41, EpdFontFamily::BOLD_ITALIC)->advanceX, 200);

  // getTextDimensions must match the resolved face's own measurement.
  int fw = 0, fh = 0, rw = 0, rh = 0;
  family.getTextDimensions("AB", &fw, &fh, EpdFontFamily::BOLD);
  fonts.bold()->getTextDimensions("AB", &rw, &rh);
  EXPECT_EQ(fw, rw);
  EXPECT_EQ(fh, rh);
}

TEST(EpdFontFamilyStyle, KerningAndLigaturesComeFromTheResolvedFace) {
  const FamilySet fonts;
  const EpdFontFamily family(fonts.regular(), fonts.bold(), fonts.italic(), nullptr);

  // Only the italic face carries kern data.
  EXPECT_EQ(family.getKerning(0x41, 0x42, EpdFontFamily::ITALIC), -16);
  EXPECT_EQ(family.getKerning(0x41, 0x42, EpdFontFamily::REGULAR), 0);
  EXPECT_EQ(family.getKerning(0x41, 0x42, EpdFontFamily::BOLD), 0);

  // Only the bold face carries the fi ligature; BOLD_ITALIC resolves to bold.
  const char* text = "i";
  const char* cursor = text;
  EXPECT_EQ(family.applyLigatures('f', cursor, EpdFontFamily::BOLD), 0xFB01u);
  EXPECT_EQ(*cursor, '\0');

  cursor = text;
  EXPECT_EQ(family.applyLigatures('f', cursor, EpdFontFamily::BOLD_ITALIC), 0xFB01u);

  cursor = text;
  EXPECT_EQ(family.applyLigatures('f', cursor, EpdFontFamily::REGULAR), static_cast<uint32_t>('f'));
  EXPECT_EQ(cursor, text);  // no substitution, cursor must not advance
}

// --- ReaderFontSizes ---

TEST_F(ReaderFontSizesTest, BuiltinSizesWhenNoSdFamilyApplies) {
  const std::vector<uint8_t> builtin{12, 14, 16, 18};
  SdCardFontRegistry registry;

  EXPECT_EQ(readerFontPointSizes(nullptr, "Anything"), builtin);
  EXPECT_EQ(readerFontPointSizes(&registry, ""), builtin);
  EXPECT_EQ(readerFontPointSizes(&registry, nullptr), builtin);
  EXPECT_EQ(readerFontPointSizes(&registry, "NotInstalled"), builtin);
}

TEST_F(ReaderFontSizesTest, SdFamilySizesAreReportedAscendingAndDeduplicated) {
  fontfx::installFont("Alpha", 18, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 9, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 13, "valid_basic.cpfont");
  // Same size in the visible root: discovery de-dups families by name, so the
  // hidden root's file wins and 13 appears once.
  fontfx::installFont("Alpha", 13, "valid_basic.cpfont", fontfx::VISIBLE_ROOT);

  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());

  EXPECT_EQ(readerFontPointSizes(&registry, "Alpha"), (std::vector<uint8_t>{9, 13, 18}));
}

TEST_F(ReaderFontSizesTest, FamilyWithNoUsableFilesFallsBackToBuiltinSizes) {
  fontfx::makeEmptyFamilyDir("Empty");
  SdCardFontRegistry registry;
  registry.discover();

  EXPECT_EQ(readerFontPointSizes(&registry, "Empty"), (std::vector<uint8_t>{12, 14, 16, 18}));
}

TEST(SnapToNearestPointSize, TiesResolveToTheSmallerSize) {
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 13), 12);
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 15), 14);
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 17), 16);
}

TEST(SnapToNearestPointSize, ClampsOutsideTheRangeAndKeepsExactMatches) {
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 1), 12);
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 250), 18);
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 4, 16), 16);
}

TEST(SnapToNearestPointSize, EmptyOrNullRangeReturnsTheRequestUnchanged) {
  EXPECT_EQ(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, 0, 13), 13);
  EXPECT_EQ(snapToNearestPointSize(nullptr, 4, 13), 13);
  EXPECT_EQ(snapToNearestPointSize(std::vector<uint8_t>{}, 13), 13);
  EXPECT_EQ(snapToNearestPointSize(std::vector<uint8_t>{9, 20}, 13), 9);
}

// --- SdCardFontManager ---

TEST_F(SdCardFontManagerTest, LoadFamilyRegistersOneFontAndReportsItsId) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));

  EXPECT_EQ(manager.currentFamilyName(), "Alpha");
  EXPECT_EQ(manager.currentPointSize(), 14);
  ASSERT_EQ(renderer.getFontMap().size(), 1u);
  ASSERT_EQ(renderer.getSdCardFonts().size(), 1u);
  const int fontId = manager.getFontId("Alpha");
  EXPECT_NE(fontId, 0);
  EXPECT_TRUE(renderer.isSdCardFont(fontId));
  EXPECT_EQ(renderer.getFontMap().begin()->first, fontId);
  EXPECT_EQ(renderer.duplicateInserts, 0);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, LoadFamilyFallsBackToTheNearestInstalledSize) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 18, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  // 15 is equidistant from 12 and 18; the tie resolves to the smaller file.
  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 15));
  EXPECT_EQ(manager.currentPointSize(), 12);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, LoadFamilyWithNoFilesFails) {
  SdCardFontFamilyInfo empty;
  empty.name = "Nothing";
  GfxRenderer renderer;
  SdCardFontManager manager;

  EXPECT_FALSE(manager.loadFamily(empty, renderer, 14));
  EXPECT_TRUE(manager.currentFamilyName().empty());
  EXPECT_EQ(manager.currentPointSize(), 0);
  EXPECT_TRUE(renderer.getFontMap().empty());
}

TEST_F(SdCardFontManagerTest, CorruptCpfontLeavesNothingRegistered) {
  fontfx::installFont("Broken", 14, "bad_magic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  EXPECT_FALSE(manager.loadFamily(*registry.findFamily("Broken"), renderer, 14));
  EXPECT_TRUE(manager.currentFamilyName().empty());
  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_TRUE(renderer.getSdCardFonts().empty());
  EXPECT_EQ(manager.getFontId("Broken"), 0);
}

TEST_F(SdCardFontManagerTest, FontIdIsStableAcrossReloadsAndVariesWithSizeAndFamily) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  fontfx::installFont("Beta", 12, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 12));
  const int alpha12 = manager.getFontId("Alpha");
  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 12));
  EXPECT_EQ(manager.getFontId("Alpha"), alpha12);

  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));
  const int alpha14 = manager.getFontId("Alpha");
  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Beta"), renderer, 12));
  const int beta12 = manager.getFontId("Beta");

  EXPECT_NE(alpha12, alpha14);
  EXPECT_NE(alpha12, beta12);  // identical bytes, different family name
  EXPECT_NE(alpha12, 0);
  EXPECT_NE(alpha14, 0);
  EXPECT_NE(beta12, 0);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, FontIdCollisionWithAnExistingFontIsRefused) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;

  int fontId = 0;
  {
    SdCardFontManager probe;
    ASSERT_TRUE(probe.loadFamily(*registry.findFamily("Alpha"), renderer, 14));
    fontId = probe.getFontId("Alpha");
    probe.unloadAll(renderer);
  }
  ASSERT_NE(fontId, 0);

  // Squat on the id the hash will produce; the manager must refuse the load
  // rather than shadow the existing registration.
  const FamilySet fonts;
  renderer.insertFont(fontId, EpdFontFamily(fonts.regular()));

  SdCardFontManager manager;
  EXPECT_FALSE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));
  EXPECT_TRUE(manager.currentFamilyName().empty());
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_TRUE(renderer.getSdCardFonts().empty());
}

TEST_F(SdCardFontManagerTest, LoadingASecondFamilyUnloadsTheFirst) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  fontfx::installFont("Beta", 16, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));
  const int alphaId = manager.getFontId("Alpha");
  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Beta"), renderer, 16));

  EXPECT_EQ(manager.currentFamilyName(), "Beta");
  EXPECT_EQ(manager.currentPointSize(), 16);
  EXPECT_EQ(manager.getFontId("Alpha"), 0);
  EXPECT_EQ(renderer.getFontMap().count(alphaId), 0u);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_EQ(renderer.getSdCardFonts().size(), 1u);
  EXPECT_GE(renderer.clearFallbackCalls, 1);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, ExtraSizeLoadsAnAdditionalFileAlongsideTheReaderFont) {
  fontfx::installFont("Alpha", 10, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  const auto* family = registry.findFamily("Alpha");
  ASSERT_TRUE(manager.loadFamily(*family, renderer, 14));
  const int readerId = manager.getFontId("Alpha");

  const int uiId = manager.loadFamilyExtraSize(*family, renderer, 10);
  EXPECT_NE(uiId, 0);
  EXPECT_NE(uiId, readerId);
  EXPECT_EQ(renderer.getFontMap().size(), 2u);
  EXPECT_EQ(renderer.getSdCardFonts().size(), 2u);
  // getFontId keeps reporting the reader font, not the extra size.
  EXPECT_EQ(manager.getFontId("Alpha"), readerId);
  EXPECT_EQ(manager.currentPointSize(), 14);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, ExtraSizeReusesAnAlreadyLoadedSizeInsteadOfLoadingTwice) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  const auto* family = registry.findFamily("Alpha");
  ASSERT_TRUE(manager.loadFamily(*family, renderer, 12));
  const int readerId = manager.getFontId("Alpha");

  EXPECT_EQ(manager.loadFamilyExtraSize(*family, renderer, 12), readerId);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_EQ(renderer.getSdCardFonts().size(), 1u);
  EXPECT_EQ(renderer.duplicateInserts, 0);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, ExtraSizeRequiresAnExactSizeMatch) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  const auto* family = registry.findFamily("Alpha");
  ASSERT_TRUE(manager.loadFamily(*family, renderer, 14));

  // No nearest-size fallback here: a UI fallback at the wrong physical size
  // would not match the surrounding Latin text.
  EXPECT_EQ(manager.loadFamilyExtraSize(*family, renderer, 12), 0);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, UnloadAllClearsFallbacksAndEveryRegistration) {
  fontfx::installFont("Alpha", 10, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  const auto* family = registry.findFamily("Alpha");
  ASSERT_TRUE(manager.loadFamily(*family, renderer, 14));
  const int uiId = manager.loadFamilyExtraSize(*family, renderer, 10);
  renderer.setFallbackFont(1234, uiId);
  ASSERT_FALSE(renderer.fallbacks().empty());

  const int fallbackClearsBefore = renderer.clearFallbackCalls;
  manager.unloadAll(renderer);

  EXPECT_EQ(renderer.clearFallbackCalls, fallbackClearsBefore + 1);
  EXPECT_TRUE(renderer.fallbacks().empty());
  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_TRUE(renderer.getSdCardFonts().empty());
  EXPECT_TRUE(manager.currentFamilyName().empty());
  EXPECT_EQ(manager.currentPointSize(), 0);
  EXPECT_EQ(manager.getFontId("Alpha"), 0);
}

TEST_F(SdCardFontManagerTest, GetFontIdOnlyAnswersForTheLoadedFamily) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;
  SdCardFontManager manager;

  EXPECT_EQ(manager.getFontId("Alpha"), 0);  // nothing loaded yet
  ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));

  EXPECT_NE(manager.getFontId("Alpha"), 0);
  EXPECT_EQ(manager.getFontId("alpha"), 0);  // exact, case-sensitive match
  EXPECT_EQ(manager.getFontId("Beta"), 0);
  EXPECT_EQ(manager.getFontId(""), 0);

  manager.unloadAll(renderer);
}

TEST_F(SdCardFontManagerTest, DestructorReleasesLoadedFontsWithoutUnloadAll) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SdCardFontRegistry registry;
  ASSERT_TRUE(registry.discover());
  GfxRenderer renderer;

  int fontId = 0;
  {
    SdCardFontManager manager;
    ASSERT_TRUE(manager.loadFamily(*registry.findFamily("Alpha"), renderer, 14));
    fontId = manager.getFontId("Alpha");
    ASSERT_NE(fontId, 0);
    ASSERT_EQ(renderer.getSdCardFonts().size(), 1u);
  }

  // The destructor frees the SdCardFont objects (ASan proves that) but does not
  // unregister them, so the renderer is left holding a dangling pointer: every
  // caller must unloadAll() before the manager dies. Pinned, not endorsed.
  EXPECT_EQ(renderer.getSdCardFonts().size(), 1u);
  EXPECT_EQ(renderer.getSdCardFonts().count(fontId), 1u);
  EXPECT_EQ(renderer.getFontMap().count(fontId), 1u);
  EXPECT_EQ(renderer.removeCalls, 0);
  EXPECT_EQ(renderer.clearFallbackCalls, 0);

  renderer.clearSdCardFonts();
}
