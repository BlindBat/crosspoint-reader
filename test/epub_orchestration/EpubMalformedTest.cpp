// Constitution VI corpus for the orchestration layer: every one of these
// archives comes off an untrusted SD card, so Epub::load() must refuse it
// without reading out of bounds, allocating on a lying length, or looping.

#include "EpubFixture.h"

namespace {

using epubtest::ManifestItem;
using epubtest::OpfSpec;
using epubtest::ZipBuilder;

std::string wellFormedEpub() {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  return zip.build();
}

// Wraps any container.xml body in an otherwise valid archive.
std::string epubWithContainer(const std::string& containerBody) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", containerBody)
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  return zip.build();
}

TEST_F(EpubFixture, LoadFailsWhenTheFileIsNotOnDisk) {
  auto epub = make(tmp.at("nothing_here.epub"));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsOnAnEmptyFile) {
  auto epub = make(writeEpub("empty.epub", ""));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsOnATooShortFile) {
  auto epub = make(writeEpub("tiny.epub", "PK"));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheFileIsNotAZip) {
  std::string garbage;
  for (int i = 0; i < 4096; i++) garbage.push_back(static_cast<char>((i * 31 + 7) & 0xFF));
  auto epub = make(writeEpub("garbage.epub", garbage));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsOnAZipWithoutContainerXml) {
  ZipBuilder zip;
  zip.add("mimetype", "application/epub+zip").add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("nocontainer.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenContainerXmlIsEmpty) {
  auto epub = make(writeEpub("emptycontainer.epub", epubWithContainer("")));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheRootfileMediaTypeIsWrong) {
  auto epub = make(writeEpub("badmedia.epub",
                             epubWithContainer(epubtest::containerXml("OEBPS/content.opf", "text/plain"))));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheContainerDeclaresNoRootfile) {
  auto epub = make(writeEpub(
      "norootfile.epub",
      epubWithContainer("<?xml version=\"1.0\"?><container version=\"1.0\"><rootfiles/></container>")));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheRootfilePathIsEmpty) {
  auto epub = make(writeEpub("emptyroot.epub", epubWithContainer(epubtest::containerXml(""))));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenContainerXmlIsTruncatedMidElement) {
  std::string container = epubtest::containerXml("OEBPS/content.opf");
  container.resize(container.size() / 2);
  auto epub = make(writeEpub("cuthalf.epub", epubWithContainer(container)));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, HostileNestingInContainerXmlIsToleratedWithoutCrashing) {
  // Pinned, not endorsed: ContainerParser latches on <container> then
  // <rootfiles> at any depth, so a rootfile buried 3000 elements deep is still
  // accepted. What matters here is that 3000 levels neither crash nor hang.
  std::string container = "<?xml version=\"1.0\"?>\n<container version=\"1.0\">\n";
  constexpr int depth = 3000;
  for (int i = 0; i < depth; i++) container += "<n>";
  container += "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
               "media-type=\"application/oebps-package+xml\"/></rootfiles>";
  for (int i = 0; i < depth; i++) container += "</n>";
  container += "\n</container>\n";

  auto epub = make(writeEpub("nested.epub", epubWithContainer(container)));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineItemsCount(), 1);
}

