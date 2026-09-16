#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Fb2ReaderMath.h"

// Percent <-> section mapping, page rescaling after re-pagination and
// progress.bin decoding for the FB2 reader (FR-105), extracted from
// Fb2ReaderActivity.

namespace {

struct Sizes {
  std::vector<size_t> cumulative;
  size_t bookSize;

  static size_t at(const void* ctx, int index) { return static_cast<const Sizes*>(ctx)->cumulative[index]; }
  fb2_reader::SectionSizes view() const { return {this, &Sizes::at, static_cast<int>(cumulative.size()), bookSize}; }
};

}  // namespace

// ---------------------------------------------------------------- percent helpers

TEST(Fb2Percent, ClampPercentBounds) {
  EXPECT_EQ(fb2_reader::clampPercent(-1), 0);
  EXPECT_EQ(fb2_reader::clampPercent(0), 0);
  EXPECT_EQ(fb2_reader::clampPercent(57), 57);
  EXPECT_EQ(fb2_reader::clampPercent(100), 100);
  EXPECT_EQ(fb2_reader::clampPercent(101), 100);
}

TEST(Fb2Percent, RoundedPercentRoundsHalfUpAndClamps) {
  EXPECT_EQ(fb2_reader::roundedPercent(0.0f), 0);
  EXPECT_EQ(fb2_reader::roundedPercent(12.49f), 12);
  EXPECT_EQ(fb2_reader::roundedPercent(12.5f), 13);
  EXPECT_EQ(fb2_reader::roundedPercent(99.6f), 100);
  EXPECT_EQ(fb2_reader::roundedPercent(140.0f), 100);
  EXPECT_EQ(fb2_reader::roundedPercent(-3.0f), 0);
}

// ---------------------------------------------------------------- percent -> section

TEST(Fb2PercentToSection, EmptyBookOrNoSectionsIsInvalid) {
  Sizes empty{{}, 0};
  EXPECT_FALSE(fb2_reader::percentToSection(50, empty.view()).valid);
  Sizes noSections{{}, 1000};
  EXPECT_FALSE(fb2_reader::percentToSection(50, noSections.view()).valid);
  // Sections present but the book has no bytes: the byte-size gate still wins,
  // and the caller must not move the reader.
  Sizes noBytes{{100, 200}, 0};
  EXPECT_FALSE(fb2_reader::percentToSection(50, noBytes.view()).valid);
  EXPECT_EQ(fb2_reader::percentToSection(50, noBytes.view()).sectionIndex, 0);
}

TEST(Fb2PercentToSection, ZeroPercentIsStartOfFirstSection) {
  Sizes s{{100, 300, 600}, 600};
  const auto t = fb2_reader::percentToSection(0, s.view());
  ASSERT_TRUE(t.valid);
  EXPECT_EQ(t.sectionIndex, 0);
  EXPECT_FLOAT_EQ(t.sectionProgress, 0.0f);
}

TEST(Fb2PercentToSection, HundredPercentIsLastByteOfLastSection) {
  Sizes s{{100, 300, 600}, 600};
  const auto t = fb2_reader::percentToSection(100, s.view());
  ASSERT_TRUE(t.valid);
  EXPECT_EQ(t.sectionIndex, 2);
  EXPECT_FLOAT_EQ(t.sectionProgress, 299.0f / 300.0f);
}

TEST(Fb2PercentToSection, MidPercentLandsByByteWeight) {
  Sizes s{{100, 300, 600}, 600};
  const auto t = fb2_reader::percentToSection(50, s.view());  // byte 300 -> end of section 1
  ASSERT_TRUE(t.valid);
  EXPECT_EQ(t.sectionIndex, 1);
  EXPECT_FLOAT_EQ(t.sectionProgress, 1.0f);
  const auto u = fb2_reader::percentToSection(60, s.view());  // byte 360 -> 60 into section 2 (300 bytes)
  EXPECT_EQ(u.sectionIndex, 2);
  EXPECT_FLOAT_EQ(u.sectionProgress, 0.2f);
}

TEST(Fb2PercentToSection, ByteMathAvoidsOverflowAndKeepsRemainder) {
  // 1234 bytes at 33%: (12*33) + (34*33/100) = 396 + 11 = 407 -> section 1 (301..800)
  Sizes s{{300, 800, 1234}, 1234};
  const auto t = fb2_reader::percentToSection(33, s.view());
  EXPECT_EQ(t.sectionIndex, 1);
  EXPECT_FLOAT_EQ(t.sectionProgress, 107.0f / 500.0f);
}

