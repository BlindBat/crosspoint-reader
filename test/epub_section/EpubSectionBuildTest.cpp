// EPUB Section pagination engine: build paths.
//
// Covers the one-shot build (createSectionFile), the incremental build
// (startBuild / buildSomeMore / isBuildComplete), the unified loadPage (from
// the active build and from the finalized file), on-disk LUT integrity,
// estimatedTotalPages during a build, abandonBuild, HTML-cache reuse, and the
// error paths (stream failure, malformed HTML).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EpubSectionTestSupport.h"

namespace {

using namespace sectest;

class EpubSectionBuildTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    // 60 paragraphs x 30 words: ~13KB of HTML (over the 10KB popup threshold),
    // ~90 pages with the 400x64 viewport and fixed-metric renderer.
    epub = makeEpub(tmp, buildChapterHtml(kParagraphs, kWordsPerParagraph));
  }

  std::unique_ptr<Section> makeSection() { return std::make_unique<Section>(epub, 0, renderer); }

  // Expected word stream of the whole chapter, in source order.
  static std::vector<std::string> expectedWords() {
    std::vector<std::string> words;
    for (int i = 0; i < kParagraphs; i++) {
      for (int j = 0; j < kWordsPerParagraph; j++) {
        words.push_back("p" + std::to_string(i) + "x" + std::to_string(j));
      }
    }
    return words;
  }

  static constexpr int kParagraphs = 60;
  static constexpr int kWordsPerParagraph = 30;

  TempDir tmp;
  GfxRenderer renderer;
  std::shared_ptr<Epub> epub;
};

TEST_F(EpubSectionBuildTest, OneShotBuildFinalizesSectionFile) {
  const auto spec = makeSpec();
  auto section = makeSection();

  ASSERT_TRUE(section->createSectionFile(spec));
  EXPECT_TRUE(section->isBuildComplete());
  EXPECT_FALSE(section->isBuilding());
  EXPECT_FALSE(section->isPartial());
  EXPECT_GT(section->pageCount, 20);

  EXPECT_TRUE(fileExists(sectionBinPath(*epub)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));  // tmp .part swapped into place
}

TEST_F(EpubSectionBuildTest, HeaderStampsVersionSpecAndTableOffsets) {
  const auto spec = makeSpec();
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(spec));

  const std::string bytes = readAll(sectionBinPath(*epub));
  ASSERT_GE(bytes.size(), kHeaderSize);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffVersion), kSectionFileVersion);
  EXPECT_EQ(readAt<int>(bytes, kOffFontId), spec.fontId);
  EXPECT_FLOAT_EQ(readAt<float>(bytes, kOffLineCompression), spec.lineCompression);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffExtraSpacing), spec.extraParagraphSpacing ? 1 : 0);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffAlignment), spec.paragraphAlignment);
  EXPECT_EQ(readAt<uint16_t>(bytes, kOffViewportWidth), spec.viewportWidth);
  EXPECT_EQ(readAt<uint16_t>(bytes, kOffViewportHeight), spec.viewportHeight);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffHyphenation), spec.hyphenationEnabled ? 1 : 0);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffEmbeddedStyle), spec.embeddedStyle ? 1 : 0);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffImageRendering), spec.imageRendering);
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffFocusReading), spec.focusReadingEnabled ? 1 : 0);
  EXPECT_EQ(readAt<uint16_t>(bytes, kOffPageCount), section->pageCount);

  // Table offsets are patched in and strictly ordered:
  // header | pages | page LUT | anchor map | paragraph LUT | li LUT | visible LUT.
  const auto lutOffset = readAt<uint32_t>(bytes, kOffLutOffset);
  const auto anchorMapOffset = readAt<uint32_t>(bytes, kOffAnchorMapOffset);
  const auto paragraphLutOffset = readAt<uint32_t>(bytes, kOffParagraphLutOffset);
  const auto liLutOffset = readAt<uint32_t>(bytes, kOffLiLutOffset);
  const auto visibleLutOffset = readAt<uint32_t>(bytes, kOffVisibleLutOffset);
  EXPECT_GE(lutOffset, kHeaderSize);
  EXPECT_LT(lutOffset, anchorMapOffset);
  EXPECT_LT(anchorMapOffset, paragraphLutOffset);
  EXPECT_LT(paragraphLutOffset, liLutOffset);
  EXPECT_LT(liLutOffset, visibleLutOffset);
  // A finalized file ends exactly at the end of the visible-offset LUT (no
  // watermark trailer -- that is partial-only).
  EXPECT_EQ(bytes.size(), visibleLutOffset + section->pageCount * sizeof(uint32_t));

  // Page LUT: page 0 starts right after the header, entries strictly increase,
  // and all point before the LUT itself.
  uint32_t previous = 0;
  for (uint16_t i = 0; i < section->pageCount; i++) {
    const auto entry = readAt<uint32_t>(bytes, lutOffset + i * sizeof(uint32_t));
    if (i == 0) {
      EXPECT_EQ(entry, kHeaderSize);
    } else {
      EXPECT_GT(entry, previous);
    }
    EXPECT_LT(entry, lutOffset);
    previous = entry;
  }
}

