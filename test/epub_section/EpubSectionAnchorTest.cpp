// EPUB Section pagination engine: anchor resolution and the lookup tables
// (anchor map, paragraph LUT, li LUT, visible-text-offset LUT), during an
// active build and from the finalized file.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "EpubSectionTestSupport.h"

namespace {

using namespace sectest;

class EpubSectionAnchorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    epub = makeEpub(tmp, buildChapterHtml(kParagraphs, kWordsPerParagraph));
  }

  std::unique_ptr<Section> makeSection() { return std::make_unique<Section>(epub, 0, renderer); }

  static constexpr int kParagraphs = 40;
  static constexpr int kWordsPerParagraph = 30;

  TempDir tmp;
  GfxRenderer renderer;
  std::shared_ptr<Epub> epub;
};

TEST_F(EpubSectionAnchorTest, AnchorsResolveFromFinalizedFile) {
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));

  // First paragraph's anchor is on page 0.
  ASSERT_TRUE(section->findAnchor("pp0").has_value());
  EXPECT_EQ(*section->findAnchor("pp0"), 0);

  // Anchor pages are in range and non-decreasing in document order.
  uint16_t last = 0;
  for (int i = 0; i < kParagraphs; i++) {
    const auto page = section->findAnchor("pp" + std::to_string(i));
    ASSERT_TRUE(page.has_value()) << "anchor pp" << i << " missing";
    ASSERT_LT(*page, section->pageCount);
    EXPECT_GE(*page, last);
    last = *page;
  }
  EXPECT_GT(last, 0);  // later anchors really land on later pages

  // The anchor records the page that was current when the anchored element
  // opened; when the remaining space on that page cannot fit the paragraph's
  // first line, the words start on the next page. So the paragraph's first
  // word lives on the anchor page or the one after it -- never elsewhere.
  const auto midPage = section->findAnchor("pp20");
  ASSERT_TRUE(midPage.has_value());
  const auto pageHasWord = [&section](const uint16_t p, const char* word) {
    const auto page = section->loadPage(p);
    if (!page) return false;
    const auto words = pageWords(*page);
    return std::find(words.begin(), words.end(), word) != words.end();
  };
  EXPECT_TRUE(pageHasWord(*midPage, "p20x0") || pageHasWord(*midPage + 1, "p20x0"))
      << "p20x0 not on anchor page " << *midPage << " or its successor";
  EXPECT_FALSE(pageHasWord(*midPage > 0 ? *midPage - 1 : 0, "p20x0") && *midPage > 0)
      << "p20x0 already on the page before the anchor";

  // With no build running, findAnchor and the raw on-disk lookup agree.
  EXPECT_EQ(section->findAnchor("pp20"), section->getPageForAnchor("pp20"));
}

TEST_F(EpubSectionAnchorTest, MissingAnchorIsNullopt) {
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));
  EXPECT_EQ(section->findAnchor("no-such-anchor"), std::nullopt);
  EXPECT_EQ(section->getPageForAnchor("no-such-anchor"), std::nullopt);
  EXPECT_EQ(section->findAnchorDuringBuild("pp0"), std::nullopt);  // no build running
}

TEST_F(EpubSectionAnchorTest, FindAnchorDuringBuildSeesOnlyParsedAnchors) {
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(makeSpec()));
  ASSERT_TRUE(section->buildSomeMore(2));
  ASSERT_FALSE(section->isBuildComplete());

  // The chapter head has been parsed; the tail has not.
  ASSERT_TRUE(section->findAnchorDuringBuild("pp0").has_value());
  EXPECT_EQ(*section->findAnchorDuringBuild("pp0"), 0);
  EXPECT_EQ(section->findAnchorDuringBuild("pp39"), std::nullopt);
  // findAnchor: build first, then disk -- no disk file yet, so still nullopt.
  EXPECT_EQ(section->findAnchor("pp39"), std::nullopt);
  EXPECT_EQ(section->findAnchor("pp0"), section->findAnchorDuringBuild("pp0"));

  int guard = 0;
  while (!section->isBuildComplete()) {
    ASSERT_TRUE(section->buildSomeMore(4));
    ASSERT_LT(guard++, 10000);
  }
  // Finalized: the last anchor is resolvable from disk.
  const auto lastAnchor = section->findAnchor("pp39");
  ASSERT_TRUE(lastAnchor.has_value());
  EXPECT_GT(*lastAnchor, 0);
  ASSERT_LT(*lastAnchor, section->pageCount);
}