TEST(Fb2PercentToSection, OutOfRangePercentIsClamped) {
  Sizes s{{100, 300, 600}, 600};
  EXPECT_EQ(fb2_reader::percentToSection(-20, s.view()).sectionIndex, 0);
  const auto t = fb2_reader::percentToSection(250, s.view());
  EXPECT_EQ(t.sectionIndex, 2);
  EXPECT_FLOAT_EQ(t.sectionProgress, 299.0f / 300.0f);
}

TEST(Fb2PercentToSection, ZeroLengthSectionIsNeverSelected) {
  Sizes s{{100, 100, 600}, 600};                              // section 1 has no bytes
  const auto t = fb2_reader::percentToSection(10, s.view());  // byte 60 -> section 0
  EXPECT_EQ(t.sectionIndex, 0);
  EXPECT_FLOAT_EQ(t.sectionProgress, 0.6f);
  // Byte 96 still matches section 0 (<=); byte 102 skips the empty section and
  // measures from the end of section 1, which is also the end of section 0.
  EXPECT_EQ(fb2_reader::percentToSection(16, s.view()).sectionIndex, 0);
  const auto u = fb2_reader::percentToSection(17, s.view());
  EXPECT_EQ(u.sectionIndex, 2);
  EXPECT_FLOAT_EQ(u.sectionProgress, 2.0f / 500.0f);
}

TEST(Fb2PercentToSection, ZeroLengthLastSectionReportsZeroProgress) {
  // Target lands on the trailing empty section: sectionSize 0 -> progress 0.
  Sizes s{{100, 100}, 100};
  const auto t = fb2_reader::percentToSection(100, s.view());  // byte 99 -> section 0
  EXPECT_EQ(t.sectionIndex, 0);
  Sizes all{{0, 0}, 10};  // cumulative sizes never reach the target byte
  const auto u = fb2_reader::percentToSection(50, all.view());
  ASSERT_TRUE(u.valid);
  EXPECT_EQ(u.sectionIndex, 1);  // falls through to the last section
  EXPECT_FLOAT_EQ(u.sectionProgress, 0.0f);
}

TEST(Fb2PercentToSection, TargetBeyondCumulativeTotalFallsToLastSection) {
  // bookSize larger than the cumulative total: no section matches, so the
  // last one is used with progress measured from byte 0 (current behaviour).
  Sizes s{{100, 200}, 1000};
  const auto t = fb2_reader::percentToSection(50, s.view());  // byte 500 > 200
  ASSERT_TRUE(t.valid);
  EXPECT_EQ(t.sectionIndex, 1);
  EXPECT_FLOAT_EQ(t.sectionProgress, 1.0f);  // 500/200 clamped
}

// ---------------------------------------------------------------- rescale / jump

TEST(Fb2Rescale, KeepsRelativePositionAcrossPageCounts) {
  EXPECT_EQ(fb2_reader::rescalePage(5, 10, 20), 10);
  EXPECT_EQ(fb2_reader::rescalePage(9, 10, 5), 4);
  EXPECT_EQ(fb2_reader::rescalePage(0, 10, 20), 0);
}

TEST(Fb2Rescale, TruncatesTowardsZero) { EXPECT_EQ(fb2_reader::rescalePage(1, 3, 10), 3); }

TEST(Fb2Rescale, UnchangedWhenCountsMatchOrOldCountUnusable) {
  EXPECT_EQ(fb2_reader::rescalePage(7, 10, 10), 7);
  EXPECT_EQ(fb2_reader::rescalePage(7, 0, 10), 7);
  EXPECT_EQ(fb2_reader::rescalePage(7, -1, 10), 7);
}

TEST(Fb2PercentJump, MapsFractionToPageAndClampsToLast) {
  EXPECT_EQ(fb2_reader::percentJumpPage(0.0f, 10), 0);
  EXPECT_EQ(fb2_reader::percentJumpPage(0.5f, 10), 5);
  EXPECT_EQ(fb2_reader::percentJumpPage(0.99f, 10), 9);
  EXPECT_EQ(fb2_reader::percentJumpPage(1.0f, 10), 9);
  EXPECT_EQ(fb2_reader::percentJumpPage(0.5f, 0), 0);
}

