// Cover and thumbnail selection (FR-046): which href Epub ends up converting,
// and what it does when there is no usable cover or the decode fails.

#include "EpubFixture.h"

namespace {

using epubtest::ManifestItem;
using epubtest::OpfSpec;
using epubtest::ZipBuilder;

constexpr char kJpgBytes[] = "JPEG-PAYLOAD";
constexpr char kPngBytes[] = "PNG-PAYLOAD";
constexpr char kGifBytes[] = "GIF-PAYLOAD";

// A book with every cover candidate present in the archive; the OPF decides
// which one wins.
std::string coverBookBytes(const OpfSpec& opf, const std::string& coverPageHref = "",
                           const std::string& coverPageXhtml = "") {
  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"))
      .add("OEBPS/cover.jpg", kJpgBytes)
      .add("OEBPS/images/cover.png", kPngBytes)
      .add("OEBPS/images/cover.gif", kGifBytes);
  if (!coverPageHref.empty()) zip.add(coverPageHref, coverPageXhtml);
  return zip.build();
}

OpfSpec baseCoverOpf() {
  OpfSpec opf;
  opf.manifest = {{"c1", "chap1.xhtml", "application/xhtml+xml", ""}};
  opf.spine = {"c1"};
  return opf;
}

TEST_F(EpubFixture, MetaCoverWithAnImageMediaTypeWins) {
  OpfSpec opf = baseCoverOpf();
  opf.metaExtra = "    <meta name=\"cover\" content=\"cover-img\"/>\n";
  opf.manifest.push_back({"cover-img", "cover.jpg", "image/jpeg", ""});
  opf.manifest.push_back({"other", "images/cover.png", "image/png", "cover-image"});

  auto epub = make(writeEpub("meta.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());

  EXPECT_EQ(imgconv_host::jpeg.calls, 1);
  EXPECT_EQ(imgconv_host::png.calls, 0);
  EXPECT_EQ(imgconv_host::jpeg.lastInput, kJpgBytes);
  EXPECT_TRUE(epubtest::pathExists(epub->getCoverBmpPath()));
}

TEST_F(EpubFixture, MetaCoverWithANonImageMediaTypeIsIgnoredInFavourOfCoverImageProperties) {
  OpfSpec opf = baseCoverOpf();
  opf.metaExtra = "    <meta name=\"cover\" content=\"wrapper\"/>\n";
  opf.manifest.push_back({"wrapper", "cover_page.xhtml", "application/xhtml+xml", ""});
  opf.manifest.push_back({"prop", "images/cover.png", "image/png", "cover-image"});

  auto epub = make(writeEpub("wrapper.epub", coverBookBytes(opf, "OEBPS/cover_page.xhtml", "<html/>")));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());

  EXPECT_EQ(imgconv_host::png.calls, 1);
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_EQ(imgconv_host::png.lastInput, kPngBytes);
}

TEST_F(EpubFixture, CoverImagePropertyIsUsedWhenNoMetaCoverExists) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "images/cover.png", "image/png", "svg cover-image"});

  auto epub = make(writeEpub("prop.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::png.lastInput, kPngBytes);
}

TEST_F(EpubFixture, AMetaCoverIdThatNamesNothingLeavesTheBookWithoutACover) {
  OpfSpec opf = baseCoverOpf();
  opf.metaExtra = "    <meta name=\"cover\" content=\"does-not-exist\"/>\n";

  auto epub = make(writeEpub("ghost.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_EQ(imgconv_host::png.calls, 0);
  EXPECT_FALSE(epubtest::pathExists(epub->getCoverBmpPath()));
}

TEST_F(EpubFixture, GuideCoverPageXlinkHrefSuppliesTheCoverWhenNothingElseDoes) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"cp", "cover_page.xhtml", "application/xhtml+xml", ""});
  opf.guide = "  <guide>\n    <reference type=\"cover\" href=\"cover_page.xhtml\"/>\n  </guide>\n";
  const std::string page = "<html><body><svg><image xlink:href=\"images/cover.png\"/></svg></body></html>";

  auto epub = make(writeEpub("guide.epub", coverBookBytes(opf, "OEBPS/cover_page.xhtml", page)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::png.lastInput, kPngBytes);
}

TEST_F(EpubFixture, GuideCoverPageFallsBackToASrcAttribute) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"cp", "cover_page.xhtml", "application/xhtml+xml", ""});
  opf.guide = "  <guide>\n    <reference type=\"cover-page\" href=\"cover_page.xhtml\"/>\n  </guide>\n";
  const std::string page = "<html><body><img src=\"cover.jpg\" alt=\"cover\"/></body></html>";

  auto epub = make(writeEpub("guidesrc.epub", coverBookBytes(opf, "OEBPS/cover_page.xhtml", page)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.lastInput, kJpgBytes);
}

