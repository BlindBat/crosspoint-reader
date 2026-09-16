// Epub::load() orchestration: cache-path derivation, the cold-build passes,
// warm reuse, spine/TOC access and the item-read helpers (FR-044, FR-045,
// FR-047).

#include <functional>

#include "EpubFixture.h"

namespace {

using epubtest::ManifestItem;
using epubtest::OpfSpec;
using epubtest::ZipBuilder;

// Collects the bytes ZipFile streams out of an entry.
class Collector : public Print {
 public:
  std::string data;
  size_t write(const uint8_t byte) override {
    data.push_back(static_cast<char>(byte));
    return 1;
  }
  size_t write(const uint8_t* buffer, const size_t length) override {
    data.append(reinterpret_cast<const char*>(buffer), length);
    return length;
  }
};

TEST_F(EpubFixture, CachePathIsCacheDirPlusFilepathHash) {
  const std::string path = tmp.at("book.epub");
  const Epub epub(path, cacheRoot);
  EXPECT_EQ(epub.getCachePath(), cacheRoot + "/epub_" + std::to_string(std::hash<std::string>{}(path)));
  EXPECT_EQ(epub.getPath(), path);
}

TEST_F(EpubFixture, CachePathChangesWhenTheFileMoves) {
  const Epub here(tmp.at("a/book.epub"), cacheRoot);
  const Epub there(tmp.at("b/book.epub"), cacheRoot);
  const Epub again(tmp.at("a/book.epub"), cacheRoot);
  EXPECT_NE(here.getCachePath(), there.getCachePath());
  EXPECT_EQ(here.getCachePath(), again.getCachePath());
}

TEST_F(EpubFixture, DerivedCacheArtifactPathsHangOffTheCachePath) {
  const Epub epub(tmp.at("book.epub"), cacheRoot);
  const std::string& base = epub.getCachePath();
  EXPECT_EQ(epub.getCoverBmpPath(), base + "/cover.bmp");
  EXPECT_EQ(epub.getCoverBmpPath(true), base + "/cover_crop.bmp");
  EXPECT_EQ(epub.getThumbBmpPath(), base + "/thumb_[HEIGHT].bmp");
  EXPECT_EQ(epub.getThumbBmpPath(120), base + "/thumb_120.bmp");
}

TEST_F(EpubFixture, SetupCacheDirCreatesTheDirectoryAndIsIdempotent) {
  const Epub epub(tmp.at("book.epub"), cacheRoot);
  ASSERT_FALSE(epubtest::pathExists(epub.getCachePath()));
  epub.setupCacheDir();
  EXPECT_TRUE(epubtest::pathExists(epub.getCachePath()));
  epub.setupCacheDir();
  EXPECT_TRUE(epubtest::pathExists(epub.getCachePath()));
}

TEST_F(EpubFixture, AccessorsAreSafeBeforeLoad) {
  const Epub epub(tmp.at("book.epub"), cacheRoot);
  EXPECT_EQ(epub.getTitle(), "");
  EXPECT_EQ(epub.getAuthor(), "");
  EXPECT_EQ(epub.getLanguage(), "");
  EXPECT_EQ(epub.getSpineItemsCount(), 0);
  EXPECT_EQ(epub.getTocItemsCount(), 0);
  EXPECT_EQ(epub.getBookSize(), 0u);
  EXPECT_EQ(epub.getSpineItem(0).href, "");
  EXPECT_EQ(epub.getTocItem(0).title, "");
  EXPECT_EQ(epub.resolveHrefToSpineIndex("OEBPS/chap1.xhtml"), -1);
  EXPECT_EQ(epub.getSpineIndexForTextReference(), 0);
  EXPECT_FLOAT_EQ(epub.calculateProgress(0, 0.5f), 0.0f);
}

TEST_F(EpubFixture, ColdLoadBuildsTheCacheAndExposesMetadata) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  EXPECT_EQ(epub->getTitle(), "Test Book");
  EXPECT_EQ(epub->getAuthor(), "Test Author");
  EXPECT_EQ(epub->getLanguage(), "en");
  EXPECT_EQ(epub->getBasePath(), "OEBPS/");
  EXPECT_TRUE(epubtest::pathExists(epub->getCachePath() + "/book.bin"));
}