TEST_F(EpubFixture, HostileNestingWithoutAValidRootfileStillFails) {
  std::string container = "<?xml version=\"1.0\"?>\n<container version=\"1.0\">\n";
  constexpr int depth = 3000;
  for (int i = 0; i < depth; i++) container += "<n>";
  for (int i = 0; i < depth; i++) container += "</n>";
  container += "\n</container>\n";

  auto epub = make(writeEpub("nested_empty.epub", epubWithContainer(container)));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, HostileNestingInTheOpfIsToleratedWithoutCrashing) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  std::string xml = epubtest::opfXml(opf);
  const size_t insertAt = xml.find("</package>");
  ASSERT_NE(insertAt, std::string::npos);
  std::string nest;
  constexpr int depth = 3000;
  for (int i = 0; i < depth; i++) nest += "<n>";
  for (int i = 0; i < depth; i++) nest += "</n>";
  xml.insert(insertAt, nest);

  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", xml)
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));

  auto epub = make(writeEpub("nestedopf.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineItemsCount(), 1);
  EXPECT_EQ(epub->getTitle(), "Test Book");
}

TEST_F(EpubFixture, LoadFailsWhenTheDeclaredOpfIsNotInTheArchive) {
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/missing.opf"))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("noopf.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheRootfilePathEscapesTheArchive) {
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("../../etc/passwd"))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("escape.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheOpfIsNotXml) {
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", std::string(512, '\x01'))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("binopf.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsOnInvalidUtf8InTheOpf) {
  OpfSpec opf;
  opf.title = "Bad\xC3\x28 Title";  // C3 28 is not a valid UTF-8 sequence
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("badutf8.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsOnATruncatedCentralDirectory) {
  std::string bytes = wellFormedEpub();
  // Drop the last 40 bytes: the EOCD goes with them.
  bytes.resize(bytes.size() - 40);
  auto epub = make(writeEpub("truncdir.epub", bytes));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheArchiveIsCutInHalf) {
  std::string bytes = wellFormedEpub();
  bytes.resize(bytes.size() / 2);
  auto epub = make(writeEpub("half.epub", bytes));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenTheEocdPointsPastTheEndOfTheFile) {
  std::string bytes = wellFormedEpub();
  ASSERT_GE(bytes.size(), 22u);
  // The EOCD is the last 22 bytes (no archive comment); patch its central
  // directory offset to a value beyond EOF.
  const size_t eocd = bytes.size() - 22;
  const uint32_t bogus = 0x7FFFFFF0u;
  for (int i = 0; i < 4; i++) bytes[eocd + 16 + i] = static_cast<char>((bogus >> (8 * i)) & 0xFF);
  auto epub = make(writeEpub("badeocd.epub", bytes));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenAStoredEntryLiesAboutItsSize) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  const std::string container = epubtest::containerXml("OEBPS/content.opf");
  ZipBuilder zip;
  // Stored entry whose declared uncompressed size does not match its payload.
  zip.addWithDeclaredSizes("META-INF/container.xml", container, static_cast<uint32_t>(container.size()), 0x00FFFFFFu)
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("lying.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenAnEntryClaimsAnUnsupportedCompressionMethod) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf")).withMethod(99);
  zip.add("OEBPS/content.opf", epubtest::opfXml(opf)).add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("badmethod.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, LoadFailsWhenACentralDirectoryEntryPointsOutsideTheFile) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  ZipBuilder zip;
  // The container entry is present in the central directory, but its local
  // header offset is a lie that lands far past EOF.
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf")).withLocalHeaderOffset(0x7FFFFFF0u);
  zip.add("OEBPS/content.opf", epubtest::opfXml(opf)).add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("badoffset.epub", zip.build()));
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, EntryNamesTooLongForTheScanBufferAreIgnored) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  const std::string longDir(300, 'd');
  ZipBuilder zip;
  zip.add(longDir + "/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("META-INF/container.xml", epubtest::containerXml("OEBPS/" + longDir + ".opf"))
      .add("OEBPS/" + longDir + ".opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));
  auto epub = make(writeEpub("longname.epub", zip.build()));
  // The OPF entry name exceeds the 256-byte scan buffer, so it is never found.
  EXPECT_FALSE(epub->load());
}

TEST_F(EpubFixture, AnOpfWithoutASpineLoadsWithAnEmptySpine) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.omitSpine = true;
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));

  auto epub = make(writeEpub("nospine.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineItemsCount(), 0);
  EXPECT_EQ(epub->getBookSize(), 0u);
  EXPECT_FLOAT_EQ(epub->calculateProgress(0, 0.5f), 0.0f);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap1.xhtml"), -1);
  EXPECT_EQ(epub->getSpineIndexForTextReference(), 0);
}

TEST_F(EpubFixture, AnEmptySpineElementLoadsWithAnEmptySpine) {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {};
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"));

  auto epub = make(writeEpub("emptyspine.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  EXPECT_EQ(epub->getSpineItemsCount(), 0);
  EXPECT_EQ(epub->getSpineItem(0).href, "");
}

TEST_F(EpubFixture, ACorruptBookBinIsRejectedAndRebuilt) {
  const std::string path = writeEpub("book.epub", wellFormedEpub());
  auto first = make(path);
  ASSERT_TRUE(first->load());
  const std::string bookBinPath = first->getCachePath() + "/book.bin";
  std::string bookBin = epubtest::readBytes(bookBinPath);
  ASSERT_FALSE(bookBin.empty());

  // Version byte no longer matches: the cache must be refused.
  bookBin[0] = static_cast<char>(0x7F);
  epubtest::writeBytes(bookBinPath, bookBin);

  auto noBuild = make(path);
  EXPECT_FALSE(noBuild->load(/*buildIfMissing=*/false));

  auto rebuild = make(path);
  ASSERT_TRUE(rebuild->load());
  EXPECT_EQ(rebuild->getSpineItemsCount(), 1);
  EXPECT_EQ(static_cast<uint8_t>(epubtest::readBytes(bookBinPath)[0]), 10);
}

TEST_F(EpubFixture, ATruncatedBookBinIsRejectedAndRebuilt) {
  const std::string path = writeEpub("book.epub", wellFormedEpub());
  auto first = make(path);
  ASSERT_TRUE(first->load());
  const std::string bookBinPath = first->getCachePath() + "/book.bin";
  std::string bookBin = epubtest::readBytes(bookBinPath);
  ASSERT_GT(bookBin.size(), 8u);
  bookBin.resize(bookBin.size() / 2);
  epubtest::writeBytes(bookBinPath, bookBin);

  auto rebuild = make(path);
  ASSERT_TRUE(rebuild->load());
  EXPECT_EQ(rebuild->getSpineItemsCount(), 1);
}

TEST_F(EpubFixture, ALoadFailureLeavesTheAccessorsInert) {
  auto epub = make(writeEpub("garbage.epub", std::string(2048, '\xAB')));
  ASSERT_FALSE(epub->load());

  EXPECT_EQ(epub->getTitle(), "");
  EXPECT_EQ(epub->getSpineItemsCount(), 0);
  EXPECT_EQ(epub->getTocItemsCount(), 0);
  EXPECT_EQ(epub->getBookSize(), 0u);
  EXPECT_EQ(epub->resolveHrefToSpineIndex("OEBPS/chap1.xhtml"), -1);
  EXPECT_FALSE(epub->generateCoverBmp());
}

TEST_F(EpubFixture, LoadFailsWhenTheCacheDirectoryCannotBeWritten) {
  auto epub = make(writeEpub("book.epub", wellFormedEpub()));
  Storage.failOpenForWrite.push_back("/spine.bin.tmp");
  EXPECT_FALSE(epub->load());
  EXPECT_FALSE(epubtest::pathExists(epub->getCachePath() + "/book.bin"));
}

}  // namespace