TEST_F(EpubFixture, GuideCoverPageSkipsAnUnsupportedGifAndTakesTheNextCandidate) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"cp", "cover_page.xhtml", "application/xhtml+xml", ""});
  opf.guide = "  <guide>\n    <reference type=\"cover\" href=\"cover_page.xhtml\"/>\n  </guide>\n";
  const std::string page = "<html><body><image xlink:href=\"images/cover.gif\"/><img src=\"cover.jpg\"/></body></html>";

  auto epub = make(writeEpub("guidegif.epub", coverBookBytes(opf, "OEBPS/cover_page.xhtml", page)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.lastInput, kJpgBytes);
  EXPECT_EQ(imgconv_host::png.calls, 0);
}

TEST_F(EpubFixture, GuideCoverPageImageRefIsResolvedRelativeToThePageAndUriDecoded) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"cp", "pages/cover_page.xhtml", "application/xhtml+xml", ""});
  opf.guide = "  <guide>\n    <reference type=\"cover\" href=\"pages/cover_page.xhtml\"/>\n  </guide>\n";
  const std::string page = "<html><body><image xlink:href=\"../art/my%20cover.png\"/></body></html>";

  ZipBuilder zip;
  zip.add("META-INF/container.xml", epubtest::containerXml("OEBPS/content.opf"))
      .add("OEBPS/content.opf", epubtest::opfXml(opf))
      .add("OEBPS/chap1.xhtml", epubtest::chapterXhtml("body"))
      .add("OEBPS/pages/cover_page.xhtml", page)
      .add("OEBPS/art/my cover.png", kPngBytes);

  auto epub = make(writeEpub("guiderel.epub", zip.build()));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::png.lastInput, kPngBytes);
}

TEST_F(EpubFixture, AGuideCoverPageWithOnlyUnsupportedImagesYieldsNoCover) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"cp", "cover_page.xhtml", "application/xhtml+xml", ""});
  opf.guide = "  <guide>\n    <reference type=\"cover\" href=\"cover_page.xhtml\"/>\n  </guide>\n";
  const std::string page = "<html><body><image xlink:href=\"images/cover.gif\"/></body></html>";

  auto epub = make(writeEpub("onlygif.epub", coverBookBytes(opf, "OEBPS/cover_page.xhtml", page)));
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_EQ(imgconv_host::png.calls, 0);
}

TEST_F(EpubFixture, ACoverWithAnUnsupportedExtensionIsNotConverted) {
  OpfSpec opf = baseCoverOpf();
  opf.metaExtra = "    <meta name=\"cover\" content=\"gif\"/>\n";
  opf.manifest.push_back({"gif", "images/cover.gif", "image/gif", ""});

  auto epub = make(writeEpub("gif.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_FALSE(epubtest::pathExists(epub->getCoverBmpPath()));
}

TEST_F(EpubFixture, CroppedCoversUseTheirOwnPathAndPassTheCropFlagThrough) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("crop.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());

  ASSERT_TRUE(epub->generateCoverBmp(/*cropped=*/false));
  EXPECT_FALSE(imgconv_host::jpeg.lastCrop);
  ASSERT_TRUE(epub->generateCoverBmp(/*cropped=*/true));
  EXPECT_TRUE(imgconv_host::jpeg.lastCrop);

  EXPECT_TRUE(epubtest::pathExists(epub->getCoverBmpPath(false)));
  EXPECT_TRUE(epubtest::pathExists(epub->getCoverBmpPath(true)));
  EXPECT_EQ(imgconv_host::jpeg.calls, 2);
}

TEST_F(EpubFixture, AnExistingCoverBmpShortCircuitsTheConversion) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("existing.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  epubtest::writeBytes(epub->getCoverBmpPath(), "already here");

  EXPECT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_EQ(epubtest::readBytes(epub->getCoverBmpPath()), "already here");
}

TEST_F(EpubFixture, AFailedCoverDecodeRemovesThePartialOutput) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("faildecode.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  imgconv_host::jpeg.result = false;

  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 1);
  EXPECT_FALSE(epubtest::pathExists(epub->getCoverBmpPath()));
  // The extracted temp image is cleaned up either way.
  EXPECT_FALSE(epubtest::pathExists(epub->getCachePath() + "/.cover.jpg"));
}

TEST_F(EpubFixture, CoverGenerationRequiresALoadedCache) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});
  const Epub epub(writeEpub("unloaded.epub", coverBookBytes(opf)), cacheRoot);

  EXPECT_FALSE(epub.generateCoverBmp());
  EXPECT_FALSE(epub.generateThumbBmp(200));
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
}