TEST_F(EpubFixture, MultipleCreatorsAreJoinedWithCommas) {
  auto book = standardBook();
  OpfSpec opf;
  opf.authors = {"Ann Author", "Bob Writer"};
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1);

  auto epub = make(writeEpub("two_authors.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getAuthor(), "Ann Author, Bob Writer");
}

TEST_F(EpubFixture, SpineIsBuiltInItemrefOrderWithCumulativeSizes) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  ASSERT_EQ(epub->getSpineItemsCount(), 3);
  EXPECT_EQ(epub->getSpineItem(0).href, "OEBPS/chap1.xhtml");
  EXPECT_EQ(epub->getSpineItem(1).href, "OEBPS/chap2.xhtml");
  EXPECT_EQ(epub->getSpineItem(2).href, "OEBPS/chap3.xhtml");
  EXPECT_EQ(epub->getCumulativeSpineItemSize(0), 100u);
  EXPECT_EQ(epub->getCumulativeSpineItemSize(1), 300u);
  EXPECT_EQ(epub->getCumulativeSpineItemSize(2), 600u);
  EXPECT_EQ(epub->getBookSize(), 600u);
}

TEST_F(EpubFixture, UnknownItemrefsAreSkipped) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""},
                  {"c2", "chap2.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1", "ghost", "c2"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1)
      .add("OEBPS/chap2.xhtml", book.chapter2);

  auto epub = make(writeEpub("ghost.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  ASSERT_EQ(epub->getSpineItemsCount(), 2);
  EXPECT_EQ(epub->getSpineItem(1).href, "OEBPS/chap2.xhtml");
}

TEST_F(EpubFixture, Epub3NavIsPreferredOverNcx) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  // The nav document lists two entries; the NCX lists one. Nav wins.
  ASSERT_EQ(epub->getTocItemsCount(), 2);
  EXPECT_EQ(epub->getTocItem(0).title, "One");
  EXPECT_EQ(epub->getTocItem(0).href, "OEBPS/chap1.xhtml");
  EXPECT_EQ(epub->getTocItem(1).title, "Three");
  EXPECT_EQ(epub->getSpineIndexForTocIndex(1), 2);
}