TEST_F(EpubSectionBuildTest, LoadPageRoundTripsBuildOutputAndSourceText) {
  const auto spec = makeSpec();
  auto section = makeSection();

  // Drive the build incrementally, reading every page back through loadPage
  // WHILE the build is running (served from the partially-written tmp .bin).
  ASSERT_TRUE(section->startBuild(spec));
  std::vector<std::vector<std::string>> duringBuild;
  int guard = 0;
  while (!section->isBuildComplete()) {
    ASSERT_TRUE(section->buildSomeMore(3));
    while (static_cast<int>(duringBuild.size()) < section->pageCount && section->isBuilding()) {
      const auto page = section->loadPage(static_cast<int>(duringBuild.size()));
      ASSERT_NE(page, nullptr) << "page " << duringBuild.size() << " unreadable during build";
      duringBuild.push_back(pageWords(*page));
    }
    ASSERT_LT(guard++, 10000);
  }
  ASSERT_GT(section->pageCount, 20);

  // After finalization the same pages come from the committed file and must
  // match the during-build reads word for word.
  std::vector<std::string> fromDisk;
  for (uint16_t p = 0; p < section->pageCount; p++) {
    const auto page = section->loadPage(p);
    ASSERT_NE(page, nullptr) << "page " << p << " unreadable after build";
    const auto words = pageWords(*page);
    if (p < duringBuild.size()) {
      EXPECT_EQ(words, duringBuild[p]) << "page " << p << " differs between build and disk";
    }
    fromDisk.insert(fromDisk.end(), words.begin(), words.end());
  }

  // The concatenated pages reproduce the chapter's word stream exactly.
  EXPECT_EQ(fromDisk, expectedWords());

  // Out-of-range reads fail cleanly.
  EXPECT_EQ(section->loadPage(section->pageCount), nullptr);
  EXPECT_EQ(section->loadPage(-1), nullptr);
}

TEST_F(EpubSectionBuildTest, IncrementalBuildMatchesOneShotBuild) {
  const auto spec = makeSpec();

  auto oneShot = makeSection();
  ASSERT_TRUE(oneShot->createSectionFile(spec));
  const uint16_t oneShotCount = oneShot->pageCount;
  const auto oneShotWords = allWords(*oneShot, oneShotCount);
  ASSERT_FALSE(oneShotWords.empty());
  ASSERT_TRUE(oneShot->clearCache());
  oneShot.reset();

  auto incremental = makeSection();
  ASSERT_TRUE(incremental->startBuild(spec));
  int guard = 0;
  while (!incremental->isBuildComplete()) {
    ASSERT_TRUE(incremental->buildSomeMore(1));
    ASSERT_LT(guard++, 10000);
  }
  EXPECT_EQ(incremental->pageCount, oneShotCount);
  EXPECT_EQ(allWords(*incremental, incremental->pageCount), oneShotWords);
}

