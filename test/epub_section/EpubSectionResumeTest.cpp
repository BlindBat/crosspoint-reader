// EPUB Section pagination engine: suspend/resume of incremental builds.
//
// suspendBuild persists the pages already laid out as a partial section file
// (partial version sentinel + all LUTs + a bytesConsumed/totalBytes watermark
// trailer). A later open serves those pages instantly while a background
// rebuild extends past them; a rebuild that got less far than an existing
// partial must not clobber it.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "EpubSectionTestSupport.h"

namespace {

using namespace sectest;

class EpubSectionResumeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    html = buildChapterHtml(kParagraphs, kWordsPerParagraph);
    epub = makeEpub(tmp, html);
  }

  std::unique_ptr<Section> makeSection() { return std::make_unique<Section>(epub, 0, renderer); }

  // Reference: full build of the same chapter under a separate cache dir.
  void buildReference() {
    auto refEpub = std::make_shared<Epub>();
    refEpub->cachePath = tmp.path() + "/epub_reference";
    refEpub->spine.push_back({kSpineHref});
    refEpub->items[kSpineHref] = html;
    Section ref(refEpub, 0, renderer);
    ASSERT_TRUE(ref.createSectionFile(makeSpec()));
    referenceCount = ref.pageCount;
    referenceWords.clear();
    for (uint16_t p = 0; p < ref.pageCount; p++) {
      const auto page = ref.loadPage(p);
      ASSERT_NE(page, nullptr);
      referenceWords.push_back(pageWords(*page));
    }
    ASSERT_GT(referenceCount, 30);
  }

  // Build until at least `pages` pages, then suspend; returns the watermark.
  uint16_t buildAndSuspend(Section& section, const int pages) {
    EXPECT_TRUE(section.startBuild(makeSpec()));
    int guard = 0;
    while (section.pageCount < pages && !section.isBuildComplete()) {
      EXPECT_TRUE(section.buildSomeMore(2));
      if (guard++ > 10000) break;
    }
    EXPECT_FALSE(section.isBuildComplete()) << "fixture too small to suspend at " << pages << " pages";
    section.suspendBuild();
    return section.pageCount;
  }

  static constexpr int kParagraphs = 50;
  static constexpr int kWordsPerParagraph = 30;

  TempDir tmp;
  GfxRenderer renderer;
  std::shared_ptr<Epub> epub;
  std::string html;
  uint16_t referenceCount = 0;
  std::vector<std::vector<std::string>> referenceWords;
};

TEST_F(EpubSectionResumeTest, SuspendWritesPartialFileWithSentinelAndTrailer) {
  auto section = makeSection();
  const uint16_t watermark = buildAndSuspend(*section, 20);
  ASSERT_GE(watermark, 20);

  EXPECT_FALSE(section->isBuilding());
  EXPECT_TRUE(section->isPartial());
  EXPECT_EQ(section->pageCount, watermark);
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));  // tmp swapped into place

  const std::string bytes = readAll(sectionBinPath(*epub));
  ASSERT_GE(bytes.size(), kHeaderSize);
  // The partial sentinel is derived from the format version in lockstep
  // (0xFE for v28, one less per version bump) -- pin the pairing.
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffVersion), kPartialVersion);
  EXPECT_EQ(readAt<uint16_t>(bytes, kOffPageCount), watermark);

  // Watermark trailer sits right after the visible-offset LUT and holds a
  // plausible parse position: 0 < bytesConsumed <= totalBytes == HTML size.
  const auto visibleLutOffset = readAt<uint32_t>(bytes, kOffVisibleLutOffset);
  const size_t trailerOffset = visibleLutOffset + watermark * sizeof(uint32_t);
  ASSERT_EQ(bytes.size(), trailerOffset + 2 * sizeof(uint32_t));
  const auto bytesConsumed = readAt<uint32_t>(bytes, trailerOffset);
  const auto totalBytes = readAt<uint32_t>(bytes, trailerOffset + sizeof(uint32_t));
  EXPECT_GT(bytesConsumed, 0u);
  EXPECT_LE(bytesConsumed, totalBytes);
  EXPECT_EQ(totalBytes, html.size());
}

TEST_F(EpubSectionResumeTest, ReopenedPartialServesPagesUpToWatermark) {
  buildReference();
  auto builder = makeSection();
  const uint16_t watermark = buildAndSuspend(*builder, 20);
  builder.reset();

  auto reopened = makeSection();
  ASSERT_TRUE(reopened->loadSectionFile(makeSpec()));
  EXPECT_TRUE(reopened->isPartial());
  EXPECT_EQ(reopened->pageCount, watermark);
  ASSERT_LT(watermark, referenceCount);

  // Every page up to the watermark is readable and identical to the same page
  // of an uninterrupted full build; past the watermark there is nothing.
  for (uint16_t p = 0; p < watermark; p++) {
    const auto page = reopened->loadPage(p);
    ASSERT_NE(page, nullptr) << "partial page " << p;
    EXPECT_EQ(pageWords(*page), referenceWords[p]) << "partial page " << p << " diverges from full build";
  }
  EXPECT_EQ(reopened->loadPage(watermark), nullptr);

  // A partial's count is a watermark, not a chapter total.
  EXPECT_EQ(reopened->getCachedPageCount(), std::nullopt);

  // The watermark trailer lets the section extrapolate a total.
  const uint16_t estimate = reopened->estimatedTotalPages();
  EXPECT_GT(estimate, watermark);
  EXPECT_LE(estimate, 60000);

  // Anchors up to the watermark resolve; anchors past it don't yet.
  ASSERT_TRUE(reopened->findAnchor("pp0").has_value());
  EXPECT_EQ(*reopened->findAnchor("pp0"), 0);
  EXPECT_EQ(reopened->findAnchor("pp" + std::to_string(kParagraphs - 1)), std::nullopt);

  // Visible-offset resolution stops at the watermark too.
  const auto lastOffset = reopened->getVisibleTextOffsetForPage(watermark - 1);
  ASSERT_TRUE(lastOffset.has_value());
  EXPECT_EQ(reopened->getPageForVisibleTextOffset(*lastOffset), static_cast<uint16_t>(watermark - 1));
  EXPECT_EQ(reopened->getPageForVisibleTextOffset(*lastOffset + 100000), std::nullopt);
}

