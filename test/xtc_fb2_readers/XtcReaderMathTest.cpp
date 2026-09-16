#include <gtest/gtest.h>

#include <vector>

#include "XtcReaderMath.h"

// Status-bar geometry, chapter-relative numbering and progress clamping for
// the XTC reader (FR-100, FR-101), extracted from XtcReaderActivity and
// XtcReaderChapterSelectionActivity.

namespace {

std::vector<xtc::ChapterInfo> chapters() { return {{"One", 0, 4}, {"Two", 5, 9}, {"", 10, 12}}; }

}  // namespace

// ---------------------------------------------------------------- status bar layout

TEST(XtcStatusBarLayout, BottomBarClearsBandAboveBottomMargin) {
  const auto l = xtc_reader::statusBarLayout(true, 800, 20, 3, 5);
  EXPECT_EQ(l.clearY, 800 - 5 - 20 - 4);
  EXPECT_EQ(l.clearHeight, 800 - 5 - l.clearY);
  EXPECT_EQ(l.paddingBottom, 0);
}

TEST(XtcStatusBarLayout, BottomBarClampsClearYAtZero) {
  const auto l = xtc_reader::statusBarLayout(true, 100, 200, 0, 0);
  EXPECT_EQ(l.clearY, 0);
  EXPECT_EQ(l.clearHeight, 100);
}

TEST(XtcStatusBarLayout, TopBarStartsAtTopMarginAndPadsTheRest) {
  const auto l = xtc_reader::statusBarLayout(false, 800, 20, 3, 5);
  EXPECT_EQ(l.clearY, 3);
  EXPECT_EQ(l.clearHeight, 24);
  EXPECT_EQ(l.paddingBottom, 800 - 20 - 5 - 3 - 4);
}

TEST(XtcStatusBarLayout, TopBarClearHeightIgnoresScreenHeight) {
  // A bar taller than the screen still clears its own band and hands the
  // caller a negative padding; the theme, not this helper, decides what to do.
  const auto l = xtc_reader::statusBarLayout(false, 10, 20, 0, 0);
  EXPECT_EQ(l.clearY, 0);
  EXPECT_EQ(l.clearHeight, 24);
  EXPECT_EQ(l.paddingBottom, -14);
}

// ---------------------------------------------------------------- progress percent

TEST(XtcProgressPercent, OneBasedPageOverCount) {
  EXPECT_FLOAT_EQ(xtc_reader::progressPercent(0, 4), 25.0f);
  EXPECT_FLOAT_EQ(xtc_reader::progressPercent(3, 4), 100.0f);
  EXPECT_FLOAT_EQ(xtc_reader::progressPercent(0, 3), 100.0f / 3.0f);
}

TEST(XtcProgressPercent, EmptyBookIsZero) { EXPECT_FLOAT_EQ(xtc_reader::progressPercent(7, 0), 0.0f); }

TEST(XtcProgressPercent, EndOfBookPageOvershootsHundred) {
  // currentPage == pageCount is the end-of-book screen; the bar is not drawn there.
  EXPECT_FLOAT_EQ(xtc_reader::progressPercent(4, 4), 125.0f);
}

// ---------------------------------------------------------------- chapters

TEST(XtcChapters, FindChapterUsesInclusiveBounds) {
  const auto c = chapters();
  EXPECT_EQ(xtc_reader::findChapter(c, 0), &c[0]);
  EXPECT_EQ(xtc_reader::findChapter(c, 4), &c[0]);
  EXPECT_EQ(xtc_reader::findChapter(c, 5), &c[1]);
  EXPECT_EQ(xtc_reader::findChapter(c, 12), &c[2]);
  EXPECT_EQ(xtc_reader::findChapter(c, 13), nullptr);
}

TEST(XtcChapters, FirstMatchWinsOnOverlap) {
  const std::vector<xtc::ChapterInfo> c = {{"A", 0, 9}, {"B", 5, 9}};
  EXPECT_EQ(xtc_reader::findChapter(c, 7), &c[0]);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(c, 7), 0);
}

TEST(XtcChapters, IndexForPageFallsBackToFirstRow) {
  const auto c = chapters();
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(c, 7), 1);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(c, 12), 2);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(c, 99), 0);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage({}, 0), 0);
}

TEST(XtcChapters, PagePositionIsChapterRelativeInsideAChapter) {
  const auto c = chapters();
  const auto p = xtc_reader::pagePosition(c, 7, 13);
  EXPECT_EQ(p.chapter, &c[1]);
  EXPECT_EQ(p.currentPage, 3);
  EXPECT_EQ(p.pageCount, 5);
}