// ---------------------------------------------------------------- progress decode

TEST(Fb2ProgressDecode, SixBytesCarrySectionPageAndPageCount) {
  const uint8_t data[6] = {0x02, 0x01, 0x34, 0x12, 0x10, 0x00};
  const auto p = fb2_reader::decodeProgress(data, 6);
  EXPECT_TRUE(p.valid);
  EXPECT_EQ(p.sectionIndex, 0x0102);
  EXPECT_EQ(p.page, 0x1234);
  EXPECT_TRUE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 16);
}

TEST(Fb2ProgressDecode, FourBytesAcceptedWithoutPageCount) {
  const uint8_t data[4] = {3, 0, 7, 0};
  const auto p = fb2_reader::decodeProgress(data, 4);
  EXPECT_TRUE(p.valid);
  EXPECT_EQ(p.sectionIndex, 3);
  EXPECT_EQ(p.page, 7);
  EXPECT_FALSE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 0);
}

TEST(Fb2ProgressDecode, OtherSizesAreRejected) {
  const uint8_t data[6] = {1, 0, 1, 0, 1, 0};
  for (const int size : {0, 1, 2, 3, 5}) {
    const auto p = fb2_reader::decodeProgress(data, size);
    EXPECT_FALSE(p.valid) << size;
    EXPECT_FALSE(p.hasPageCount) << size;
  }
  EXPECT_FALSE(fb2_reader::decodeProgress(data, -1).valid);
}

TEST(Fb2ProgressDecode, ZeroPageCountStillCountsAsPresent) {
  const uint8_t data[6] = {0, 0, 0, 0, 0, 0};
  const auto p = fb2_reader::decodeProgress(data, 6);
  EXPECT_TRUE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 0);
}

TEST(Fb2PercentToSection, BookSmallerThanHundredBytesUsesRemainderMath) {
  // bookSize/100 == 0, so the whole target comes from the remainder term:
  // (0 * 50) + (50 * 50 / 100) = 25 -> section 1 (21..50).
  Sizes s{{20, 50}, 50};
  const auto t = fb2_reader::percentToSection(50, s.view());
  ASSERT_TRUE(t.valid);
  EXPECT_EQ(t.sectionIndex, 1);
  EXPECT_FLOAT_EQ(t.sectionProgress, 5.0f / 30.0f);
}

TEST(Fb2PercentToSection, SingleSectionAlwaysWins) {
  Sizes s{{400}, 400};
  EXPECT_EQ(fb2_reader::percentToSection(0, s.view()).sectionIndex, 0);
  EXPECT_EQ(fb2_reader::percentToSection(50, s.view()).sectionIndex, 0);
  const auto t = fb2_reader::percentToSection(100, s.view());
  EXPECT_EQ(t.sectionIndex, 0);
  EXPECT_FLOAT_EQ(t.sectionProgress, 399.0f / 400.0f);
}

TEST(Fb2PercentToSection, ProgressIsNeverOutsideZeroToOne) {
  Sizes s{{10, 20, 30}, 3000};  // cumulative far smaller than the book size
  for (int p = 0; p <= 100; p += 5) {
    const auto t = fb2_reader::percentToSection(p, s.view());
    ASSERT_TRUE(t.valid) << p;
    EXPECT_GE(t.sectionProgress, 0.0f) << p;
    EXPECT_LE(t.sectionProgress, 1.0f) << p;
    EXPECT_GE(t.sectionIndex, 0) << p;
    EXPECT_LT(t.sectionIndex, 3) << p;
  }
}

TEST(Fb2Rescale, EmptyNewSectionCollapsesToPageZero) {
  EXPECT_EQ(fb2_reader::rescalePage(5, 10, 0), 0);
  EXPECT_EQ(fb2_reader::rescalePage(0, 10, 0), 0);
}

TEST(Fb2ProgressDecode, SixteenBitFieldsAreUnsigned) {
  const uint8_t data[6] = {0xFF, 0xFF, 0xFE, 0xFF, 0xFD, 0xFF};
  const auto p = fb2_reader::decodeProgress(data, 6);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.sectionIndex, 0xFFFF);
  EXPECT_EQ(p.page, 0xFFFE);
  EXPECT_EQ(p.pageCount, 0xFFFD);
}
