// Host tests for the SD font install/selection layer:
//   - FontInstaller (src/FontInstaller.cpp): name and filename validation,
//     fonts-root selection, .cpfont magic validation, family deletion.
//   - SdCardFontSystem (src/SdCardFontSystem.cpp): begin/ensureLoaded/reload
//     matrix, point-size snapping and persistence throttling, the CJK probe
//     that gates the size-matched UI fallbacks.
//
// Both production units are compiled from copies placed next to the stub
// CrossPointSettings.h (see CMakeLists.txt), so SETTINGS here is the stub
// singleton and its writes are counted rather than performed.

#include <CrossPointSettings.h>
#include <FontInstaller.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFontRegistry.h>
#include <SdCardFontSystem.h>
#include <fontIds.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>

#include "FontSystemFixtures.h"

namespace {

class FontInstallerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fontfx::resetSandbox();
    ESP = EspHostStub{};
    SETTINGS.reset();
  }
  void TearDown() override {
    SETTINGS.reset();
    fontfx::teardownSandbox();
    ESP = EspHostStub{};
  }

  bool hostExists(const std::string& devicePath) const { return std::filesystem::exists(fontfx::hostPath(devicePath)); }

  SdCardFontRegistry registry;
};

using SdCardFontSystemTest = FontInstallerTest;

// Name of the .cpfont installed for `family` at `pt`, as the registry sees it.
std::string fontFile(const std::string& family, const int pt) {
  return family + "/" + family + "_" + std::to_string(pt) + ".cpfont";
}

}  // namespace

// --- FontInstaller: family-name validation ---