TEST_F(EpubSectionBuildTest, BuildSomeMorePacesOnRequestedPageCount) {
  const auto spec = makeSpec();
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(spec));
  EXPECT_TRUE(section->isBuilding());
  EXPECT_EQ(section->pageCount, 0);

  ASSERT_TRUE(section->buildSomeMore(2));
  EXPECT_FALSE(section->isBuildComplete());
  // At least the requested pages; overshoot is bounded by the pages one
  // 1KB parse chunk can complete (a page here is ~150 bytes of source).
  EXPECT_GE(section->pageCount, 2);
  EXPECT_LE(section->pageCount, 30);

  // Page counts never regress while stepping.
  uint16_t last = section->pageCount;
  int guard = 0;
  while (!section->isBuildComplete()) {
    ASSERT_TRUE(section->buildSomeMore(2));
    EXPECT_GE(section->pageCount, last);
    last = section->pageCount;
    ASSERT_LT(guard++, 10000);
  }
  EXPECT_FALSE(section->isBuilding());
  EXPECT_TRUE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionBuildTest, EstimatedTotalPagesTracksBuildAndSettlesExact) {
  const auto spec = makeSpec();
  auto section = makeSection();

  EXPECT_EQ(section->estimatedTotalPages(), 0);  // nothing built, nothing known
  ASSERT_TRUE(section->startBuild(spec));
  EXPECT_EQ(section->estimatedTotalPages(), 0);  // no pages laid out yet

  ASSERT_TRUE(section->buildSomeMore(2));
  ASSERT_FALSE(section->isBuildComplete());
  const uint16_t earlyPages = section->pageCount;
  const uint16_t earlyEstimate = section->estimatedTotalPages();
  // Early in the build the byte-ratio extrapolation must project well past the
  // handful of pages laid out so far.
  EXPECT_GT(earlyEstimate, earlyPages);
  // The EMA is keyed on build advances: with no new parsing, repeated reads
  // return the identical estimate (no per-redraw drift).
  EXPECT_EQ(section->estimatedTotalPages(), earlyEstimate);
  EXPECT_EQ(section->estimatedTotalPages(), earlyEstimate);

  int guard = 0;
  while (!section->isBuildComplete()) {
    ASSERT_TRUE(section->buildSomeMore(2));
    // The estimate never reads below the pages already available.
    EXPECT_GE(section->estimatedTotalPages(), section->pageCount);
    ASSERT_LT(guard++, 10000);
  }

  // Finalized: the estimate is the exact page count.
  EXPECT_EQ(section->estimatedTotalPages(), section->pageCount);
}

TEST_F(EpubSectionBuildTest, AbandonBuildLeavesNoFilesAndNoPages) {
  const auto spec = makeSpec();
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(spec));
  ASSERT_TRUE(section->buildSomeMore(2));
  ASSERT_GT(section->pageCount, 0);

  section->abandonBuild();
  EXPECT_FALSE(section->isBuilding());
  EXPECT_FALSE(section->isBuildComplete());
  EXPECT_FALSE(section->isPartial());
  EXPECT_EQ(section->pageCount, 0);
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));
  EXPECT_EQ(section->loadPage(0), nullptr);
}

TEST_F(EpubSectionBuildTest, ParseErrorAbandonsBuildAndCleansUp) {
  auto broken = std::make_shared<Epub>();
  broken->cachePath = tmp.path() + "/epub_broken";
  broken->spine.push_back({kSpineHref});
  broken->items[kSpineHref] = "<?xml version=\"1.0\"?><html><body><p>never closed";

  Section section(broken, 0, renderer);
  EXPECT_FALSE(section.createSectionFile(makeSpec()));
  EXPECT_FALSE(section.isBuilding());
  EXPECT_FALSE(section.isBuildComplete());
  EXPECT_EQ(section.pageCount, 0);
  EXPECT_FALSE(fileExists(sectionBinPath(*broken)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*broken)));
}