TEST_F(EpubSectionResumeTest, RebuildOverPartialCatchesUpToFullChapter) {
  buildReference();
  auto builder = makeSection();
  const uint16_t watermark = buildAndSuspend(*builder, 20);
  builder.reset();

  auto resumed = makeSection();
  ASSERT_TRUE(resumed->loadSectionFile(makeSpec()));
  ASSERT_TRUE(resumed->isPartial());

  // While the rebuild is behind the watermark, availability never drops: the
  // partial's pages keep coming from disk and pageCount stays pinned.
  ASSERT_TRUE(resumed->startBuild(makeSpec()));
  EXPECT_EQ(resumed->pageCount, watermark);
  auto beforeCatchUp = resumed->loadPage(watermark - 1);
  ASSERT_NE(beforeCatchUp, nullptr);
  EXPECT_EQ(pageWords(*beforeCatchUp), referenceWords[watermark - 1]);

  ASSERT_TRUE(resumed->buildSomeMore(2));
  EXPECT_GE(resumed->pageCount, watermark);  // pinned until the build passes it
  EXPECT_GE(resumed->estimatedTotalPages(), resumed->pageCount);

  int guard = 0;
  while (!resumed->isBuildComplete()) {
    ASSERT_TRUE(resumed->buildSomeMore(4));
    ASSERT_LT(guard++, 10000);
  }
  EXPECT_FALSE(resumed->isPartial());
  EXPECT_EQ(resumed->pageCount, referenceCount);
  EXPECT_EQ(resumed->getCachedPageCount(), std::optional<uint16_t>(referenceCount));
  for (uint16_t p = 0; p < referenceCount; p++) {
    const auto page = resumed->loadPage(p);
    ASSERT_NE(page, nullptr) << "page " << p;
    EXPECT_EQ(pageWords(*page), referenceWords[p]) << "resumed page " << p;
  }
}

TEST_F(EpubSectionResumeTest, ShorterRebuildKeepsTheLargerExistingPartial) {
  auto builder = makeSection();
  const uint16_t watermark = buildAndSuspend(*builder, 25);
  ASSERT_GE(watermark, 25);
  builder.reset();
  const std::string keptBytes = readAll(sectionBinPath(*epub));

  auto resumed = makeSection();
  ASSERT_TRUE(resumed->loadSectionFile(makeSpec()));
  ASSERT_TRUE(resumed->startBuild(makeSpec()));
  ASSERT_TRUE(resumed->buildSomeMore(1));    // a few pages -- far short of the watermark
  ASSERT_EQ(resumed->pageCount, watermark);  // still pinned: the rebuild is behind it
  resumed->suspendBuild();

  // The bigger pre-existing partial won: same file, same watermark.
  EXPECT_TRUE(resumed->isPartial());
  EXPECT_EQ(resumed->pageCount, watermark);
  EXPECT_EQ(readAll(sectionBinPath(*epub)), keptBytes);
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));
}

TEST_F(EpubSectionResumeTest, DestructorSuspendsAnInProgressBuild) {
  {
    auto section = makeSection();
    ASSERT_TRUE(section->startBuild(makeSpec()));
    ASSERT_TRUE(section->buildSomeMore(6));
    ASSERT_GE(section->pageCount, 6);
    ASSERT_FALSE(section->isBuildComplete());
    // section goes out of scope mid-build -> ~Section() must persist it.
  }

  ASSERT_TRUE(fileExists(sectionBinPath(*epub)));
  const std::string bytes = readAll(sectionBinPath(*epub));
  EXPECT_EQ(readAt<uint8_t>(bytes, kOffVersion), kPartialVersion);

  auto reopened = makeSection();
  ASSERT_TRUE(reopened->loadSectionFile(makeSpec()));
  EXPECT_TRUE(reopened->isPartial());
  EXPECT_GE(reopened->pageCount, 6);
  EXPECT_NE(reopened->loadPage(0), nullptr);
}

TEST_F(EpubSectionResumeTest, SuspendWithNoPagesPersistsNothing) {
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(makeSpec()));
  section->suspendBuild();  // zero pages laid out -- nothing worth keeping

  EXPECT_FALSE(section->isBuilding());
  EXPECT_FALSE(section->isPartial());
  EXPECT_EQ(section->pageCount, 0);
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));
}

TEST_F(EpubSectionResumeTest, SuspendAfterCompletionIsANoOp) {
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));
  const std::string finalized = readAll(sectionBinPath(*epub));

  section->suspendBuild();  // no active build -> must not touch the file
  EXPECT_TRUE(section->isBuildComplete());
  EXPECT_FALSE(section->isPartial());
  EXPECT_EQ(readAll(sectionBinPath(*epub)), finalized);
}

}  // namespace