TEST_F(EpubFixture, ACoverHrefMissingFromTheArchiveStillReachesTheDecoderWithNoBytes) {
  // Pinned, not endorsed: generateCoverBmp ignores the extraction result, so a
  // manifest cover that is not in the ZIP hands the decoder an empty file.
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "absent.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("absent.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  EXPECT_TRUE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 1);
  EXPECT_TRUE(imgconv_host::jpeg.lastInput.empty());
}

TEST_F(EpubFixture, ThumbnailsUseTheOneBitPathAtSixTenthsOfTheRequestedHeight) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("thumb.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  ASSERT_TRUE(epub->generateThumbBmp(200));

  EXPECT_EQ(imgconv_host::jpeg.calls, 1);
  EXPECT_EQ(imgconv_host::jpeg.lastTargetWidth, 120);
  EXPECT_EQ(imgconv_host::jpeg.lastTargetHeight, 200);
  EXPECT_TRUE(epubtest::pathExists(epub->getThumbBmpPath(200)));
}

TEST_F(EpubFixture, AnExistingThumbnailShortCircuitsTheConversion) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("thumbexisting.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  epubtest::writeBytes(epub->getThumbBmpPath(150), "cached thumb");

  EXPECT_TRUE(epub->generateThumbBmp(150));
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
}

TEST_F(EpubFixture, NoCoverWritesAnEmptyThumbnailMarkerSoItIsNotRetried) {
  OpfSpec opf = baseCoverOpf();

  auto epub = make(writeEpub("nocover.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epub->generateThumbBmp(180));

  EXPECT_TRUE(epubtest::pathExists(epub->getThumbBmpPath(180)));
  EXPECT_TRUE(epubtest::readBytes(epub->getThumbBmpPath(180)).empty());
  // The marker makes the next call a short-circuit hit.
  EXPECT_TRUE(epub->generateThumbBmp(180));
}

TEST_F(EpubFixture, AnUnsupportedCoverExtensionAlsoWritesTheThumbnailMarker) {
  OpfSpec opf = baseCoverOpf();
  opf.metaExtra = "    <meta name=\"cover\" content=\"gif\"/>\n";
  opf.manifest.push_back({"gif", "images/cover.gif", "image/gif", ""});

  auto epub = make(writeEpub("gifthumb.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  EXPECT_FALSE(epub->generateThumbBmp(220));
  EXPECT_TRUE(epubtest::pathExists(epub->getThumbBmpPath(220)));
  EXPECT_TRUE(epubtest::readBytes(epub->getThumbBmpPath(220)).empty());
}

TEST_F(EpubFixture, AFailedThumbnailDecodeRemovesTheOutputWithoutLeavingAMarker) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "images/cover.png", "image/png", "cover-image"});

  auto epub = make(writeEpub("thumbfail.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  imgconv_host::png.result = false;

  EXPECT_FALSE(epub->generateThumbBmp(240));
  EXPECT_EQ(imgconv_host::png.calls, 1);
  EXPECT_FALSE(epubtest::pathExists(epub->getThumbBmpPath(240)));
  EXPECT_FALSE(epubtest::pathExists(epub->getCachePath() + "/.cover.png"));
}

TEST_F(EpubFixture, CoverGenerationFailsWhenTheBmpOutputCannotBeOpened) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("nobmp.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  Storage.failOpenForWrite.push_back("/cover.bmp");

  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_FALSE(epubtest::pathExists(epub->getCoverBmpPath()));
  // Pinned, not endorsed: this early return skips the temp-image cleanup every
  // other exit path performs, so the extracted cover stays in the cache dir.
  EXPECT_TRUE(epubtest::pathExists(epub->getCachePath() + "/.cover.jpg"));
}

TEST_F(EpubFixture, ThumbnailGenerationFailsWhenTheOutputCannotBeOpened) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("nothumb.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  Storage.failOpenForWrite.push_back("/thumb_200.bmp");

  EXPECT_FALSE(epub->generateThumbBmp(200));
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
  EXPECT_FALSE(epubtest::pathExists(epub->getThumbBmpPath(200)));
  // Same leak as the cover path: no marker file, and the temp image survives.
  EXPECT_TRUE(epubtest::pathExists(epub->getCachePath() + "/.cover.jpg"));
}

TEST_F(EpubFixture, CoverGenerationFailsWhenTheTempImageCannotBeWritten) {
  OpfSpec opf = baseCoverOpf();
  opf.manifest.push_back({"prop", "cover.jpg", "image/jpeg", "cover-image"});

  auto epub = make(writeEpub("nowrite.epub", coverBookBytes(opf)));
  ASSERT_TRUE(epub->load());
  Storage.failOpenForWrite.push_back("/.cover.jpg");

  EXPECT_FALSE(epub->generateCoverBmp());
  EXPECT_EQ(imgconv_host::jpeg.calls, 0);
}

}  // namespace