TEST_F(FontInstallerTest, FamilyNameAcceptsAlphanumericHyphenUnderscore) {
  EXPECT_TRUE(FontInstaller::isValidFamilyName("Noto"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("Noto-Sans"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("Noto_Sans_CJK"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("A1"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("0"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("-"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("_"));
}

TEST_F(FontInstallerTest, FamilyNameRejectsPathTraversalAndSeparators) {
  EXPECT_FALSE(FontInstaller::isValidFamilyName(".."));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("../evil"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a..b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("..\\..\\x"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("/"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a/b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a\\b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("/.crosspoint"));
}

TEST_F(FontInstallerTest, FamilyNameRejectsEmptyNullAndOtherPunctuation) {
  EXPECT_FALSE(FontInstaller::isValidFamilyName(nullptr));
  EXPECT_FALSE(FontInstaller::isValidFamilyName(""));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("."));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a.b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a\tb"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a:b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a*"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("caf\xC3\xA9"));  // non-ASCII bytes
}

TEST_F(FontInstallerTest, FamilyNameHasNoLengthLimitOfItsOwn) {
  // Deliberate pin: the 31-usable-byte cap lives in CrossPointSettings::
  // sdFontFamilyName, not here. See the truncation test below.
  const std::string long63(63, 'A');
  EXPECT_TRUE(FontInstaller::isValidFamilyName(long63.c_str()));
}

// --- FontInstaller: .cpfont filename validation ---

TEST_F(FontInstallerTest, CpfontFilenameAcceptsCanonicalNames) {
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("Alpha_14.cpfont"));
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("Noto-Sans-CJK_12.cpfont"));
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("a.cpfont"));
}

TEST_F(FontInstallerTest, CpfontFilenameRejectsTraversalAndSeparators) {
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("../Alpha_14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("../../.crosspoint/settings.json"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("evil/Alpha_14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("evil\\Alpha_14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("/Alpha_14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha_14..cpfont"));
}

TEST_F(FontInstallerTest, CpfontFilenameRequiresTheExactExtensionAndACleanBasename) {
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(nullptr));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(""));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(".cpfont"));  // extension only, empty basename
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha_14.cpfon"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha_14.CPFONT"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha_14.cpfont.tmp"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha.14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha 14.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("Alpha_14.cpfont~"));
}

// --- FontInstaller: fonts-root selection ---

TEST_F(FontInstallerTest, EnsureFamilyDirCreatesTheHiddenRootWhenNeitherRootExists) {
  FontInstaller installer(registry);

  ASSERT_TRUE(installer.ensureFamilyDir("Alpha"));
  EXPECT_TRUE(hostExists("/.fonts/Alpha"));
  EXPECT_FALSE(hostExists("/fonts"));
}

TEST_F(FontInstallerTest, EnsureFamilyDirFollowsTheOnlyExistingRoot) {
  std::filesystem::create_directories(fontfx::hostPath("/fonts"));
  FontInstaller installer(registry);

  ASSERT_TRUE(installer.ensureFamilyDir("Alpha"));
  EXPECT_TRUE(hostExists("/fonts/Alpha"));
  EXPECT_FALSE(hostExists("/.fonts/Alpha"));
}

TEST_F(FontInstallerTest, EnsureFamilyDirReusesAnInstalledFamilysExistingRoot) {
  // Both roots exist, but Alpha already lives in the visible one: a re-install
  // must land beside the existing files, not create a second copy.
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont", fontfx::VISIBLE_ROOT);
  fontfx::installFont("Beta", 14, "valid_basic.cpfont", fontfx::HIDDEN_ROOT);
  FontInstaller installer(registry);

  ASSERT_TRUE(installer.ensureFamilyDir("Alpha"));
  EXPECT_FALSE(hostExists("/.fonts/Alpha"));

  // A brand-new family with both roots present goes to the hidden root.
  ASSERT_TRUE(installer.ensureFamilyDir("Gamma"));
  EXPECT_TRUE(hostExists("/.fonts/Gamma"));
  EXPECT_FALSE(hostExists("/fonts/Gamma"));
}

TEST_F(FontInstallerTest, BuildFontPathUsesTheSameRootSelectionAsEnsureFamilyDir) {
  char path[160];

  FontInstaller::buildFontPath("Alpha", "Alpha_14.cpfont", path, sizeof(path));
  EXPECT_STREQ(path, "/.fonts/Alpha/Alpha_14.cpfont");

  fontfx::installFont("Alpha", 12, "valid_basic.cpfont", fontfx::VISIBLE_ROOT);
  FontInstaller::buildFontPath("Alpha", "Alpha_14.cpfont", path, sizeof(path));
  EXPECT_STREQ(path, "/fonts/Alpha/Alpha_14.cpfont");
}

TEST_F(FontInstallerTest, BuildFontPathAndEnsureFamilyDirDoNotValidateTheirInputs) {
  // Pinning a real gap, not endorsing it: neither entry point runs the
  // isValid* checks, so a caller that skips them escapes the fonts tree. The
  // web-upload path validates first (src/network/CrossPointWebServer.cpp:1791);
  // the manifest download path does not
  // (src/activities/settings/FontDownloadActivity.cpp:398,416).
  char path[160];
  FontInstaller::buildFontPath("../../.crosspoint", "../evil.cpfont", path, sizeof(path));
  EXPECT_STREQ(path, "/.fonts/../../.crosspoint/../evil.cpfont");

  FontInstaller installer(registry);
  ASSERT_TRUE(installer.ensureFamilyDir("..escape"));
  EXPECT_TRUE(hostExists("/.fonts/..escape"));
}

TEST_F(FontInstallerTest, BuildFontPathTruncatesIntoAnUndersizedBuffer) {
  // Pinning current behaviour: snprintf truncates silently and the caller gets
  // a short, still NUL-terminated path.
  char path[8];
  FontInstaller::buildFontPath("Alpha", "Alpha_14.cpfont", path, sizeof(path));
  EXPECT_STREQ(path, "/.fonts");
}

// --- FontInstaller: .cpfont magic validation ---

TEST_F(FontInstallerTest, ValidateCpfontFileAcceptsOnlyTheRealMagic) {
  const std::string good = fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  const std::string bad = fontfx::installFont("Broken", 14, "bad_magic.cpfont");
  FontInstaller installer(registry);

  EXPECT_TRUE(installer.validateCpfontFile(good.c_str()));
  EXPECT_FALSE(installer.validateCpfontFile(bad.c_str()));
}

TEST_F(FontInstallerTest, ValidateCpfontFileRejectsShortEmptyAndMissingFiles) {
  fontfx::writeFile("/.fonts/Short/Short_14.cpfont", {'C', 'P', 'F'});
  fontfx::writeFile("/.fonts/Empty/Empty_14.cpfont", {});
  // Full-length magic prefix, wrong trailing byte.
  fontfx::writeFile("/.fonts/Near/Near_14.cpfont", {'C', 'P', 'F', 'O', 'N', 'T', 0, 1});
  FontInstaller installer(registry);

  EXPECT_FALSE(installer.validateCpfontFile("/.fonts/Short/Short_14.cpfont"));
  EXPECT_FALSE(installer.validateCpfontFile("/.fonts/Empty/Empty_14.cpfont"));
  EXPECT_FALSE(installer.validateCpfontFile("/.fonts/Near/Near_14.cpfont"));
  EXPECT_FALSE(installer.validateCpfontFile("/.fonts/Nope/Nope_14.cpfont"));
}

// --- FontInstaller: deletion ---

TEST_F(FontInstallerTest, DeleteFamilyRejectsAnInvalidNameWithoutTouchingDisk) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  FontInstaller installer(registry);

  EXPECT_EQ(installer.deleteFamily("../Alpha"), FontInstaller::Error::INVALID_FAMILY_NAME);
  EXPECT_EQ(installer.deleteFamily(""), FontInstaller::Error::INVALID_FAMILY_NAME);
  EXPECT_EQ(installer.deleteFamily(nullptr), FontInstaller::Error::INVALID_FAMILY_NAME);
  EXPECT_TRUE(hostExists("/.fonts/" + fontFile("Alpha", 14)));
}

TEST_F(FontInstallerTest, DeleteFamilyRemovesTheFamilyFromBothRoots) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont", fontfx::HIDDEN_ROOT);
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont", fontfx::VISIBLE_ROOT);
  fontfx::installFont("Beta", 14, "valid_basic.cpfont", fontfx::HIDDEN_ROOT);
  FontInstaller installer(registry);

  EXPECT_EQ(installer.deleteFamily("Alpha"), FontInstaller::Error::OK);
  EXPECT_FALSE(hostExists("/.fonts/Alpha"));
  EXPECT_FALSE(hostExists("/fonts/Alpha"));
  EXPECT_TRUE(hostExists("/.fonts/" + fontFile("Beta", 14)));
}

