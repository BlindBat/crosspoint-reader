#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "Fb2ReaderMath.h"
#include "ReaderProgressGuard.h"
#include "XtcFixture.h"
#include "XtcReaderMath.h"

// The production write guard (src/activities/reader/ReaderProgressGuard.cpp)
// driven in the XTC and FB2 shapes, with the stdio HalStorage stub counting
// every file opened for writing. renderBook() calls saveProgress() on every
// repaint, so without the guard a repaint of the current page rewrites
// progress.bin -- a temp write plus a remove and a rename on the SD card.

namespace {

// Exactly what XtcReaderActivity::saveProgress() encodes: LE u32 page.
bool saveXtcProgress(ReaderProgressGuard& guard, const std::string& cachePath, const uint32_t page) {
  uint8_t data[4];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = (page >> 16) & 0xFF;
  data[3] = (page >> 24) & 0xFF;
  return guard.save(cachePath, static_cast<int>(page), data, sizeof(data));
}

// Exactly what Fb2ReaderActivity::saveProgress() encodes: LE u16 section,
// page and page count.
bool saveFb2Progress(ReaderProgressGuard& guard, const std::string& cachePath, const int section, const int page,
                     const int pageCount) {
  uint8_t data[6];
  data[0] = section & 0xFF;
  data[1] = (section >> 8) & 0xFF;
  data[2] = page & 0xFF;
  data[3] = (page >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  return guard.save(cachePath, section, page, pageCount, data, sizeof(data));
}

class ProgressGuardTest : public ::testing::Test {
 protected:
  xtcfix::TempDir dir;
  std::string cachePath;

  void SetUp() override {
    ASSERT_FALSE(dir.path().empty());
    cachePath = dir.path() + "/cache";
    ASSERT_TRUE(Storage.mkdir(cachePath.c_str()));
    Storage.resetCounters();
  }

  void TearDown() override { Storage.resetCounters(); }

  std::vector<uint8_t> savedBytes() const { return xtcfix::readFile(cachePath + "/progress.bin"); }
};

TEST_F(ProgressGuardTest, XtcRepaintOfTheSamePageWritesOnce) {
  ReaderProgressGuard guard;
  EXPECT_TRUE(saveXtcProgress(guard, cachePath, 12));
  EXPECT_EQ(Storage.writeCount, 1);
  EXPECT_EQ(Storage.writtenPaths[0], cachePath + "/progress.bin.tmp");

  for (int repaint = 0; repaint < 4; repaint++) {
    EXPECT_TRUE(saveXtcProgress(guard, cachePath, 12));
  }
  EXPECT_EQ(Storage.writeCount, 1);

  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(xtc_reader::decodeProgress(bytes.data(), 100), 12u);
}

TEST_F(ProgressGuardTest, XtcPageTurnsStillWriteAndTheLastOneWins) {
  ReaderProgressGuard guard;
  for (uint32_t page = 0; page < 3; page++) {
    EXPECT_TRUE(saveXtcProgress(guard, cachePath, page));
    EXPECT_TRUE(saveXtcProgress(guard, cachePath, page));  // repaint
  }
  EXPECT_EQ(Storage.writeCount, 3);

  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(xtc_reader::decodeProgress(bytes.data(), 100), 2u);
}

TEST_F(ProgressGuardTest, XtcMarkSavedAfterLoadSuppressesTheOpeningRewrite) {
  // loadProgress() decoded page 5 from the file, so the file already says 5.
  ReaderProgressGuard seeded;
  ASSERT_TRUE(saveXtcProgress(seeded, cachePath, 5));
  Storage.resetCounters();

  ReaderProgressGuard guard;
  guard.markSaved(5);
  EXPECT_TRUE(saveXtcProgress(guard, cachePath, 5));
  EXPECT_EQ(Storage.writeCount, 0);

  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(xtc_reader::decodeProgress(bytes.data(), 100), 5u);
}

TEST_F(ProgressGuardTest, Fb2SkipsOnlyWhenSectionPageAndCountAllMatch) {
  ReaderProgressGuard guard;
  EXPECT_TRUE(saveFb2Progress(guard, cachePath, 3, 4, 20));
  EXPECT_EQ(Storage.writeCount, 1);

  EXPECT_TRUE(saveFb2Progress(guard, cachePath, 3, 4, 20));
  EXPECT_EQ(Storage.writeCount, 1);

  // Same page, new section.
  EXPECT_TRUE(saveFb2Progress(guard, cachePath, 4, 4, 20));
  EXPECT_EQ(Storage.writeCount, 2);

  // Same section and page, re-paginated to a different page count: the stored
  // count is what rescales the page on the next open, so it must be written.
  EXPECT_TRUE(saveFb2Progress(guard, cachePath, 4, 4, 31));
  EXPECT_EQ(Storage.writeCount, 3);

  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 6u);
  const auto decoded = fb2_reader::decodeProgress(bytes.data(), static_cast<int>(bytes.size()));
  EXPECT_TRUE(decoded.valid);
  EXPECT_TRUE(decoded.hasPageCount);
  EXPECT_EQ(decoded.sectionIndex, 4);
  EXPECT_EQ(decoded.page, 4);
  EXPECT_EQ(decoded.pageCount, 31);
}

TEST_F(ProgressGuardTest, Fb2ForgetReenablesTheWriteAfterACacheClear) {
  ReaderProgressGuard guard;
  ASSERT_TRUE(saveFb2Progress(guard, cachePath, 2, 7, 9));
  EXPECT_EQ(Storage.writeCount, 1);

  // "Delete cache" removes progress.bin and re-saves the backed-up position.
  ASSERT_TRUE(Storage.remove((cachePath + "/progress.bin").c_str()));
  guard.forget();
  EXPECT_TRUE(saveFb2Progress(guard, cachePath, 2, 7, 9));
  EXPECT_EQ(Storage.writeCount, 2);

  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 6u);
  const auto decoded = fb2_reader::decodeProgress(bytes.data(), static_cast<int>(bytes.size()));
  EXPECT_EQ(decoded.sectionIndex, 2);
  EXPECT_EQ(decoded.page, 7);
  EXPECT_EQ(decoded.pageCount, 9);
}

TEST_F(ProgressGuardTest, AFailedWriteIsNotRecordedAsSaved) {
  ReaderProgressGuard guard;
  const std::string missing = dir.path() + "/no_such_cache";
  uint8_t data[6] = {1, 0, 2, 0, 3, 0};
  EXPECT_FALSE(guard.save(missing, 1, 2, 3, data, sizeof(data)));

  // The position was never recorded, so the retry on the next repaint writes.
  EXPECT_TRUE(guard.save(cachePath, 1, 2, 3, data, sizeof(data)));
  const auto bytes = savedBytes();
  ASSERT_EQ(bytes.size(), 6u);
  EXPECT_EQ(bytes[0], 1);
  EXPECT_EQ(bytes[2], 2);
  EXPECT_EQ(bytes[4], 3);
}

}  // namespace