TEST(XtcChapters, PagePositionIsBookRelativeOutsideChapters) {
  const auto c = chapters();
  const auto p = xtc_reader::pagePosition(c, 20, 30);
  EXPECT_EQ(p.chapter, nullptr);
  EXPECT_EQ(p.currentPage, 21);
  EXPECT_EQ(p.pageCount, 30);
}

TEST(XtcChapters, PagePositionIsBookRelativeWithoutChapters) {
  const auto p = xtc_reader::pagePosition({}, 0, 13);
  EXPECT_EQ(p.chapter, nullptr);
  EXPECT_EQ(p.currentPage, 1);
  EXPECT_EQ(p.pageCount, 13);
}

TEST(XtcChapters, InvertedChapterRangeNeverMatches) {
  const std::vector<xtc::ChapterInfo> c = {{"Bad", 9, 5}};
  EXPECT_EQ(xtc_reader::findChapter(c, 7), nullptr);
  EXPECT_EQ(xtc_reader::pagePosition(c, 7, 10).chapter, nullptr);
}

// ---------------------------------------------------------------- progress clamp / decode

TEST(XtcProgressClamp, PastEndSnapsToLastPage) {
  EXPECT_EQ(xtc_reader::clampPage(9, 10), 9u);
  EXPECT_EQ(xtc_reader::clampPage(10, 10), 9u);
  EXPECT_EQ(xtc_reader::clampPage(0xFFFFFFFFu, 10), 9u);
}

TEST(XtcProgressClamp, EmptyBookLeavesPageUntouched) { EXPECT_EQ(xtc_reader::clampPage(5, 0), 5u); }

TEST(XtcProgressDecode, ReadsLittleEndianU32) {
  const uint8_t data[4] = {0x04, 0x03, 0x02, 0x01};
  EXPECT_EQ(xtc_reader::decodeProgress(data, 0), 0x01020304u);
}

TEST(XtcProgressDecode, HighBitByteDecodesWithoutSignExtension) {
  const uint8_t data[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_EQ(xtc_reader::decodeProgress(data, 0), 0xFFFFFFFFu);
}

TEST(XtcProgressDecode, ClampsToLastPage) {
  const uint8_t data[4] = {0x2C, 0x01, 0, 0};  // 300
  EXPECT_EQ(xtc_reader::decodeProgress(data, 250), 249u);
  EXPECT_EQ(xtc_reader::decodeProgress(data, 301), 300u);
}

// ---------------------------------------------------------------- skip

TEST(XtcSkip, ClampsToStartAndEndOfBookScreen) {
  EXPECT_EQ(xtc_reader::skipTarget(2, -10, 20), 0u);
  EXPECT_EQ(xtc_reader::skipTarget(18, 10, 20), 20u);
  EXPECT_EQ(xtc_reader::skipTarget(5, 10, 20), 15u);
  EXPECT_EQ(xtc_reader::skipTarget(5, -3, 20), 2u);
  EXPECT_EQ(xtc_reader::skipTarget(5, 0, 20), 5u);
}

TEST(XtcStatusBarLayout, ZeroHeightBarStillClearsThePaddingBand) {
  const auto top = xtc_reader::statusBarLayout(false, 800, 0, 10, 10);
  EXPECT_EQ(top.clearY, 10);
  EXPECT_EQ(top.clearHeight, 4);
  const auto bottom = xtc_reader::statusBarLayout(true, 800, 0, 10, 10);
  EXPECT_EQ(bottom.clearY, 786);
  EXPECT_EQ(bottom.clearHeight, 4);
}

TEST(XtcChapters, SingleChapterCoveringTheWholeBook) {
  const std::vector<xtc::ChapterInfo> c = {{"All", 0, 9}};
  const auto p = xtc_reader::pagePosition(c, 0, 10);
  EXPECT_EQ(p.chapter, &c[0]);
  EXPECT_EQ(p.currentPage, 1);
  EXPECT_EQ(p.pageCount, 10);
  EXPECT_EQ(xtc_reader::findChapterIndexForPage(c, 9), 0);
}

TEST(XtcChapters, SinglePageChapterCountsAsOnePage) {
  const std::vector<xtc::ChapterInfo> c = {{"Solo", 4, 4}};
  const auto p = xtc_reader::pagePosition(c, 4, 10);
  EXPECT_EQ(p.currentPage, 1);
  EXPECT_EQ(p.pageCount, 1);
}

TEST(XtcSkip, EmptyBookStaysOnPageZero) {
  EXPECT_EQ(xtc_reader::skipTarget(0, 10, 0), 0u);
  EXPECT_EQ(xtc_reader::skipTarget(0, -1, 0), 0u);
}