TEST_F(FontInstallerTest, DeleteFamilyThatIsNotInstalledSucceedsSilently) {
  FontInstaller installer(registry);
  EXPECT_EQ(installer.deleteFamily("Ghost"), FontInstaller::Error::OK);
}

TEST_F(FontInstallerTest, DeletingTheActiveFamilyClearsTheSelection) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily("Alpha"), FontInstaller::Error::OK);
  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
}

TEST_F(FontInstallerTest, DeletingAnInactiveFamilyLeavesTheSelectionAlone) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  fontfx::installFont("Beta", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily("Beta"), FontInstaller::Error::OK);
  EXPECT_EQ(SETTINGS.clearSdFontCalls, 0);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "Alpha");
}

TEST_F(FontInstallerTest, RefreshRegistryAndIsFamilyInstalledTrackTheCard) {
  FontInstaller installer(registry);
  EXPECT_FALSE(installer.isFamilyInstalled("Alpha"));

  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  EXPECT_FALSE(installer.isFamilyInstalled("Alpha"));  // stale until refreshed

  installer.refreshRegistry();
  EXPECT_TRUE(installer.isFamilyInstalled("Alpha"));
  EXPECT_FALSE(installer.isFamilyInstalled("Beta"));

  ASSERT_EQ(installer.deleteFamily("Alpha"), FontInstaller::Error::OK);
  installer.refreshRegistry();
  EXPECT_FALSE(installer.isFamilyInstalled("Alpha"));
}

// --- SdCardFontSystem: begin() ---

TEST_F(SdCardFontSystemTest, BeginWithoutASelectionOnlyDiscovers) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_EQ(system.registry().getFamilyCount(), 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_EQ(SETTINGS.saveCount, 0);
  EXPECT_EQ(SETTINGS.clearSdFontCalls, 0);
}

TEST_F(SdCardFontSystemTest, BeginInstallsTheSettingsResolverTrampoline) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  ASSERT_NE(SETTINGS.sdFontIdResolver, nullptr);
  EXPECT_EQ(SETTINGS.sdFontResolverCtx, &system);
  const int viaTrampoline = SETTINGS.sdFontIdResolver(SETTINGS.sdFontResolverCtx, "Alpha", 14);
  EXPECT_EQ(viaTrampoline, system.resolveFontId("Alpha", 14));
  EXPECT_NE(viaTrampoline, 0);
}

