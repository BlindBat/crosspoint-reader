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

// Every callback is an SD record read on device, so the jump must binary-search the
// running totals instead of scanning them.
TEST(Fb2PercentToSection, LargeBookIsSearchedNotScanned) {
  struct Counted {
    std::vector<size_t> cumulative;
    mutable int calls = 0;
    static size_t at(const void* ctx, int index) {
      const auto* self = static_cast<const Counted*>(ctx);
      self->calls++;
      return self->cumulative[index];
    }
  };
  Counted c;
  constexpr int kSections = 4000;
  for (int i = 0; i < kSections; i++) c.cumulative.push_back(static_cast<size_t>(i + 1) * 10);
  const fb2_reader::SectionSizes sizes{&c, &Counted::at, kSections, c.cumulative.back()};
  for (const int percent : {0, 1, 37, 50, 99, 100}) {
    c.calls = 0;
    const auto t = fb2_reader::percentToSection(percent, sizes);
    ASSERT_TRUE(t.valid);
    // The expected chapter, computed independently: the first whose total reaches the target.
    const size_t target = percent >= 100 ? sizes.bookSize - 1 : sizes.bookSize / 100 * percent;
    EXPECT_EQ(t.sectionIndex, static_cast<int>(target / 10 - (target % 10 == 0 && target > 0 ? 1 : 0)))
        << percent << "%";
    EXPECT_LE(c.calls, 14) << percent << "% took " << c.calls << " lookups";  // ceil(log2 4000) + 2
  }
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

TEST(Fb2ProgressDecode, MarkedEightBytesCarryChapterPageAndPageCount) {
  const uint8_t data[8] = {0x02, 0x01, 0x34, 0x12, 0x10, 0x00, 0x02, 0xFB};
  const auto p = fb2_reader::decodeProgress(data, 8);
  EXPECT_TRUE(p.valid);
  EXPECT_FALSE(p.legacyOrdinal);
  EXPECT_EQ(p.sectionIndex, 0x0102);
  EXPECT_EQ(p.page, 0x1234);
  EXPECT_TRUE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 16);
}

TEST(Fb2ProgressDecode, EightBytesWithTheWrongMarkerAreRejected) {
  uint8_t data[8] = {1, 0, 2, 0, 3, 0, 0x02, 0xFB};
  ASSERT_TRUE(fb2_reader::decodeProgress(data, 8).valid);
  data[7] = 0xFA;  // anything but the marker
  EXPECT_FALSE(fb2_reader::decodeProgress(data, 8).valid);
  data[6] = 0x00;
  data[7] = 0x00;
  EXPECT_FALSE(fb2_reader::decodeProgress(data, 8).valid);
}

// A payload with no marker was written before every section became a chapter:
// its first field is a top-level ordinal, and its page number indexed a chapter
// that no longer exists at that size, so the page restarts at 0.
TEST(Fb2ProgressDecode, SixBytesDecodeAsALegacyTopLevelOrdinal) {
  const uint8_t data[6] = {0x02, 0x01, 0x34, 0x12, 0x10, 0x00};
  const auto p = fb2_reader::decodeProgress(data, 6);
  EXPECT_TRUE(p.valid);
  EXPECT_TRUE(p.legacyOrdinal);
  EXPECT_EQ(p.sectionIndex, 0x0102);
  EXPECT_EQ(p.page, 0);
  EXPECT_FALSE(p.hasPageCount) << "a legacy page count must not seed the save guard";
}

TEST(Fb2ProgressDecode, FourBytesDecodeAsALegacyTopLevelOrdinal) {
  const uint8_t data[4] = {3, 0, 7, 0};
  const auto p = fb2_reader::decodeProgress(data, 4);
  EXPECT_TRUE(p.valid);
  EXPECT_TRUE(p.legacyOrdinal);
  EXPECT_EQ(p.sectionIndex, 3);
  EXPECT_EQ(p.page, 0);
  EXPECT_FALSE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 0);
}