TEST_F(EpubSectionBuildTest, StreamFailureFailsStartBuildAfterRetries) {
  epub->failStreaming = true;
  auto section = makeSection();
  EXPECT_FALSE(section->startBuild(makeSpec()));
  EXPECT_FALSE(section->isBuilding());
  // The SD retry loop attempts the inflation three times before giving up.
  EXPECT_EQ(epub->streamCalls, 3);
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));
  EXPECT_FALSE(section->hasHtmlCache());
}

TEST_F(EpubSectionBuildTest, HtmlCacheIsPromotedAndReusedAcrossBuilds) {
  const auto spec = makeSpec();
  auto first = makeSection();
  EXPECT_FALSE(first->hasHtmlCache());
  ASSERT_TRUE(first->createSectionFile(spec));
  EXPECT_TRUE(first->hasHtmlCache());
  EXPECT_TRUE(fileExists(htmlCachePath(*epub)));
  EXPECT_EQ(epub->streamCalls, 1);
  const uint16_t firstCount = first->pageCount;

  // clearCache drops only the layout cache; the unzipped HTML survives.
  ASSERT_TRUE(first->clearCache());
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
  EXPECT_TRUE(first->hasHtmlCache());
  first.reset();

  // A rebuild parses the cached HTML instead of re-streaming the zip item.
  auto second = makeSection();
  ASSERT_TRUE(second->createSectionFile(spec));
  EXPECT_EQ(epub->streamCalls, 1);
  EXPECT_EQ(second->pageCount, firstCount);
}

TEST_F(EpubSectionBuildTest, PopupFiresForLargeChaptersOnly) {
  int popups = 0;
  const BuildPopupFn popupFn{[](void* ctx) { ++*static_cast<int*>(ctx); }, &popups};

  // Main fixture is ~13KB of HTML: over the 10KB indexing-popup threshold.
  auto big = makeSection();
  ASSERT_TRUE(big->createSectionFile(makeSpec(), popupFn));
  EXPECT_EQ(popups, 1);

  auto smallEpub = std::make_shared<Epub>();
  smallEpub->cachePath = tmp.path() + "/epub_small";
  smallEpub->spine.push_back({kSpineHref});
  smallEpub->items[kSpineHref] = buildChapterHtml(3, 10);
  Section small(smallEpub, 0, renderer);
  popups = 0;
  ASSERT_TRUE(small.createSectionFile(makeSpec(), popupFn));
  EXPECT_EQ(popups, 0);
}

TEST_F(EpubSectionBuildTest, StartBuildWhileBuildActiveIsRejected) {
  const auto spec = makeSpec();
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(spec));
  EXPECT_FALSE(section->startBuild(spec));
  EXPECT_TRUE(section->isBuilding());  // original build unharmed
  ASSERT_TRUE(section->buildSomeMore(1));
  EXPECT_GT(section->pageCount, 0);
  section->abandonBuild();
}

TEST_F(EpubSectionBuildTest, GetTextFromSectionFileReadsCurrentPage) {
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));

  section->currentPage = 0;
  const std::string text = section->getTextFromSectionFile();
  EXPECT_EQ(text.rfind("p0x0 p0x1 ", 0), 0u) << "page 0 text was: " << text;

  section->currentPage = section->pageCount;  // out of range -> empty
  EXPECT_TRUE(section->getTextFromSectionFile().empty());
}

TEST_F(EpubSectionBuildTest, LoadPageWithoutAnyBuildOrFileFails) {
  auto section = makeSection();
  EXPECT_EQ(section->loadPage(0), nullptr);
  EXPECT_FALSE(section->loadSectionFile(makeSpec()));
  EXPECT_EQ(section->getCachedPageCount(), std::nullopt);
}

}  // namespace