TEST_F(SdCardFontSystemTest, BeginLoadsTheSavedFamilyAndSnapsThePointSize) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 18, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_EQ(SETTINGS.fontPointSize, 12);  // nearest installed size, tie to smaller
  EXPECT_EQ(SETTINGS.saveCount, 1);       // snapped once, persisted once
  EXPECT_EQ(SETTINGS.clearSdFontCalls, 0);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_NE(system.resolveFontId("Alpha", 12), 0);
}

TEST_F(SdCardFontSystemTest, BeginClearsASelectionThatIsNotOnTheCard) {
  fontfx::installFont("Beta", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
  EXPECT_TRUE(renderer.getFontMap().empty());
  // Only the write clearSdFontFamily() does itself: no redundant save on top.
  EXPECT_EQ(SETTINGS.saveCount, 1);
}

TEST_F(SdCardFontSystemTest, BeginClearsASelectionWhoseFontFileIsCorrupt) {
  fontfx::installFont("Alpha", 14, "bad_version.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_EQ(system.resolveFontId("Alpha", 14), 0);
}

TEST_F(SdCardFontSystemTest, AFamilyNameLongerThanTheSettingsFieldCanNeverStayLoaded) {
  // sdFontFamilyName holds 31 usable bytes; a longer directory name is
  // truncated on the way in and then never matches the registry.
  const std::string long40(40, 'A');
  fontfx::installFont(long40, 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily(long40.c_str());
  ASSERT_EQ(std::strlen(SETTINGS.sdFontFamilyName), 31u);
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
}

// --- SdCardFontSystem: ensureLoaded() ---

TEST_F(SdCardFontSystemTest, EnsureLoadedWithNoSelectionUnloadsAndSnapsToBuiltinSizes) {
  fontfx::installFont("Alpha", 13, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 13;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_EQ(SETTINGS.saveCount, 0);  // 13 was already the installed size
  ASSERT_EQ(renderer.getFontMap().size(), 1u);

  SETTINGS.sdFontFamilyName[0] = '\0';  // switched back to a built-in family
  system.ensureLoaded(renderer);

  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_TRUE(renderer.getSdCardFonts().empty());
  EXPECT_EQ(SETTINGS.fontPointSize, 12);  // 13 is equidistant from 12 and 14
  EXPECT_EQ(SETTINGS.saveCount, 1);
}

TEST_F(SdCardFontSystemTest, EnsureLoadedIsANoOpWhenTheWantedSizeIsAlreadyLoaded) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  const int fontId = system.resolveFontId("Alpha", 14);
  const int fallbackClears = renderer.clearFallbackCalls;
  const int removals = renderer.removeCalls;

  system.ensureLoaded(renderer);

  EXPECT_EQ(system.resolveFontId("Alpha", 14), fontId);
  EXPECT_EQ(renderer.clearFallbackCalls, fallbackClears);
  EXPECT_EQ(renderer.removeCalls, removals);
  EXPECT_EQ(SETTINGS.saveCount, 0);
  EXPECT_EQ(renderer.duplicateInserts, 0);
}

TEST_F(SdCardFontSystemTest, EnsureLoadedSnapsThePointSizeBeforeTheNoOpReturn) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 18, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 12;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_EQ(SETTINGS.saveCount, 0);
  const int fallbackClears = renderer.clearFallbackCalls;

  // A size the family does not ship, mapping to the file already loaded.
  SETTINGS.fontPointSize = 13;
  system.ensureLoaded(renderer);

  EXPECT_EQ(SETTINGS.fontPointSize, 12);
  EXPECT_EQ(SETTINGS.saveCount, 1);
  EXPECT_EQ(renderer.clearFallbackCalls, fallbackClears);  // no reload
}

TEST_F(SdCardFontSystemTest, EnsureLoadedReloadsWhenTheSelectedSizeMapsToAnotherFile) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  fontfx::installFont("Alpha", 18, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 12;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  const int small = system.resolveFontId("Alpha", 12);

  SETTINGS.fontPointSize = 18;
  system.ensureLoaded(renderer);

  const int large = system.resolveFontId("Alpha", 18);
  EXPECT_NE(large, 0);
  EXPECT_NE(large, small);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_EQ(renderer.getFontMap().count(small), 0u);
  EXPECT_EQ(SETTINGS.saveCount, 0);  // 18 is an installed size, no snap needed
}

TEST_F(SdCardFontSystemTest, EnsureLoadedReloadsWhenTheRegistryWasMarkedDirty) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  const int fallbackClears = renderer.clearFallbackCalls;

  // Same family, same size: only the dirty flag forces the reload, because the
  // bytes on disk may have been replaced by a web upload.
  system.markRegistryDirty();
  system.ensureLoaded(renderer);

  EXPECT_EQ(renderer.clearFallbackCalls, fallbackClears + 1);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
  EXPECT_NE(system.resolveFontId("Alpha", 14), 0);
  EXPECT_EQ(renderer.duplicateInserts, 0);

  // The flag is consumed: a second call is a no-op again.
  const int afterReload = renderer.clearFallbackCalls;
  system.ensureLoaded(renderer);
  EXPECT_EQ(renderer.clearFallbackCalls, afterReload);
}

TEST_F(SdCardFontSystemTest, EnsureLoadedSwitchesToANewlySelectedFamily) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  fontfx::installFont("Beta", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  const int alphaId = system.resolveFontId("Alpha", 14);

  SETTINGS.setSdFontFamily("Beta");
  system.ensureLoaded(renderer);

  EXPECT_EQ(system.resolveFontId("Alpha", 14), 0);
  EXPECT_NE(system.resolveFontId("Beta", 14), 0);
  EXPECT_NE(system.resolveFontId("Beta", 14), alphaId);
  EXPECT_EQ(renderer.getFontMap().size(), 1u);
}

TEST_F(SdCardFontSystemTest, EnsureLoadedClearsTheSelectionWhenTheFamilyDisappears) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_EQ(renderer.getFontMap().size(), 1u);

  std::filesystem::remove_all(fontfx::hostPath("/.fonts/Alpha"));
  system.markRegistryDirty();
  system.ensureLoaded(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
  EXPECT_TRUE(renderer.getSdCardFonts().empty());
  EXPECT_EQ(system.resolveFontId("Alpha", 14), 0);
}

TEST_F(SdCardFontSystemTest, EnsureLoadedClearsTheSelectionWhenANewFamilyFailsToLoad) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  fontfx::installFont("Broken", 14, "truncated_header.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);

  SETTINGS.setSdFontFamily("Broken");
  system.ensureLoaded(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
}

TEST_F(SdCardFontSystemTest, EnsureLoadedClearsTheSelectionWhenTheFamilyIsUnknown) {
  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);

  SETTINGS.setSdFontFamily("Ghost");
  system.ensureLoaded(renderer);

  EXPECT_EQ(SETTINGS.clearSdFontCalls, 1);
  EXPECT_TRUE(renderer.getFontMap().empty());
}

TEST_F(SdCardFontSystemTest, ResolveFontIdIgnoresTheRequestedSizeAndTheFamilyMustMatch) {
  fontfx::installFont("Alpha", 12, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Alpha");
  SETTINGS.fontPointSize = 12;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);

  const int fontId = system.resolveFontId("Alpha", 12);
  EXPECT_NE(fontId, 0);
  EXPECT_EQ(system.resolveFontId("Alpha", 99), fontId);
  EXPECT_EQ(system.resolveFontId("Alpha", 0), fontId);
  EXPECT_EQ(system.resolveFontId("Beta", 12), 0);
  EXPECT_EQ(system.resolveFontId("", 12), 0);
}

TEST_F(SdCardFontSystemTest, RefreshIfDirtyRediscoversExactlyOncePerFlag) {
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_EQ(system.registry().getFamilyCount(), 0);

  fontfx::installFont("Alpha", 14, "valid_basic.cpfont");
  system.refreshIfDirty();
  EXPECT_EQ(system.registry().getFamilyCount(), 0);  // flag not set: no rescan

  system.markRegistryDirty();
  system.refreshIfDirty();
  EXPECT_EQ(system.registry().getFamilyCount(), 1);

  fontfx::installFont("Beta", 14, "valid_basic.cpfont");
  system.refreshIfDirty();
  EXPECT_EQ(system.registry().getFamilyCount(), 1);  // flag already consumed
}

// --- SdCardFontSystem: CJK UI fallbacks ---

TEST_F(SdCardFontSystemTest, CjkFamilyRegistersSizeMatchedUiFallbacks) {
  for (const uint8_t pt : {8, 10, 12, 14}) fontfx::installFont("Cjk", pt, "cjk.cpfont");
  SETTINGS.setSdFontFamily("Cjk");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  ASSERT_EQ(renderer.fallbacks().size(), 3u);
  EXPECT_EQ(renderer.fallbacks().count(SMALL_FONT_ID), 1u);
  EXPECT_EQ(renderer.fallbacks().count(UI_10_FONT_ID), 1u);
  EXPECT_EQ(renderer.fallbacks().count(UI_12_FONT_ID), 1u);
  // Reader font plus the three UI sizes.
  EXPECT_EQ(renderer.getFontMap().size(), 4u);
  for (const auto& [uiId, sdId] : renderer.fallbacks()) {
    EXPECT_NE(sdId, 0) << "fallback for " << uiId;
    EXPECT_TRUE(renderer.isSdCardFont(sdId));
  }
}

TEST_F(SdCardFontSystemTest, LatinOnlyFamilySkipsTheUiFallbackSizesEntirely) {
  for (const uint8_t pt : {8, 10, 12, 14}) fontfx::installFont("Latin", pt, "valid_basic.cpfont");
  SETTINGS.setSdFontFamily("Latin");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  EXPECT_TRUE(renderer.fallbacks().empty());
  EXPECT_EQ(renderer.getFontMap().size(), 1u);  // no UI sizes loaded into RAM
}

TEST_F(SdCardFontSystemTest, UiFallbackIsRegisteredOnlyForSizesTheFamilyShips) {
  fontfx::installFont("Cjk", 10, "cjk.cpfont");
  fontfx::installFont("Cjk", 14, "cjk.cpfont");
  SETTINGS.setSdFontFamily("Cjk");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;

  system.begin(renderer);

  ASSERT_EQ(renderer.fallbacks().size(), 1u);
  EXPECT_EQ(renderer.fallbacks().count(UI_10_FONT_ID), 1u);
  EXPECT_EQ(renderer.getFontMap().size(), 2u);
}

TEST_F(SdCardFontSystemTest, ReloadingTheFamilyRebuildsTheUiFallbacksFromScratch) {
  for (const uint8_t pt : {10, 14}) fontfx::installFont("Cjk", pt, "cjk.cpfont");
  SETTINGS.setSdFontFamily("Cjk");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_EQ(renderer.fallbacks().size(), 1u);
  const int before = renderer.fallbacks().at(UI_10_FONT_ID);
  const int fallbackClears = renderer.clearFallbackCalls;
  const int removals = renderer.removeCalls;

  system.markRegistryDirty();
  system.ensureLoaded(renderer);

  // The reload really happened: both loaded sizes were torn down and the
  // fallback map cleared before being rebuilt.
  EXPECT_EQ(renderer.clearFallbackCalls, fallbackClears + 1);
  EXPECT_EQ(renderer.removeCalls, removals + 2);
  ASSERT_EQ(renderer.fallbacks().size(), 1u);
  // Same bytes on disk, so the content-hash ids come back identical.
  EXPECT_EQ(renderer.fallbacks().at(UI_10_FONT_ID), before);
  EXPECT_EQ(renderer.getFontMap().size(), 2u);
  EXPECT_EQ(renderer.duplicateInserts, 0);
}

TEST_F(SdCardFontSystemTest, UnloadingTheSdFamilyDropsTheUiFallbacks) {
  for (const uint8_t pt : {10, 14}) fontfx::installFont("Cjk", pt, "cjk.cpfont");
  SETTINGS.setSdFontFamily("Cjk");
  SETTINGS.fontPointSize = 14;
  GfxRenderer renderer;
  SdCardFontSystem system;
  system.begin(renderer);
  ASSERT_FALSE(renderer.fallbacks().empty());

  SETTINGS.sdFontFamilyName[0] = '\0';
  system.ensureLoaded(renderer);

  EXPECT_TRUE(renderer.fallbacks().empty());
  EXPECT_TRUE(renderer.getFontMap().empty());
}
