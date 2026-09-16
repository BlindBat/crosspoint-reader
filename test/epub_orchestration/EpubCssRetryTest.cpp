// The CSS cache retry state machine in Epub::load()/parseCssFiles(): when the
// stylesheets are reparsed, when the cache is replaced or preserved, and when
// the rendered section caches must be dropped because the rule set changed.

#include "EpubFixture.h"

namespace {

using epubtest::ManifestItem;
using epubtest::OpfSpec;
using epubtest::ZipBuilder;

constexpr uint8_t kFlagPartial = 1 << 0;

std::string cssCachePath(const Epub& epub) { return epub.getCachePath() + "/css_rules.cache"; }

// Cache header layout: version, flags, ruleCount (little-endian uint16).
uint8_t cacheVersion(const Epub& epub) {
  const std::string bytes = epubtest::readBytes(cssCachePath(epub));
  return bytes.size() > 0 ? static_cast<uint8_t>(bytes[0]) : 0xFF;
}
uint8_t cacheFlags(const Epub& epub) {
  const std::string bytes = epubtest::readBytes(cssCachePath(epub));
  return bytes.size() > 1 ? static_cast<uint8_t>(bytes[1]) : 0xFF;
}
uint16_t cacheRuleCount(const Epub& epub) {
  const std::string bytes = epubtest::readBytes(cssCachePath(epub));
  if (bytes.size() < 4) return 0;
  return static_cast<uint16_t>(static_cast<uint8_t>(bytes[2]) | (static_cast<uint8_t>(bytes[3]) << 8));
}

void markCacheAsPartial(const Epub& epub) {
  std::string bytes = epubtest::readBytes(cssCachePath(epub));
  ASSERT_GE(bytes.size(), 2u);
  bytes[1] = static_cast<char>(kFlagPartial);
  epubtest::writeBytes(cssCachePath(epub), bytes);
}

std::string sectionsDir(const Epub& epub) { return epub.getCachePath() + "/sections"; }

// Pre-creates the cache directory plus a rendered-section file, so a test can
// see whether load() invalidated it.
void seedSections(const Epub& epub) {
  Storage.mkdir(epub.getCachePath().c_str());
  Storage.mkdir(sectionsDir(epub).c_str());
  epubtest::writeBytes(sectionsDir(epub) + "/0.bin", "rendered");
}
bool sectionsSurvived(const Epub& epub) { return epubtest::pathExists(sectionsDir(epub) + "/0.bin"); }

// Book whose stylesheet set is supplied by the caller. Every entry is declared
// in the manifest unless extraZipCss names a file the manifest omits.
std::string cssBook(const std::vector<std::pair<std::string, std::string>>& stylesheets,
                    const std::string& undeclaredCssPath = "", const std::string& undeclaredCss = "") {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  int id = 0;
  for (const auto& sheet : stylesheets) {
    opf.manifest.push_back({"css" + std::to_string(id++), sheet.first, "text/css", ""});
  }

  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  for (const auto& sheet : stylesheets) zip.add("OEBPS/" + sheet.first, sheet.second);
  if (!undeclaredCssPath.empty()) zip.add(undeclaredCssPath, undeclaredCss);
  return zip.build();
}

TEST_F(EpubFixture, ColdLoadParsesStylesheetsIntoACompleteCache) {
  auto epub = make(writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}})));
  ASSERT_TRUE(epub->load());

  ASSERT_TRUE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_EQ(cacheVersion(*epub), CssParser::CSS_CACHE_VERSION);
  EXPECT_EQ(cacheFlags(*epub), 0);
  EXPECT_EQ(cacheRuleCount(*epub), 1);
}

TEST_F(EpubFixture, ColdLoadDropsStaleSectionCachesOnceCssIsParsed) {
  auto epub = make(writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}})));
  seedSections(*epub);
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(sectionsSurvived(*epub));
}

TEST_F(EpubFixture, ABookWithoutStylesheetsStillGetsACompleteCache) {
  auto epub = make(writeEpub("nocss.epub", cssBook({})));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_EQ(cacheFlags(*epub), 0);
  EXPECT_EQ(cacheRuleCount(*epub), 0);
}

TEST_F(EpubFixture, StylesheetsMissingFromTheManifestAreDiscoveredByZipEnumeration) {
  auto epub = make(writeEpub("hidden.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}, "OEBPS/extra.css",
                                                    "blockquote { margin-left: 2em; }\n")));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(cacheRuleCount(*epub), 2);
}