TEST_F(EpubSectionAnchorTest, TocAnchorForcesPageBreakAtChapterBoundary) {
  // Register pp10 as a TOC chapter boundary within this spine: the parser must
  // start a fresh page there, so the anchor's page begins with p10x0.
  epub->toc.push_back({"Chapter", kSpineHref, "pp10", 0});
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));

  const auto anchorPage = section->findAnchor("pp10");
  ASSERT_TRUE(anchorPage.has_value());
  EXPECT_GT(*anchorPage, 0);
  const auto page = section->loadPage(*anchorPage);
  ASSERT_NE(page, nullptr);
  const auto words = pageWords(*page);
  ASSERT_FALSE(words.empty());
  EXPECT_EQ(words.front(), "p10x0");
}

TEST_F(EpubSectionAnchorTest, ParagraphLutMapsPagesAndParagraphsBothWays) {
  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));

  // XPath paragraph indices are 1-based: p[1] starts on page 0.
  ASSERT_TRUE(section->getPageForParagraphIndex(1).has_value());
  EXPECT_EQ(*section->getPageForParagraphIndex(1), 0);

  uint16_t lastParagraph = 0;
  for (uint16_t p = 0; p < section->pageCount; p++) {
    const auto paragraph = section->getParagraphIndexForPage(p);
    ASSERT_TRUE(paragraph.has_value()) << "page " << p;
    EXPECT_GE(*paragraph, lastParagraph) << "paragraph LUT not monotonic at page " << p;
    lastParagraph = *paragraph;

    // Reverse lookup lands on the first page carrying that paragraph index.
    const auto firstPage = section->getPageForParagraphIndex(*paragraph);
    ASSERT_TRUE(firstPage.has_value());
    EXPECT_LE(*firstPage, p);
    EXPECT_EQ(section->getParagraphIndexForPage(*firstPage), paragraph);
  }
  // The last page has seen all paragraphs open.
  EXPECT_EQ(lastParagraph, kParagraphs);

  EXPECT_EQ(section->getParagraphIndexForPage(section->pageCount), std::nullopt);
}

TEST_F(EpubSectionAnchorTest, ListItemLutLocatesListContent) {
  // 2 lead paragraphs, then 30 list items of 12 words each ("q<k>x<j>"), so
  // the list spans many pages and li lookups have distinct targets.
  std::ostringstream html;
  html << "<?xml version=\"1.0\"?><html><body><p>intro one</p><p>intro two</p><ul>";
  for (int k = 0; k < 30; k++) {
    html << "<li>";
    for (int j = 0; j < 12; j++) {
      if (j > 0) html << ' ';
      html << 'q' << k << 'x' << j;
    }
    html << "</li>";
  }
  html << "</ul></body></html>";

  auto listEpub = std::make_shared<Epub>();
  listEpub->cachePath = tmp.path() + "/epub_list";
  listEpub->spine.push_back({kSpineHref});
  listEpub->items[kSpineHref] = html.str();

  Section section(listEpub, 0, renderer);
  ASSERT_TRUE(section.createSectionFile(makeSpec()));
  ASSERT_GT(section.pageCount, 5);

  // Running li indices are 1-based; the first list item is reachable from the
  // chapter head.
  const auto firstLi = section.getPageForListItemIndex(1);
  ASSERT_TRUE(firstLi.has_value());
  ASSERT_LT(*firstLi, section.pageCount);

  // A mid-list item resolves to the page where it starts: that page (or the
  // next, when the item opens at the very bottom of a page) carries its first
  // word. Lookups are monotone in the running index.
  const auto li10 = section.getPageForListItemIndex(10);
  const auto li25 = section.getPageForListItemIndex(25);
  ASSERT_TRUE(li10.has_value());
  ASSERT_TRUE(li25.has_value());
  EXPECT_LE(*firstLi, *li10);
  EXPECT_LT(*li10, *li25);

  const auto pageHasWord = [&section](const uint16_t p, const std::string& word) {
    if (p >= section.pageCount) return false;
    const auto page = section.loadPage(p);
    if (!page) return false;
    const auto words = pageWords(*page);
    return std::find(words.begin(), words.end(), word) != words.end();
  };
  EXPECT_TRUE(pageHasWord(*li10, "q9x0") || pageHasWord(*li10 + 1, "q9x0"));
  EXPECT_TRUE(pageHasWord(*li25, "q24x0") || pageHasWord(*li25 + 1, "q24x0"));
}