TEST(Fb2ProgressDecode, OtherSizesAreRejected) {
  const uint8_t data[8] = {1, 0, 1, 0, 1, 0, 0x02, 0xFB};
  for (const int size : {0, 1, 2, 3, 5, 7, 9}) {
    const auto p = fb2_reader::decodeProgress(data, size);
    EXPECT_FALSE(p.valid) << size;
    EXPECT_FALSE(p.hasPageCount) << size;
  }
  EXPECT_FALSE(fb2_reader::decodeProgress(data, -1).valid);
}

TEST(Fb2ProgressDecode, ZeroPageCountStillCountsAsPresent) {
  const uint8_t data[8] = {0, 0, 0, 0, 0, 0, 0x02, 0xFB};
  const auto p = fb2_reader::decodeProgress(data, 8);
  EXPECT_TRUE(p.hasPageCount);
  EXPECT_EQ(p.pageCount, 0);
}

// Writing always emits the current form, so a book that was read once migrates.
TEST(Fb2ProgressEncode, EncodeRoundTripsThroughDecodeWithTheMarker) {
  uint8_t data[fb2_reader::PROGRESS_SIZE] = {};
  EXPECT_EQ(fb2_reader::encodeProgress(data, 300, 12, 40), fb2_reader::PROGRESS_SIZE);
  EXPECT_EQ(data[6], 0x02);
  EXPECT_EQ(data[7], 0xFB);

  const auto p = fb2_reader::decodeProgress(data, fb2_reader::PROGRESS_SIZE);
  EXPECT_TRUE(p.valid);
  EXPECT_FALSE(p.legacyOrdinal);
  EXPECT_EQ(p.sectionIndex, 300);
  EXPECT_EQ(p.page, 12);
  EXPECT_EQ(p.pageCount, 40);
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
  const uint8_t data[8] = {0xFF, 0xFF, 0xFE, 0xFF, 0xFD, 0xFF, 0x02, 0xFB};
  const auto p = fb2_reader::decodeProgress(data, 8);
  ASSERT_TRUE(p.valid);
  EXPECT_EQ(p.sectionIndex, 0xFFFF);
  EXPECT_EQ(p.page, 0xFFFE);
  EXPECT_EQ(p.pageCount, 0xFFFD);
}

// ---------------------------------------------------------------------------
// prefetchTarget: which chapter the reader lays out ahead of the one being read.
// ---------------------------------------------------------------------------

using fb2_reader::NO_PREFETCH_TARGET;
using fb2_reader::prefetchTarget;

// TP1: always the NEXT chapter, and never past the end of the book.
// Catches: an off-by-one that targets the chapter being read, or that runs on
// the last chapter (there is nothing after it to prepare).
TEST(Fb2PrefetchTarget, TargetsTheNextChapterAndStopsAtTheEnd) {
  EXPECT_EQ(prefetchTarget(0, 5, false, false, NO_PREFETCH_TARGET), 1);
  EXPECT_EQ(prefetchTarget(3, 5, false, false, NO_PREFETCH_TARGET), 4);
  // Last chapter: nothing follows.
  EXPECT_EQ(prefetchTarget(4, 5, false, false, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
  // One-chapter book.
  EXPECT_EQ(prefetchTarget(0, 1, false, false, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
  // Cover and end-of-book are not reading positions.
  EXPECT_EQ(prefetchTarget(0, 5, true, false, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
  EXPECT_EQ(prefetchTarget(5, 5, false, true, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
  // Out-of-range current index (end-of-book sets it past the last chapter).
  EXPECT_EQ(prefetchTarget(5, 5, false, false, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
  EXPECT_EQ(prefetchTarget(-1, 5, false, false, NO_PREFETCH_TARGET), NO_PREFETCH_TARGET);
}

// TP2: a chapter already prepared is not prepared again.
// Catches: dropping the already-prepared check, so every re-entry to a chapter
// rebuilds a section file that is already on the card.
TEST(Fb2PrefetchTarget, AlreadyPreparedChapterIsNotRepeated) {
  EXPECT_EQ(prefetchTarget(2, 5, false, false, 3), NO_PREFETCH_TARGET);
  // A different chapter being prepared does not block this one.
  EXPECT_EQ(prefetchTarget(2, 5, false, false, 1), 3);
  EXPECT_EQ(prefetchTarget(2, 5, false, false, 4), 3);
}