TEST_F(EpubFixture, StylesheetsAreDiscoveredAnywhereWhenTheOpfSitsAtTheArchiveRoot) {
  // A root-level OPF leaves contentBasePath empty, so the ZIP sweep has no
  // prefix to filter on and every .css entry in the archive is a candidate.
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("content.opf"))
      .add("content.opf", epubtest::opfXml(opf))
      .add("chap1.xhtml", epubtest::chapterXhtml("body"))
      .add("styles/deep.css", "p { text-indent: 1em; }\n");

  auto epub = make(writeEpub("rootopf.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getBasePath(), "");
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);
  EXPECT_EQ(cacheRuleCount(*epub), 1);
}

TEST_F(EpubFixture, StylesheetsOutsideTheContentFolderAreNotDiscovered) {
  auto epub = make(writeEpub("outside.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}, "extras/theme.css",
                                                     "blockquote { margin-left: 2em; }\n")));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(cacheRuleCount(*epub), 1);
}

TEST_F(EpubFixture, ByteIdenticalStylesheetsAreExtractedOnlyOnce) {
  const std::string rule = "p { text-indent: 1em; }\n";
  auto twin = make(writeEpub("twin.epub", cssBook({{"a.css", rule}, {"b.css", rule}})));
  ASSERT_TRUE(twin->load());
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);

  Storage.resetForTest();
  auto distinct =
      make(writeEpub("distinct.epub", cssBook({{"a.css", rule}, {"b.css", "blockquote { margin-left: 2em; }\n"}})));
  ASSERT_TRUE(distinct->load());
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 2u);
  EXPECT_EQ(cacheRuleCount(*distinct), 2);
}

TEST_F(EpubFixture, WarmLoadWithACompleteCacheNeitherReparsesNorDropsSections) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());
  const std::string cacheBytes = epubtest::readBytes(cssCachePath(*first));

  auto second = make(path);
  seedSections(*second);
  Storage.resetForTest();
  ASSERT_TRUE(second->load());

  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 0u);
  EXPECT_EQ(epubtest::readBytes(cssCachePath(*second)), cacheBytes);
  EXPECT_TRUE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, WarmLoadWithAMissingCssCacheReparsesAndDropsSections) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());

  auto second = make(path);
  ASSERT_TRUE(Storage.remove(cssCachePath(*second).c_str()));
  seedSections(*second);
  Storage.resetForTest();
  ASSERT_TRUE(second->load());

  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);
  EXPECT_EQ(cacheFlags(*second), 0);
  EXPECT_FALSE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, WarmLoadWithACorruptCssCacheDeletesItAndRebuilds) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());

  auto second = make(path);
  std::string bytes = epubtest::readBytes(cssCachePath(*second));
  ASSERT_FALSE(bytes.empty());
  bytes[0] = static_cast<char>(0xFE);  // unknown cache version
  epubtest::writeBytes(cssCachePath(*second), bytes);
  seedSections(*second);
  Storage.resetForTest();

  ASSERT_TRUE(second->load());
  EXPECT_EQ(cacheVersion(*second), CssParser::CSS_CACHE_VERSION);
  EXPECT_EQ(cacheFlags(*second), 0);
  EXPECT_FALSE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, WarmLoadUpgradesAPartialCacheToCompleteAndDropsSections) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());

  auto second = make(path);
  markCacheAsPartial(*second);
  ASSERT_EQ(cacheFlags(*second), kFlagPartial);
  seedSections(*second);
  Storage.resetForTest();

  ASSERT_TRUE(second->load());
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);
  EXPECT_EQ(cacheFlags(*second), 0);
  EXPECT_FALSE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, AnOversizedStylesheetIsSkippedAndTheCacheIsMarkedPartial) {
  const std::string oversized = std::string(129 * 1024, ' ') + "div { margin: 0; }\n";
  auto epub =
      make(writeEpub("partial.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}, {"big.css", oversized}})));
  ASSERT_TRUE(epub->load());

  ASSERT_TRUE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_EQ(cacheFlags(*epub), kFlagPartial);
  EXPECT_EQ(cacheRuleCount(*epub), 1);  // only the small stylesheet made it in
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);
}

TEST_F(EpubFixture, ARetryThatStaysPartialPreservesThePreviousPartialCacheAndItsSections) {
  const std::string oversized = std::string(129 * 1024, ' ') + "div { margin: 0; }\n";
  const std::string path =
      writeEpub("partial.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}, {"big.css", oversized}}));

  auto first = make(path);
  ASSERT_TRUE(first->load());
  ASSERT_EQ(cacheFlags(*first), kFlagPartial);
  const std::string cacheBytes = epubtest::readBytes(cssCachePath(*first));

  auto second = make(path);
  seedSections(*second);
  Storage.resetForTest();
  ASSERT_TRUE(second->load());

  // The retry ran, stayed partial, and left both the cache and the sections alone.
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 1u);
  EXPECT_EQ(epubtest::readBytes(cssCachePath(*second)), cacheBytes);
  EXPECT_TRUE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, APartialParseWithNoUsableRulesLeavesNoCacheBehind) {
  // The only stylesheet is over the size cap, so nothing is loaded: the parse
  // degrades to Error rather than writing an empty "partial" cache.
  const std::string oversized = std::string(129 * 1024, ' ') + "div { margin: 0; }\n";
  auto epub = make(writeEpub("empty_partial.epub", cssBook({{"big.css", oversized}})));
  seedSections(*epub);
  ASSERT_TRUE(epub->load());

  EXPECT_FALSE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_TRUE(sectionsSurvived(*epub));
}

TEST_F(EpubFixture, LowHeapSkipsCssParsingAndKeepsTheSectionCaches) {
  auto epub = make(writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}})));
  seedSections(*epub);
  platform_host::setHeap(16 * 1024, 16 * 1024);

  ASSERT_TRUE(epub->load());
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 0u);
  EXPECT_FALSE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_TRUE(sectionsSurvived(*epub));
}