TEST_F(EpubSectionAnchorTest, VisibleTextOffsetsRoundTripDuringAndAfterBuild) {
  auto section = makeSection();
  ASSERT_TRUE(section->startBuild(makeSpec()));
  ASSERT_TRUE(section->buildSomeMore(4));
  ASSERT_FALSE(section->isBuildComplete());

  // During the build, offsets come from the in-RAM LUT and are stamped onto
  // loaded pages.
  ASSERT_GE(section->pageCount, 2);
  const auto offset0 = section->getVisibleTextOffsetForPage(0);
  const auto offset1 = section->getVisibleTextOffsetForPage(1);
  ASSERT_TRUE(offset0.has_value());
  ASSERT_TRUE(offset1.has_value());
  EXPECT_EQ(*offset0, 0u);
  EXPECT_GT(*offset1, *offset0);
  const auto duringPage = section->loadPage(1);
  ASSERT_NE(duringPage, nullptr);
  EXPECT_EQ(duringPage->visibleTextOffset, *offset1);
  EXPECT_TRUE(section->buildReachedVisibleTextOffset(*offset1));
  EXPECT_FALSE(section->buildReachedVisibleTextOffset(1u << 30));

  int guard = 0;
  while (!section->isBuildComplete()) {
    ASSERT_TRUE(section->buildSomeMore(4));
    ASSERT_LT(guard++, 10000);
  }

  // Finalized: strictly increasing page-start offsets (text-only chapter), the
  // on-disk LUT matches what loadPage stamps, and offset->page inverts
  // page->offset.
  uint32_t previous = 0;
  for (uint16_t p = 0; p < section->pageCount; p++) {
    const auto offset = section->getVisibleTextOffsetForPage(p);
    ASSERT_TRUE(offset.has_value()) << "page " << p;
    if (p > 0) EXPECT_GT(*offset, previous) << "page " << p;
    previous = *offset;

    const auto page = section->loadPage(p);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->visibleTextOffset, *offset);

    EXPECT_EQ(section->getPageForVisibleTextOffset(*offset), p);
    EXPECT_EQ(section->getPageForVisibleTextOffset(*offset, /*preferFirstAtOffset=*/true), p);
    // An offset just inside the page still maps to it (next page starts later).
    EXPECT_EQ(section->getPageForVisibleTextOffset(*offset + 1), p);
  }
  // Past-the-end offsets clamp to the last page of a finalized section.
  EXPECT_EQ(section->getPageForVisibleTextOffset(previous + 100000), static_cast<uint16_t>(section->pageCount - 1));
  // With no active build, nothing is "pending": callers resolve from disk.
  EXPECT_FALSE(section->buildReachedVisibleTextOffset(0));

  EXPECT_EQ(section->getVisibleTextOffsetForPage(section->pageCount), std::nullopt);
}

}  // namespace