TEST_F(EpubFixture, NcxIsUsedWhenNoNavDocumentIsDeclared) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"ncx", "toc.ncx", "application/x-dtbncx+xml", ""},
                  {"c1", "chap1.xhtml", "application/xhtml+xml", ""},
                  {"c2", "chap2.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1", "c2"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/toc.ncx", epubtest::ncxXml({{"Ncx One", "chap1.xhtml"}, {"Ncx Two", "chap2.xhtml"}}))
      .add("OEBPS/chap1.xhtml", book.chapter1)
      .add("OEBPS/chap2.xhtml", book.chapter2);

  auto epub = make(writeEpub("ncx.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  ASSERT_EQ(epub->getTocItemsCount(), 2);
  EXPECT_EQ(epub->getTocItem(0).title, "Ncx One");
}

TEST_F(EpubFixture, NcxIsUsedWhenTheNavDocumentIsMissingFromTheArchive) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"nav", "nav.xhtml", "application/xhtml+xml", "nav"},
                  {"ncx", "toc.ncx", "application/x-dtbncx+xml", ""},
                  {"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;  // nav.xhtml is declared but absent
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/toc.ncx", epubtest::ncxXml({{"Fallback", "chap1.xhtml"}}))
      .add("OEBPS/chap1.xhtml", book.chapter1);

  auto epub = make(writeEpub("navmissing.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  ASSERT_EQ(epub->getTocItemsCount(), 1);
  EXPECT_EQ(epub->getTocItem(0).title, "Fallback");
}

TEST_F(EpubFixture, LoadSucceedsWithNoTocAtAll) {
  const auto book = standardBook();
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", book.chapter1);

  auto epub = make(writeEpub("notoc.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getTocItemsCount(), 0);
  EXPECT_EQ(epub->getSpineItemsCount(), 1);
  EXPECT_EQ(epub->getTocIndexForSpineIndex(0), -1);
}

TEST_F(EpubFixture, LoadWithoutBuildFailsUntilACacheExists) {
  const auto book = standardBook();
  const std::string path = writeEpub("book.epub", book.bytes);

  auto cold = make(path);
  EXPECT_FALSE(cold->load(/*buildIfMissing=*/false));
  EXPECT_FALSE(epubtest::pathExists(cold->getCachePath() + "/book.bin"));

  auto build = make(path);
  ASSERT_TRUE(build->load());

  auto warm = make(path);
  EXPECT_TRUE(warm->load(/*buildIfMissing=*/false));
  EXPECT_EQ(warm->getSpineItemsCount(), 3);
}

TEST_F(EpubFixture, WarmLoadReusesBookBinByteForByte) {
  const auto book = standardBook();
  const std::string path = writeEpub("book.epub", book.bytes);

  auto first = make(path);
  ASSERT_TRUE(first->load());
  const std::string bookBin = epubtest::readBytes(first->getCachePath() + "/book.bin");
  ASSERT_FALSE(bookBin.empty());

  auto second = make(path);
  ASSERT_TRUE(second->load());
  EXPECT_EQ(epubtest::readBytes(second->getCachePath() + "/book.bin"), bookBin);
}

TEST_F(EpubFixture, ClearCacheRemovesTheDirectoryAndIsANoopWhenAbsent) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  EXPECT_TRUE(epub->clearCache());  // nothing to clear yet

  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epubtest::pathExists(epub->getCachePath()));
  EXPECT_TRUE(epub->clearCache());
  EXPECT_FALSE(epubtest::pathExists(epub->getCachePath()));
}

TEST_F(EpubFixture, ItemReadHelpersResolveEntriesThroughTheZip) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  size_t size = 0;
  ASSERT_TRUE(epub->getItemSize("OEBPS/chap2.xhtml", &size));
  EXPECT_EQ(size, 200u);
  EXPECT_FALSE(epub->getItemSize("OEBPS/nope.xhtml", &size));

  size_t bytes = 0;
  uint8_t* content = epub->readItemContentsToBytes("OEBPS/chap1.xhtml", &bytes, true);
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(bytes, 100u);
  EXPECT_EQ(content[bytes], 0);
  free(content);

  EXPECT_EQ(epub->readItemContentsToBytes("", &bytes), nullptr);
  EXPECT_EQ(epub->readItemContentsToBytes("OEBPS/nope.xhtml", &bytes), nullptr);

  Collector sink;
  EXPECT_TRUE(epub->readItemContentsToStream("OEBPS/chap3.xhtml", sink, 64));
  EXPECT_EQ(sink.data, book.chapter3);
  EXPECT_FALSE(epub->readItemContentsToStream("", sink, 64));
}

TEST_F(EpubFixture, RelativeItemHrefsAreNormalisedBeforeTheZipLookup) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  size_t size = 0;
  EXPECT_TRUE(epub->getItemSize("OEBPS/images/../chap2.xhtml", &size));
  EXPECT_EQ(size, 200u);
}

TEST_F(EpubFixture, ExtractItemToFileWritesTheEntryAndCleansUpOnFailure) {
  const auto book = standardBook();
  auto epub = make(writeEpub("book.epub", book.bytes));
  ASSERT_TRUE(epub->load());

  const std::string dest = tmp.at("extracted.xhtml");
  EXPECT_TRUE(epub->extractItemToFile("OEBPS/chap2.xhtml", dest));
  EXPECT_EQ(epubtest::readBytes(dest), book.chapter2);

  const std::string missing = tmp.at("missing.xhtml");
  EXPECT_FALSE(epub->extractItemToFile("OEBPS/nope.xhtml", missing));
  EXPECT_FALSE(epubtest::pathExists(missing));

  Storage.failOpenForWrite.push_back("blocked.xhtml");
  EXPECT_FALSE(epub->extractItemToFile("OEBPS/chap2.xhtml", tmp.at("blocked.xhtml")));
}

}  // namespace