TEST_F(EpubFixture, LowHeapOnARetryPreservesTheExistingCssCache) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());

  auto second = make(path);
  markCacheAsPartial(*second);  // forces the retry branch
  const std::string cacheBytes = epubtest::readBytes(cssCachePath(*second));
  seedSections(*second);
  platform_host::setHeap(16 * 1024, 16 * 1024);

  ASSERT_TRUE(second->load());
  EXPECT_EQ(epubtest::readBytes(cssCachePath(*second)), cacheBytes);
  EXPECT_TRUE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, AWarmRetryWhoseArchiveNoLongerParsesKeepsTheExistingCaches) {
  const std::string path = writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}}));
  auto first = make(path);
  ASSERT_TRUE(first->load());

  auto second = make(path);
  markCacheAsPartial(*second);  // forces the retry branch
  const std::string cacheBytes = epubtest::readBytes(cssCachePath(*second));
  seedSections(*second);
  // book.bin still describes the book, but the archive behind it is gone.
  epubtest::writeBytes(path, std::string(2048, '\x7F'));
  Storage.resetForTest();

  ASSERT_TRUE(second->load());
  EXPECT_EQ(second->getSpineItemsCount(), 1);
  EXPECT_EQ(Storage.countWriteOpensEndingWith("/.tmp.css"), 0u);
  EXPECT_EQ(epubtest::readBytes(cssCachePath(*second)), cacheBytes);
  EXPECT_TRUE(sectionsSurvived(*second));
}

TEST_F(EpubFixture, AFailedTempCssWriteLeavesTheCacheAndSectionsUntouched) {
  auto epub = make(writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}})));
  seedSections(*epub);
  Storage.failOpenForWrite.push_back("/.tmp.css");

  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_TRUE(sectionsSurvived(*epub));
}

TEST_F(EpubFixture, SkipLoadingCssBypassesTheStateMachineEntirely) {
  auto epub = make(writeEpub("css.epub", cssBook({{"style.css", "p { text-indent: 1em; }\n"}})));
  seedSections(*epub);

  ASSERT_TRUE(epub->load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true));
  EXPECT_FALSE(epubtest::pathExists(cssCachePath(*epub)));
  EXPECT_TRUE(sectionsSurvived(*epub));

  // And on a warm load it still leaves an existing cache alone.
  const std::string path = epub->getPath();
  auto warm = make(path);
  ASSERT_TRUE(warm->load(true, true));
  EXPECT_FALSE(epubtest::pathExists(cssCachePath(*warm)));
  EXPECT_TRUE(sectionsSurvived(*warm));
}

}  // namespace
