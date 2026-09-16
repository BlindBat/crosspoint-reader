// Reader helper suite: bookmark path/summary utilities, per-book bookmark
// persistence, crash-safe progress writes, progress byte layout, link hit
// testing, percent wrap, reader input/refresh helpers, menu and end-of-book
// row order, and QR rendering. Production sources compile against the
// stdio/scripted stand-ins in stubs/.

#include <gtest/gtest.h>
#include <qrcode.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "GfxRenderer.h"
#include "HalGPIO.h"
#include "HalStorage.h"
#include "HalTiltSensor.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EndOfBookRows.h"
#include "activities/reader/EpubReaderMenuItems.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/PercentWrap.h"
#include "activities/reader/ProgressFile.h"
#include "activities/reader/ReaderUtils.h"
#include "util/BookmarkFile.h"
#include "util/BookmarkUtil.h"
#include "util/QrUtils.h"

namespace {

using Button = MappedInputManager::Button;
using SwipeDir = MappedInputManager::SwipeDir;
using ReaderMenu::MenuAction;

// Per-test scratch root for the HalStorage stub; SETTINGS and the HAL
// singletons are reset so scripted state never leaks between tests.
class ReaderHelpersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/crosspoint_reader_helpers_XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl), nullptr);
    root = tmpl;
    Storage.root = root;
    Storage.resetControls();
    SETTINGS.reset();
    gpio = HalGPIO{};
    halTiltSensor = HalTiltSensor{};
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  std::string hostPath(const std::string& devicePath) const { return root + devicePath; }

  bool hostExists(const std::string& devicePath) const {
    struct stat st{};
    return ::stat(hostPath(devicePath).c_str(), &st) == 0;
  }

  std::string readHost(const std::string& devicePath) const {
    std::FILE* f = std::fopen(hostPath(devicePath).c_str(), "rb");
    if (!f) return "<missing>";
    std::string out;
    char buf[256];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
  }

  void writeHost(const std::string& devicePath, const std::string& content) const {
    std::FILE* f = std::fopen(hostPath(devicePath).c_str(), "wb");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), f), content.size());
    std::fclose(f);
  }

  std::string root;
};

// ---------------------------------------------------------------------------
// BookmarkUtil (FR-085: /.crosspoint/bookmarks/<flattened path>.json, ≤72-char summary)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, BookmarksDirIsFixed) {
  EXPECT_EQ(BookmarkUtil::getBookmarksDir(), "/.crosspoint/bookmarks/");
}

TEST_F(ReaderHelpersTest, BookmarkPathFlattensNestedFolders) {
  EXPECT_EQ(BookmarkUtil::getBookmarkPath("/Books/Fiction/Dune.epub"),
            "/.crosspoint/bookmarks/Books_Fiction_Dune.json");
}

TEST_F(ReaderHelpersTest, BookmarkPathFlattensBackslashes) {
  EXPECT_EQ(BookmarkUtil::getBookmarkPath("/Books\\Dune.epub"), "/.crosspoint/bookmarks/Books_Dune.json");
}

TEST_F(ReaderHelpersTest, BookmarkPathWithoutExtensionAppendsJson) {
  EXPECT_EQ(BookmarkUtil::getBookmarkPath("/Books/README"), "/.crosspoint/bookmarks/Books_README.json");
}

TEST_F(ReaderHelpersTest, BookmarkPathStripsOnlyLastExtension) {
  EXPECT_EQ(BookmarkUtil::getBookmarkPath("/my.book.v2.epub"), "/.crosspoint/bookmarks/my.book.v2.json");
}

TEST_F(ReaderHelpersTest, BookmarkPathTreatsDotInFolderAsExtensionWhenFileHasNone) {
  // Pinned: the last dot is stripped even when it belonged to a folder name.
  EXPECT_EQ(BookmarkUtil::getBookmarkPath("/Books.old/title"), "/.crosspoint/bookmarks/Books.json");
}

TEST_F(ReaderHelpersTest, SanitizeSummaryCollapsesWhitespaceRuns) {
  // Pinned: a run collapses to its *first* character, so a tab-led run stays a tab.
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("a  b\t\t c"), "a b\tc");
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("one     two"), "one two");
}

TEST_F(ReaderHelpersTest, SanitizeSummaryDropsNewlines) {
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("first\nsecond"), "firstsecond");
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("first \nsecond"), "first second");
}

TEST_F(ReaderHelpersTest, SanitizeSummaryTrimsEnds) {
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("   padded text  \t"), "padded text");
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("   \n  "), "");
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(""), "");
}

TEST_F(ReaderHelpersTest, SanitizeSummaryTruncatesTo72Chars) {
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(std::string(100, 'x')), std::string(72, 'x'));
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(std::string(72, 'y')).size(), 72u);
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(std::string(73, 'z')).size(), 72u) << "one over the cap still cuts";
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(std::string(71, 'w')).size(), 71u);
}

TEST_F(ReaderHelpersTest, SanitizeSummaryLeavesNonAsciiBytesAlone) {
  // Book text is untrusted: bytes >= 0x80 are not whitespace and must survive
  // the collapse/trim passes byte for byte.
  const std::string utf8 = "\xC3\xA9\xC3\xA0\xE2\x80\x94";  // "éà—"
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(utf8), utf8);
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary("  " + utf8 + "  "), utf8);
  EXPECT_EQ(BookmarkUtil::sanitizeBookmarkSummary(utf8 + "  " + utf8), utf8 + " " + utf8);
}

// ---------------------------------------------------------------------------
// BookmarkFile (FR-085 persistence, Constitution VI malformed input)
// ---------------------------------------------------------------------------

BookmarkEntry makeBookmark(const char* xpath, const float pct, const char* summary, const uint16_t si,
                           const uint16_t pc, const uint16_t pp) {
  BookmarkEntry e;
  e.xpath = xpath;
  e.percentage = pct;
  e.summary = summary;
  e.computedSpineIndex = si;
  e.computedChapterPageCount = pc;
  e.computedChapterProgress = pp;
  return e;
}

TEST_F(ReaderHelpersTest, BookmarkSaveCreatesDirectoryAndFile) {
  std::vector<BookmarkEntry> in{makeBookmark("/body/p[1]", 0.25f, "Once upon", 3, 40, 7)};
  ASSERT_TRUE(BookmarkFile::save("/Books/Dune.epub", in));
  EXPECT_TRUE(hostExists("/.crosspoint/bookmarks"));
  EXPECT_TRUE(hostExists("/.crosspoint/bookmarks/Books_Dune.json"));
  const std::string json = readHost("/.crosspoint/bookmarks/Books_Dune.json");
  EXPECT_NE(json.find("\"bookmarks\""), std::string::npos);
  EXPECT_NE(json.find("\"xpath\":\"/body/p[1]\""), std::string::npos);
  EXPECT_EQ(json.find("\"vo\""), std::string::npos) << "no visible offset must not emit vo";
}

TEST_F(ReaderHelpersTest, BookmarkRoundTripPreservesAllFieldsAndOrder) {
  std::vector<BookmarkEntry> in;
  in.push_back(makeBookmark("/a", 0.1f, "first", 1, 10, 2));
  in.push_back(makeBookmark("/b", 0.5f, "second", 2, 20, 4));
  in.back().hasVisibleTextOffset = true;
  in.back().visibleTextOffset = 0x01020304u;
  in.push_back(makeBookmark("/c", 0.9f, "third", 3, 30, 6));
  ASSERT_TRUE(BookmarkFile::save("/Dune.epub", in));

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkFile::load("/Dune.epub", out));
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0].xpath, "/a");
  EXPECT_EQ(out[1].xpath, "/b");
  EXPECT_EQ(out[2].xpath, "/c");
  EXPECT_FLOAT_EQ(out[1].percentage, 0.5f);
  EXPECT_EQ(out[1].summary, "second");
  EXPECT_EQ(out[1].computedSpineIndex, 2);
  EXPECT_EQ(out[1].computedChapterPageCount, 20);
  EXPECT_EQ(out[1].computedChapterProgress, 4);
  EXPECT_TRUE(out[1].hasVisibleTextOffset);
  EXPECT_EQ(out[1].visibleTextOffset, 0x01020304u);
  EXPECT_FALSE(out[0].hasVisibleTextOffset);
  EXPECT_FALSE(out[2].hasVisibleTextOffset);
}

TEST_F(ReaderHelpersTest, BookmarkLoadMissingFileClearsAndReturnsFalse) {
  std::vector<BookmarkEntry> out{makeBookmark("/stale", 0.0f, "", 0, 0, 0)};
  EXPECT_FALSE(BookmarkFile::load("/Nope.epub", out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ReaderHelpersTest, BookmarkLoadEmptyFileReturnsFalse) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/Empty.json", "");
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkFile::load("/Empty.epub", out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ReaderHelpersTest, BookmarkLoadMalformedJsonReturnsFalse) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/Bad.json", "{\"bookmarks\": [ {\"xpath\": }");
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkFile::load("/Bad.epub", out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ReaderHelpersTest, BookmarkLoadTruncatedJsonReturnsFalse) {
  std::vector<BookmarkEntry> in{makeBookmark("/a", 0.1f, "first", 1, 10, 2)};
  ASSERT_TRUE(BookmarkFile::save("/Cut.epub", in));
  const std::string full = readHost("/.crosspoint/bookmarks/Cut.json");
  writeHost("/.crosspoint/bookmarks/Cut.json", full.substr(0, full.size() / 2));
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkFile::load("/Cut.epub", out));
}

TEST_F(ReaderHelpersTest, BookmarkLoadHostileNestingReturnsFalse) {
  Storage.mkdir("/.crosspoint/bookmarks");
  std::string nested = "{\"bookmarks\":";
  for (int i = 0; i < 200; i++) nested += '[';
  for (int i = 0; i < 200; i++) nested += ']';
  nested += '}';
  writeHost("/.crosspoint/bookmarks/Deep.json", nested);
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkFile::load("/Deep.epub", out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ReaderHelpersTest, BookmarkLoadWithoutBookmarksKeyIsEmptySuccess) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/NoKey.json", "{\"other\": 1}");
  std::vector<BookmarkEntry> out;
  EXPECT_TRUE(BookmarkFile::load("/NoKey.epub", out));
  EXPECT_TRUE(out.empty());
}

TEST_F(ReaderHelpersTest, BookmarkLoadNonArrayBookmarksKeyIsEmptySuccess) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/NotArray.json", "{\"bookmarks\": 42}");
  std::vector<BookmarkEntry> out{makeBookmark("/stale", 0.0f, "", 0, 0, 0)};
  EXPECT_TRUE(BookmarkFile::load("/NotArray.epub", out));
  EXPECT_TRUE(out.empty()) << "a scalar under the bookmarks key yields no rows, not garbage rows";
}

TEST_F(ReaderHelpersTest, BookmarkLoadNonObjectElementsBecomeDefaultEntries) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/Scalars.json", "{\"bookmarks\":[1,\"two\",null]}");
  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkFile::load("/Scalars.epub", out));
  ASSERT_EQ(out.size(), 3u) << "one row per element, each all-defaults";
  for (const auto& e : out) {
    EXPECT_EQ(e.xpath, "");
    EXPECT_FLOAT_EQ(e.percentage, 0.0f);
    EXPECT_EQ(e.summary, "");
    EXPECT_EQ(e.computedSpineIndex, 0);
    EXPECT_EQ(e.computedChapterPageCount, 0);
    EXPECT_EQ(e.computedChapterProgress, 0);
    EXPECT_FALSE(e.hasVisibleTextOffset);
  }
}

TEST_F(ReaderHelpersTest, BookmarkLoadOutOfRangeNumbersSaturateIntoTheFields) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/Range.json",
            "{\"bookmarks\":[{\"xpath\":\"/a\",\"si\":70000,\"pc\":-1,\"pp\":65535,\"vo\":4294967295}]}");
  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkFile::load("/Range.epub", out));
  ASSERT_EQ(out.size(), 1u);
  // Pinned: ArduinoJson refuses a value that does not fit uint16_t and the `| 0`
  // default takes over; 65535 and the full uint32_t offset survive intact.
  EXPECT_EQ(out[0].computedSpineIndex, 0);
  EXPECT_EQ(out[0].computedChapterPageCount, 0);
  EXPECT_EQ(out[0].computedChapterProgress, 65535);
  EXPECT_TRUE(out[0].hasVisibleTextOffset);
  EXPECT_EQ(out[0].visibleTextOffset, 4294967295u);
}

TEST_F(ReaderHelpersTest, BookmarkLoadLyingFieldTypesFallBackToDefaults) {
  Storage.mkdir("/.crosspoint/bookmarks");
  writeHost("/.crosspoint/bookmarks/Lies.json",
            "{\"bookmarks\":[{\"xpath\":42,\"percentage\":\"half\",\"summary\":[1],"
            "\"si\":\"x\",\"pc\":{},\"pp\":true}]}");
  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkFile::load("/Lies.epub", out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].xpath, "");
  EXPECT_FLOAT_EQ(out[0].percentage, 0.0f);
  EXPECT_EQ(out[0].summary, "");
  EXPECT_EQ(out[0].computedSpineIndex, 0);
  EXPECT_EQ(out[0].computedChapterPageCount, 0);
  EXPECT_FALSE(out[0].hasVisibleTextOffset);
}

TEST_F(ReaderHelpersTest, BookmarkSaveReportsWriteFailure) {
  Storage.failNextWrite = true;
  std::vector<BookmarkEntry> in{makeBookmark("/a", 0.1f, "first", 1, 10, 2)};
  EXPECT_FALSE(BookmarkFile::save("/Fail.epub", in));
}

// ---------------------------------------------------------------------------
// ProgressFile::writeAtomic (FR-076: temp file renamed over progress.bin)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, ProgressWriteAtomicLandsBytesAndRemovesTemp) {
  Storage.mkdir("/.crosspoint/epub_1");
  const uint8_t data[] = {1, 2, 3, 4, 5, 6};
  ASSERT_TRUE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, sizeof(data)));
  EXPECT_EQ(readHost("/.crosspoint/epub_1/progress.bin"), std::string("\x01\x02\x03\x04\x05\x06", 6));
  EXPECT_FALSE(hostExists("/.crosspoint/epub_1/progress.bin.tmp"));
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicUsesOpenTempRemoveRenameOrder) {
  Storage.mkdir("/.crosspoint/epub_1");
  const uint8_t data[] = {9};
  ASSERT_TRUE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, 1));
  const std::vector<std::string> expected = {
      "openW:/.crosspoint/epub_1/progress.bin.tmp",
      "remove:/.crosspoint/epub_1/progress.bin",
      "rename:/.crosspoint/epub_1/progress.bin.tmp->/.crosspoint/epub_1/progress.bin",
  };
  EXPECT_EQ(Storage.ops, expected);
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicReplacesExistingFile) {
  Storage.mkdir("/.crosspoint/epub_1");
  writeHost("/.crosspoint/epub_1/progress.bin", "OLDOLDOLDOLD");
  const uint8_t data[] = {7, 7};
  ASSERT_TRUE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, 2));
  EXPECT_EQ(readHost("/.crosspoint/epub_1/progress.bin"), std::string("\x07\x07", 2));
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicShortWritePreservesOldFile) {
  Storage.mkdir("/.crosspoint/epub_1");
  writeHost("/.crosspoint/epub_1/progress.bin", "KEEP");
  Storage.maxWriteBytes = 3;
  const uint8_t data[] = {1, 2, 3, 4, 5, 6};
  EXPECT_FALSE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, sizeof(data)));
  EXPECT_EQ(readHost("/.crosspoint/epub_1/progress.bin"), "KEEP");
  // Neither remove nor rename ran after the short write.
  ASSERT_EQ(Storage.ops.size(), 1u);
  EXPECT_EQ(Storage.ops[0], "openW:/.crosspoint/epub_1/progress.bin.tmp");
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicOpenFailureLeavesOldFile) {
  Storage.mkdir("/.crosspoint/epub_1");
  writeHost("/.crosspoint/epub_1/progress.bin", "KEEP");
  Storage.failNextOpenForWrite = true;
  const uint8_t data[] = {1};
  EXPECT_FALSE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, 1));
  EXPECT_EQ(readHost("/.crosspoint/epub_1/progress.bin"), "KEEP");
  EXPECT_EQ(Storage.ops.size(), 1u);
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicRenameFailureReportsFalse) {
  Storage.mkdir("/.crosspoint/epub_1");
  Storage.failNextRename = true;
  const uint8_t data[] = {1};
  EXPECT_FALSE(ProgressFile::writeAtomic("/.crosspoint/epub_1", data, 1));
  EXPECT_FALSE(hostExists("/.crosspoint/epub_1/progress.bin")) << "the old file was already removed";
  EXPECT_TRUE(hostExists("/.crosspoint/epub_1/progress.bin.tmp")) << "the fully written temp is what survives";
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicMissingCacheDirFails) {
  const uint8_t data[] = {1};
  EXPECT_FALSE(ProgressFile::writeAtomic("/.crosspoint/nope", data, 1));
}

TEST_F(ReaderHelpersTest, ProgressWriteAtomicZeroLengthCreatesEmptyFile) {
  Storage.mkdir("/.crosspoint/epub_1");
  ASSERT_TRUE(ProgressFile::writeAtomic("/.crosspoint/epub_1", nullptr, 0));
  EXPECT_TRUE(hostExists("/.crosspoint/epub_1/progress.bin"));
  EXPECT_EQ(readHost("/.crosspoint/epub_1/progress.bin"), "");
}

// ---------------------------------------------------------------------------
// EpubReaderUtils::saveProgress (FR-076 byte layout and 0..65535 range checks)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, SaveProgressWritesSixLittleEndianBytesWithoutOffset) {
  Storage.mkdir("/.crosspoint/epub_2");
  const Epub epub("/.crosspoint/epub_2");
  ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 0x0102, 0x0304, 0x0506));
  EXPECT_EQ(readHost("/.crosspoint/epub_2/progress.bin"), std::string("\x02\x01\x04\x03\x06\x05", 6));
}

TEST_F(ReaderHelpersTest, SaveProgressWritesTenBytesWithOffset) {
  Storage.mkdir("/.crosspoint/epub_2");
  const Epub epub("/.crosspoint/epub_2");
  ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 1, 2, 3, 0xAABBCCDDu));
  EXPECT_EQ(readHost("/.crosspoint/epub_2/progress.bin"),
            std::string("\x01\x00\x02\x00\x03\x00\xDD\xCC\xBB\xAA", 10));
}

TEST_F(ReaderHelpersTest, SaveProgressAcceptsUpperBoundary) {
  Storage.mkdir("/.crosspoint/epub_2");
  const Epub epub("/.crosspoint/epub_2");
  ASSERT_TRUE(EpubReaderUtils::saveProgress(epub, 0xFFFF, 0xFFFF, 0xFFFF));
  EXPECT_EQ(readHost("/.crosspoint/epub_2/progress.bin"), std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6));
}

TEST_F(ReaderHelpersTest, SaveProgressRejectsOutOfRangeValuesWithoutTouchingStorage) {
  Storage.mkdir("/.crosspoint/epub_2");
  const Epub epub("/.crosspoint/epub_2");
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, -1, 0, 0));
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 0x10000, 0, 0));
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 0, -1, 0));
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 0, 0x10000, 0));
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 0, 0, -1));
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 0, 0, 0x10000));
  EXPECT_TRUE(Storage.ops.empty());
  EXPECT_FALSE(hostExists("/.crosspoint/epub_2/progress.bin"));
}

// ---------------------------------------------------------------------------
// EpubReaderUtils::linkAtPoint (FR-087: 6 px slop, 28 px minimum width)
// ---------------------------------------------------------------------------

PageLink makeLink(const char* href, const int16_t x, const int16_t y, const int16_t w, const int16_t h) {
  PageLink link;
  std::snprintf(link.href, sizeof(link.href), "%s", href);
  link.x = x;
  link.y = y;
  link.width = w;
  link.height = h;
  return link;
}

TEST_F(ReaderHelpersTest, LinkAtPointHitsInsideAndWithinSlop) {
  const std::vector<PageLink> links{makeLink("a.html", 100, 200, 60, 20)};
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 130, 210, 0, 0), nullptr);
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 94, 210, 0, 0), nullptr);   // x - 6
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 93, 210, 0, 0), nullptr);   // x - 7
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 165, 210, 0, 0), nullptr);  // x + w + 5
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 166, 210, 0, 0), nullptr);  // x + w + 6
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 130, 194, 0, 0), nullptr);  // y - 6
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 130, 193, 0, 0), nullptr);  // y - 7
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 130, 225, 0, 0), nullptr);  // y + h + 5
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 130, 226, 0, 0), nullptr);  // y + h + 6
}

TEST_F(ReaderHelpersTest, LinkAtPointWidensNarrowTargetsHorizontallyOnly) {
  // 4 px wide note marker: horizontal reach grows to (28 - 4) / 2 = 12 px; vertical stays 6.
  const std::vector<PageLink> links{makeLink("n1", 100, 200, 4, 10)};
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 88, 205, 0, 0), nullptr);
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 87, 205, 0, 0), nullptr);
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 115, 205, 0, 0), nullptr);
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 116, 205, 0, 0), nullptr);
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 102, 193, 0, 0), nullptr);
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 102, 194, 0, 0), nullptr);
}

TEST_F(ReaderHelpersTest, LinkAtPointSubtractsMargins) {
  const std::vector<PageLink> links{makeLink("m", 0, 0, 20, 10)};
  EXPECT_NE(EpubReaderUtils::linkAtPoint(links, 35, 45, 30, 40), nullptr);
  EXPECT_EQ(EpubReaderUtils::linkAtPoint(links, 5, 5, 30, 40), nullptr);
}

TEST_F(ReaderHelpersTest, LinkAtPointReturnsFirstMatchAndNullWhenEmpty) {
  const std::vector<PageLink> links{makeLink("first", 0, 0, 50, 50), makeLink("second", 10, 10, 50, 50)};
  const PageLink* hit = EpubReaderUtils::linkAtPoint(links, 20, 20, 0, 0);
  ASSERT_NE(hit, nullptr);
  EXPECT_STREQ(hit->href, "first");
  EXPECT_EQ(EpubReaderUtils::linkAtPoint({}, 20, 20, 0, 0), nullptr);
}

// ---------------------------------------------------------------------------
// ReaderPercent::wrapPercent (FR-088: 0..100 slider, ±1 / ±10 steps)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, WrapPercentStepsWithinRange) {
  EXPECT_EQ(ReaderPercent::wrapPercent(0, 1), 1);
  EXPECT_EQ(ReaderPercent::wrapPercent(42, 10), 52);
  EXPECT_EQ(ReaderPercent::wrapPercent(42, -10), 32);
  EXPECT_EQ(ReaderPercent::wrapPercent(0, 0), 0);
  EXPECT_EQ(ReaderPercent::wrapPercent(100, 0), 100);
}

TEST_F(ReaderHelpersTest, WrapPercentLandsOn100WithoutCrossing) {
  EXPECT_EQ(ReaderPercent::wrapPercent(90, 10), 100);
  EXPECT_EQ(ReaderPercent::wrapPercent(99, 1), 100);
  EXPECT_EQ(ReaderPercent::wrapPercent(50, 50), 100);
}

TEST_F(ReaderHelpersTest, WrapPercentWrapsPast100) {
  EXPECT_EQ(ReaderPercent::wrapPercent(100, 1), 1);
  EXPECT_EQ(ReaderPercent::wrapPercent(100, 10), 10);
  EXPECT_EQ(ReaderPercent::wrapPercent(95, 10), 5);
}

TEST_F(ReaderHelpersTest, WrapPercentWrapsBelowZero) {
  EXPECT_EQ(ReaderPercent::wrapPercent(0, -1), 99);
  EXPECT_EQ(ReaderPercent::wrapPercent(0, -10), 90);
  EXPECT_EQ(ReaderPercent::wrapPercent(100, -1), 99);
  EXPECT_EQ(ReaderPercent::wrapPercent(5, -10), 95);
}

// ---------------------------------------------------------------------------
// ReaderUtils::detectPageTurn (FR-077 press/release semantics, power, tilt)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, PageTurnFrontButtonsFireOnPressByDefault) {
  MappedInputManager input;
  input.press(Button::Left);
  auto r = ReaderUtils::detectPageTurn(input);
  EXPECT_TRUE(r.prev);
  EXPECT_FALSE(r.next);
  EXPECT_FALSE(r.fromTilt);

  input = MappedInputManager{};
  input.press(Button::Right);
  r = ReaderUtils::detectPageTurn(input);
  EXPECT_FALSE(r.prev);
  EXPECT_TRUE(r.next);
}

TEST_F(ReaderHelpersTest, PageTurnSwapsFrontButtonsWhenNavDirectionSwapped) {
  MappedInputManager input;
  input.navSwapped = true;
  input.press(Button::Left);
  const auto r = ReaderUtils::detectPageTurn(input);
  EXPECT_TRUE(r.next);
  EXPECT_FALSE(r.prev);
}

TEST_F(ReaderHelpersTest, PageTurnSideButtonsAreNotSwapped) {
  MappedInputManager input;
  input.navSwapped = true;
  input.press(Button::PageBack);
  EXPECT_TRUE(ReaderUtils::detectPageTurn(input).prev);
  input = MappedInputManager{};
  input.press(Button::PageForward);
  EXPECT_TRUE(ReaderUtils::detectPageTurn(input).next);
}

TEST_F(ReaderHelpersTest, PageTurnUsesReleaseOrHoldWhenLongPressBehaviorSet) {
  SETTINGS.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  MappedInputManager input;
  input.press(Button::Right);
  EXPECT_FALSE(ReaderUtils::detectPageTurn(input).next) << "press alone must not turn";

  input = MappedInputManager{};
  input.release(Button::Right);
  EXPECT_TRUE(ReaderUtils::detectPageTurn(input).next);

  input = MappedInputManager{};
  input.longPress(Button::Right, ReaderUtils::SKIP_HOLD_MS);
  EXPECT_TRUE(ReaderUtils::detectPageTurn(input).next);
  EXPECT_EQ(input.lastLongPressThreshold, 700u);

  input = MappedInputManager{};
  input.longPress(Button::Right, ReaderUtils::SKIP_HOLD_MS - 1);
  EXPECT_FALSE(ReaderUtils::detectPageTurn(input).next);
}

TEST_F(ReaderHelpersTest, PageTurnPowerReleaseOnlyInPageTurnMode) {
  MappedInputManager input;
  input.release(Button::Power);
  EXPECT_FALSE(ReaderUtils::detectPageTurn(input).next);
  SETTINGS.shortPwrBtn = CrossPointSettings::PAGE_TURN;
  const auto r = ReaderUtils::detectPageTurn(input);
  EXPECT_TRUE(r.next);
  EXPECT_FALSE(r.prev);
}

TEST_F(ReaderHelpersTest, PageTurnTiltOnlyWhenEnabled) {
  MappedInputManager input;
  halTiltSensor.forwardEvent = true;
  EXPECT_FALSE(ReaderUtils::detectPageTurn(input).next);

  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_NORMAL;
  halTiltSensor.forwardEvent = true;
  auto r = ReaderUtils::detectPageTurn(input);
  EXPECT_TRUE(r.next);
  EXPECT_TRUE(r.fromTilt);

  halTiltSensor.backEvent = true;
  r = ReaderUtils::detectPageTurn(input);
  EXPECT_TRUE(r.prev);
  EXPECT_FALSE(r.next);
  EXPECT_TRUE(r.fromTilt);
}

// ---------------------------------------------------------------------------
// ReaderUtils touch helpers (FR-077 touch zones, reader-menu tap)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, TouchPageTurnDisabledWithoutSettingOrHardware) {
  GfxRenderer renderer;
  MappedInputManager input;
  input.tapped = true;
  input.tapX = 10;
  input.tapY = 400;
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_ON;
  input.touch = false;
  EXPECT_FALSE(ReaderUtils::detectTouchPageTurn(renderer, input).prev);

  input.touch = true;
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_OFF;
  EXPECT_FALSE(ReaderUtils::detectTouchPageTurn(renderer, input).prev);
}

TEST_F(ReaderHelpersTest, TouchPageTurnSwipeModeMapsHorizontalSwipes) {
  GfxRenderer renderer;
  MappedInputManager input;
  input.touch = true;
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_SWIPE;
  input.swipe = SwipeDir::Left;
  EXPECT_TRUE(ReaderUtils::detectTouchPageTurn(renderer, input).next);
  input.swipe = SwipeDir::Right;
  EXPECT_TRUE(ReaderUtils::detectTouchPageTurn(renderer, input).prev);
  input.swipe = SwipeDir::Up;
  const auto r = ReaderUtils::detectTouchPageTurn(renderer, input);
  EXPECT_FALSE(r.prev);
  EXPECT_FALSE(r.next);
  // Taps are ignored in swipe mode even in the outer thirds.
  input.swipe = SwipeDir::None;
  input.tapped = true;
  input.tapX = 10;
  input.tapY = 400;
  EXPECT_FALSE(ReaderUtils::detectTouchPageTurn(renderer, input).prev);
}

TEST_F(ReaderHelpersTest, TouchPageTurnTapModeUsesOuterThirds) {
  GfxRenderer renderer;  // 480 wide: zones are [0,160) and [320,480)
  MappedInputManager input;
  input.touch = true;
  input.tapped = true;
  input.tapY = 400;
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_ON;
  gpio.touchHeldMs = 123;

  input.tapX = 159;
  auto r = ReaderUtils::detectTouchPageTurn(renderer, input);
  EXPECT_TRUE(r.prev);
  EXPECT_FALSE(r.next);
  EXPECT_EQ(r.heldMs, 123u);

  input.tapX = 160;
  r = ReaderUtils::detectTouchPageTurn(renderer, input);
  EXPECT_FALSE(r.prev);
  EXPECT_FALSE(r.next);

  input.tapX = 319;
  r = ReaderUtils::detectTouchPageTurn(renderer, input);
  EXPECT_FALSE(r.next);

  input.tapX = 320;
  r = ReaderUtils::detectTouchPageTurn(renderer, input);
  EXPECT_TRUE(r.next);
  EXPECT_FALSE(r.prev);
}

TEST_F(ReaderHelpersTest, TouchPageTurnInvertedTapSwapsZones) {
  GfxRenderer renderer;
  MappedInputManager input;
  input.touch = true;
  input.tapped = true;
  input.tapY = 400;
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_INVERTED_TAP;
  input.tapX = 10;
  EXPECT_TRUE(ReaderUtils::detectTouchPageTurn(renderer, input).next);
  input.tapX = 470;
  EXPECT_TRUE(ReaderUtils::detectTouchPageTurn(renderer, input).prev);
}

TEST_F(ReaderHelpersTest, TouchMenuTapRequiresCenterThirdAndTapSetting) {
  GfxRenderer renderer;  // center: x in [160,320), y in [266,534)
  MappedInputManager input;
  input.touch = true;
  input.tapped = true;
  input.tapX = 240;
  input.tapY = 400;
  EXPECT_TRUE(ReaderUtils::isTouchMenuTap(renderer, input));

  input.tapX = 159;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));
  input.tapX = 320;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));
  input.tapX = 240;
  input.tapY = 265;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));
  input.tapY = 534;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));

  input.tapY = 400;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_OFF;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_TAP;
  input.touch = false;
  EXPECT_FALSE(ReaderUtils::isTouchMenuTap(renderer, input));
}

TEST_F(ReaderHelpersTest, TouchMenuGestureHonorsMenuSwipeAndSwipeUpSetting) {
  GfxRenderer renderer;
  MappedInputManager input;
  input.touch = true;
  input.menuGesture = true;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_OFF;
  EXPECT_TRUE(ReaderUtils::isTouchMenuGesture(renderer, input)) << "menu edge swipe ignores showReaderMenu";

  input.menuGesture = false;
  input.readerMenuSwipeUp = true;
  EXPECT_FALSE(ReaderUtils::isTouchMenuGesture(renderer, input));
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_SWIPE_UP;
  EXPECT_TRUE(ReaderUtils::isTouchMenuGesture(renderer, input));

  input.readerMenuSwipeUp = false;
  input.tapped = true;
  input.tapX = 240;
  input.tapY = 400;
  EXPECT_FALSE(ReaderUtils::isTouchMenuGesture(renderer, input)) << "center tap needs READER_MENU_TAP";
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_TAP;
  EXPECT_TRUE(ReaderUtils::isTouchMenuGesture(renderer, input));
  input.touch = false;
  EXPECT_FALSE(ReaderUtils::isTouchMenuGesture(renderer, input));
}

// ---------------------------------------------------------------------------
// ReaderUtils refresh countdown (FR-091: fast refresh, half every N pages)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, RefreshCycleCountsDownThenPromotesToHalf) {
  GfxRenderer renderer;
  SETTINGS.refreshFrequencyPages = 5;
  int pages = 3;
  ReaderUtils::displayWithRefreshCycle(renderer, pages);
  ASSERT_EQ(renderer.displayCalls.size(), 1u);
  EXPECT_EQ(renderer.displayCalls[0], HalDisplay::FAST_REFRESH);
  EXPECT_EQ(pages, 2);
  ReaderUtils::displayWithRefreshCycle(renderer, pages);
  EXPECT_EQ(renderer.displayCalls[1], HalDisplay::FAST_REFRESH);
  EXPECT_EQ(pages, 1);
  ReaderUtils::displayWithRefreshCycle(renderer, pages);
  EXPECT_EQ(renderer.displayCalls[2], HalDisplay::HALF_REFRESH);
  EXPECT_EQ(pages, 5) << "counter reloads from Refresh Frequency";
  EXPECT_TRUE(renderer.asyncCalls.empty());
}

TEST_F(ReaderHelpersTest, RefreshCycleZeroCounterIsHalfRefresh) {
  GfxRenderer renderer;
  SETTINGS.refreshFrequencyPages = 15;
  int pages = 0;
  ReaderUtils::displayWithRefreshCycle(renderer, pages);
  EXPECT_EQ(renderer.displayCalls.at(0), HalDisplay::HALF_REFRESH);
  EXPECT_EQ(pages, 15);
}

TEST_F(ReaderHelpersTest, RefreshCycleAsyncUsesAsyncDisplayOnly) {
  GfxRenderer renderer;
  int pages = 1;
  SETTINGS.refreshFrequencyPages = 30;
  ReaderUtils::displayWithRefreshCycle(renderer, pages, true);
  EXPECT_TRUE(renderer.displayCalls.empty());
  ASSERT_EQ(renderer.asyncCalls.size(), 1u);
  EXPECT_EQ(renderer.asyncCalls[0], HalDisplay::HALF_REFRESH);
  EXPECT_EQ(pages, 30);
}

TEST_F(ReaderHelpersTest, BaseRefreshCycleRoutesThroughGrayscaleBaseOnCombiningPanels) {
  GfxRenderer renderer;
  int pages = 2;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, pages);
  EXPECT_EQ(renderer.displayCalls.size(), 1u);
  EXPECT_TRUE(renderer.baseCalls.empty());
  EXPECT_EQ(pages, 1);

  renderer.combinesBase = true;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, pages);
  EXPECT_EQ(renderer.displayCalls.size(), 1u);
  ASSERT_EQ(renderer.baseCalls.size(), 1u);
  EXPECT_EQ(renderer.baseCalls[0], HalDisplay::HALF_REFRESH);
  EXPECT_EQ(pages, 15);
}

TEST_F(ReaderHelpersTest, AntiAliasedPassRunsLsbThenMsbAndRestoresBw) {
  GfxRenderer renderer;
  int renders = 0;
  ReaderUtils::renderAntiAliased(renderer, [&] { renders++; });
  EXPECT_EQ(renders, 2) << "content is re-rendered once per grayscale plane";
  const std::vector<std::string> expected = {"store",    "clear:00", "mode:lsb",     "copy:lsb", "clear:00",
                                             "mode:msb", "copy:msb", "display:gray", "mode:bw",  "restore"};
  EXPECT_EQ(renderer.trace, expected);
  EXPECT_EQ(renderer.renderMode, GfxRenderer::BW) << "render mode is left back on B/W";
}

TEST_F(ReaderHelpersTest, AntiAliasedPassFlushesDeferredBaseWhenBufferStoreFails) {
  GfxRenderer renderer;
  renderer.bwStoreSucceeds = false;
  int renders = 0;
  ReaderUtils::renderAntiAliased(renderer, [&] { renders++; });
  EXPECT_EQ(renders, 0);
  EXPECT_EQ(renderer.trace, std::vector<std::string>{"store-fail"}) << "non-combining panel needs no flush";

  renderer.trace.clear();
  renderer.combinesBase = true;
  ReaderUtils::renderAntiAliased(renderer, [&] { renders++; });
  EXPECT_EQ(renders, 0);
  const std::vector<std::string> expected = {"store-fail", "cleanup"};
  EXPECT_EQ(renderer.trace, expected);
}

// ---------------------------------------------------------------------------
// ReaderUtils::handleBackNavigation (FR-092) and applyOrientation
// ---------------------------------------------------------------------------

struct HomeCounter {
  int calls = 0;
  static void bump(void* ctx) { static_cast<HomeCounter*>(ctx)->calls++; }
};

TEST_F(ReaderHelpersTest, BackNavigationShortReleaseGoesHome) {
  MappedInputManager input;
  ActivityManager activities;
  HomeCounter home;
  input.release(Button::Back);
  input.heldTime = ReaderUtils::GO_BACK_OR_HOME_MS - 1;
  EXPECT_TRUE(ReaderUtils::handleBackNavigation(input, activities, "/Books/Dune.epub", {&home, &HomeCounter::bump}));
  EXPECT_EQ(home.calls, 1);
  EXPECT_TRUE(activities.fileBrowserPaths.empty());
}

TEST_F(ReaderHelpersTest, BackNavigationLongPressOpensFileBrowserAtBook) {
  MappedInputManager input;
  ActivityManager activities;
  HomeCounter home;
  input.longPress(Button::Back, ReaderUtils::GO_BACK_OR_HOME_MS);
  EXPECT_TRUE(ReaderUtils::handleBackNavigation(input, activities, "/Books/Dune.epub", {&home, &HomeCounter::bump}));
  EXPECT_EQ(home.calls, 0);
  ASSERT_EQ(activities.fileBrowserPaths.size(), 1u);
  EXPECT_EQ(activities.fileBrowserPaths[0], "/Books/Dune.epub");
}

TEST_F(ReaderHelpersTest, BackNavigationSwapsWhenShortBackToFileBrowser) {
  SETTINGS.backShortToFileBrowser = 1;
  MappedInputManager input;
  ActivityManager activities;
  HomeCounter home;
  input.release(Button::Back);
  input.heldTime = 200;
  EXPECT_TRUE(ReaderUtils::handleBackNavigation(input, activities, "/b.epub", {&home, &HomeCounter::bump}));
  EXPECT_EQ(activities.fileBrowserPaths.size(), 1u);
  EXPECT_EQ(home.calls, 0);

  input = MappedInputManager{};
  input.longPress(Button::Back, 1500);
  EXPECT_TRUE(ReaderUtils::handleBackNavigation(input, activities, "/b.epub", {&home, &HomeCounter::bump}));
  EXPECT_EQ(activities.fileBrowserPaths.size(), 1u);
  EXPECT_EQ(home.calls, 1);
}

TEST_F(ReaderHelpersTest, BackNavigationIgnoresBackGestureAndIdleInput) {
  MappedInputManager input;
  ActivityManager activities;
  HomeCounter home;
  EXPECT_FALSE(ReaderUtils::handleBackNavigation(input, activities, "/b.epub", {&home, &HomeCounter::bump}));
  input.release(Button::Back);
  input.backGesture = true;
  EXPECT_FALSE(ReaderUtils::handleBackNavigation(input, activities, "/b.epub", {&home, &HomeCounter::bump}));
  EXPECT_EQ(home.calls, 0);
  EXPECT_TRUE(activities.fileBrowserPaths.empty());
}

TEST_F(ReaderHelpersTest, ApplyOrientationMapsAllFourSettings) {
  GfxRenderer renderer;
  ReaderUtils::applyOrientation(renderer, CrossPointSettings::LANDSCAPE_CW);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::LandscapeClockwise);
  ReaderUtils::applyOrientation(renderer, CrossPointSettings::INVERTED);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::PortraitInverted);
  ReaderUtils::applyOrientation(renderer, CrossPointSettings::LANDSCAPE_CCW);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::LandscapeCounterClockwise);
  ReaderUtils::applyOrientation(renderer, CrossPointSettings::PORTRAIT);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::Portrait);
}

TEST_F(ReaderHelpersTest, ApplyOrientationLeavesUnknownValuesAlone) {
  GfxRenderer renderer;
  // Starts away from Portrait so a default branch that fell through to Portrait
  // (rather than doing nothing) would be visible here.
  ReaderUtils::applyOrientation(renderer, CrossPointSettings::LANDSCAPE_CCW);
  ReaderUtils::applyOrientation(renderer, 99);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::LandscapeCounterClockwise);
  ReaderUtils::applyOrientation(renderer, 4);
  EXPECT_EQ(renderer.orientation, GfxRenderer::Orientation::LandscapeCounterClockwise);
}

// ---------------------------------------------------------------------------
// Reader menu row order (FR-089/FR-088 rows present; Constitution V)
// ---------------------------------------------------------------------------

std::vector<MenuAction> actionsOf(const std::vector<ReaderMenu::MenuItem>& items) {
  std::vector<MenuAction> out;
  out.reserve(items.size());
  for (const auto& item : items) out.push_back(item.action);
  return out;
}

TEST_F(ReaderHelpersTest, MenuRowsDefaultOrderWithoutOptionalRows) {
  std::vector<ReaderMenu::MenuItem> items;
  ReaderMenu::buildMenuItems(items, false, false, false);
  const std::vector<MenuAction> expected = {
      MenuAction::SELECT_CHAPTER, MenuAction::TOGGLE_BOOKMARK, MenuAction::TEXT_SETTINGS, MenuAction::NIGHT_MODE,
      MenuAction::DICTIONARY,     MenuAction::ROTATE_SCREEN,   MenuAction::AUTO_PAGE_TURN, MenuAction::GO_TO_PERCENT,
      MenuAction::SCREENSHOT,     MenuAction::DISPLAY_QR,      MenuAction::GO_HOME,        MenuAction::SYNC,
      MenuAction::DELETE_CACHE,
  };
  EXPECT_EQ(actionsOf(items), expected);
}

TEST_F(ReaderHelpersTest, MenuRowsInsertFootnotesAndBookmarksAfterChapter) {
  std::vector<ReaderMenu::MenuItem> items;
  ReaderMenu::buildMenuItems(items, true, true, false);
  ASSERT_GE(items.size(), 4u);
  EXPECT_EQ(items[0].action, MenuAction::SELECT_CHAPTER);
  EXPECT_EQ(items[1].action, MenuAction::FOOTNOTES);
  EXPECT_EQ(items[2].action, MenuAction::BOOKMARKS);
  EXPECT_EQ(items[3].action, MenuAction::TOGGLE_BOOKMARK);

  ReaderMenu::buildMenuItems(items, false, true, false);
  EXPECT_EQ(items[1].action, MenuAction::BOOKMARKS);
  ReaderMenu::buildMenuItems(items, true, false, false);
  EXPECT_EQ(items[1].action, MenuAction::FOOTNOTES);
  EXPECT_EQ(items[2].action, MenuAction::TOGGLE_BOOKMARK);
}

TEST_F(ReaderHelpersTest, MenuRowsPlaceFrontlightRightAfterNightMode) {
  std::vector<ReaderMenu::MenuItem> items;
  ReaderMenu::buildMenuItems(items, false, false, true);
  const auto actions = actionsOf(items);
  auto it = std::find(actions.begin(), actions.end(), MenuAction::NIGHT_MODE);
  ASSERT_NE(it, actions.end());
  ASSERT_NE(it + 1, actions.end());
  EXPECT_EQ(*(it + 1), MenuAction::FRONTLIGHT);
  EXPECT_EQ(*(it + 2), MenuAction::DICTIONARY);
}

TEST_F(ReaderHelpersTest, MenuRowsFullSetFitsFixedCapacity) {
  std::vector<ReaderMenu::MenuItem> items;
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});  // must be cleared
  ReaderMenu::buildMenuItems(items, true, true, true);
  EXPECT_EQ(items.size(), ReaderMenu::MAX_MENU_ITEMS);
  EXPECT_GE(items.capacity(), ReaderMenu::MAX_MENU_ITEMS);
  EXPECT_EQ(items.front().action, MenuAction::SELECT_CHAPTER);
  EXPECT_EQ(items.back().action, MenuAction::DELETE_CACHE);
}

TEST_F(ReaderHelpersTest, MenuRowsCarryMatchingLabels) {
  std::vector<ReaderMenu::MenuItem> items;
  ReaderMenu::buildMenuItems(items, true, true, true);
  // Full (action, label) pairing in row order: every row is checked, so a label
  // swapped onto the wrong action fails here rather than slipping through a
  // default branch.
  const std::vector<std::pair<MenuAction, StrId>> expected = {
      {MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER},
      {MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES},
      {MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS},
      {MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK},
      {MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS},
      {MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE},
      {MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT},
      {MenuAction::DICTIONARY, StrId::STR_LOOKUP},
      {MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION},
      {MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN},
      {MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT},
      {MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON},
      {MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR},
      {MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON},
      {MenuAction::SYNC, StrId::STR_SYNC_PROGRESS},
      {MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE},
  };
  ASSERT_EQ(items.size(), expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    EXPECT_EQ(items[i].action, expected[i].first) << "row " << i;
    EXPECT_EQ(items[i].labelId, expected[i].second) << "row " << i;
  }
}

// ---------------------------------------------------------------------------
// End-of-book rows (FR-093: suggestions under "Continue with" plus a Home row)
// ---------------------------------------------------------------------------

TEST_F(ReaderHelpersTest, EndOfBookDisplayNameStripsExtension) {
  EXPECT_EQ(EndOfBookRows::displayName("Dune.epub"), "Dune");
  EXPECT_EQ(EndOfBookRows::displayName("Dune.part2.epub"), "Dune.part2");
  EXPECT_EQ(EndOfBookRows::displayName("NoExtension"), "NoExtension");
}

TEST_F(ReaderHelpersTest, EndOfBookRowsListSuggestionsThenHome) {
  std::string labels[4];
  const size_t count = EndOfBookRows::buildLabels({"B.epub", "A.epub", "C.epub"}, "Home", labels, 4);
  ASSERT_EQ(count, 4u);
  EXPECT_EQ(labels[0], "B");
  EXPECT_EQ(labels[1], "A");
  EXPECT_EQ(labels[2], "C");
  EXPECT_EQ(labels[3], "Home");
}

TEST_F(ReaderHelpersTest, EndOfBookRowsHomeOnlyWhenNoSuggestions) {
  std::string labels[4];
  EXPECT_EQ(EndOfBookRows::buildLabels({}, "Home", labels, 4), 1u);
  EXPECT_EQ(labels[0], "Home");
}

TEST_F(ReaderHelpersTest, EndOfBookRowsCapAtMaxRows) {
  // Pinned: when suggestions fill every row the Home row is dropped.
  std::string labels[2];
  EXPECT_EQ(EndOfBookRows::buildLabels({"A.epub", "B.epub", "C.epub"}, "Home", labels, 2), 2u);
  EXPECT_EQ(labels[0], "A");
  EXPECT_EQ(labels[1], "B");
}

TEST_F(ReaderHelpersTest, EndOfBookJoinPathHandlesRootAndSubfolder) {
  EXPECT_EQ(EndOfBookRows::joinPath("/", "A.epub"), "/A.epub");
  EXPECT_EQ(EndOfBookRows::joinPath("/Books", "A.epub"), "/Books/A.epub");
}

// ---------------------------------------------------------------------------
// QrUtils::drawQrCode (version selection, centering, UTF-8-safe truncation)
// ---------------------------------------------------------------------------

// Set-module count of the reference encoding at `version`.
size_t referenceModuleCount(const std::string& payload, const uint8_t version, uint8_t& sizeOut) {
  std::vector<uint8_t> buf(qrcode_getBufferSize(version));
  QRCode qr;
  if (qrcode_initText(&qr, buf.data(), version, ECC_LOW, payload.c_str()) != 0) return 0;
  sizeOut = qr.size;
  size_t count = 0;
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) count++;
    }
  }
  return count;
}

TEST_F(ReaderHelpersTest, QrShortPayloadUsesVersion4ModulesScaledAndCentered) {
  GfxRenderer renderer;
  const std::string payload = "https://example.org/book";
  uint8_t size = 0;
  const size_t expected = referenceModuleCount(payload, 4, size);
  ASSERT_EQ(size, 33);
  ASSERT_GT(expected, 0u);

  QrUtils::drawQrCode(renderer, Rect(10, 20, 200, 200), payload);
  ASSERT_EQ(renderer.rects.size(), expected);
  // 200 / 33 = 6 px per module; the 198 px code sits 1 px inside each edge.
  int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
  for (const auto& r : renderer.rects) {
    EXPECT_EQ(r.width, 6);
    EXPECT_EQ(r.height, 6);
    EXPECT_TRUE(r.state);
    minX = std::min(minX, r.x);
    minY = std::min(minY, r.y);
    maxX = std::max(maxX, r.x + r.width);
    maxY = std::max(maxY, r.y + r.height);
  }
  EXPECT_EQ(minX, 11);
  EXPECT_EQ(minY, 21);
  EXPECT_EQ(maxX, 11 + 198);
  EXPECT_EQ(maxY, 21 + 198);
}

TEST_F(ReaderHelpersTest, QrVersionThresholdsFollowPayloadLength) {
  // Module pixel size reveals the chosen version: px = 800 / (4 * version + 17),
  // which is distinct for every version QrUtils can pick (24/14/8/5/4).
  const auto pxFor = [](const size_t len) {
    GfxRenderer renderer;
    QrUtils::drawQrCode(renderer, Rect(0, 0, 800, 800), std::string(len, 'a'));
    return renderer.rects.empty() ? -1 : renderer.rects.front().width;
  };
  // Lengths stay inside what ricmoo/QRCode's stack buffers can hold at the
  // version QrUtils picks: its thresholds overshoot on the low versions
  // (see QrVersionSelectionStaysBelowEncoderCapacity).
  EXPECT_EQ(pxFor(1), 24);     // version 4 (33 modules)
  EXPECT_EQ(pxFor(99), 24);    // still version 4 at the 114-byte threshold's safe edge
  EXPECT_EQ(pxFor(115), 14);   // version 10 (57 modules)
  EXPECT_EQ(pxFor(343), 14);
  EXPECT_EQ(pxFor(396), 8);    // version 20 (97 modules)
  EXPECT_EQ(pxFor(1066), 8);   // last length before the version 30 threshold
  EXPECT_EQ(pxFor(1067), 5);   // version 30 (137 modules)
  EXPECT_EQ(pxFor(2110), 5);   // last length before the version 40 threshold
  EXPECT_EQ(pxFor(2111), 4);   // version 40 (177 modules)
}

TEST_F(ReaderHelpersTest, QrVersionForLengthPinsEveryThreshold) {
  EXPECT_EQ(QrUtils::versionForLength(0), 4);
  EXPECT_EQ(QrUtils::versionForLength(114), 4);
  EXPECT_EQ(QrUtils::versionForLength(115), 10);
  EXPECT_EQ(QrUtils::versionForLength(395), 10);
  EXPECT_EQ(QrUtils::versionForLength(396), 20);
  EXPECT_EQ(QrUtils::versionForLength(1066), 20);
  EXPECT_EQ(QrUtils::versionForLength(1067), 30);
  EXPECT_EQ(QrUtils::versionForLength(2110), 30);
  EXPECT_EQ(QrUtils::versionForLength(2111), 40);
  EXPECT_EQ(QrUtils::versionForLength(2953), 40) << "the truncation cap still selects version 40";
}

// Documents the live overshoot in the version thresholds (QrUtils.h versionForLength):
// ricmoo/QRCode writes the encoded bit stream into a stack VLA sized from the
// version's raw module count and never range-checks it, so a payload longer
// than that version can hold smashes the stack. The safe ceilings measured
// against the pinned QRCode 0.0.1 are 99 / 343 bytes for versions 4 / 10, well
// under the 114 / 395 byte thresholds QrUtils uses. The assertions below pin
// the safe side only; the unsafe band is reported, not exercised.
TEST_F(ReaderHelpersTest, QrVersionSelectionStaysBelowEncoderCapacity) {
  uint8_t size = 0;
  EXPECT_GT(referenceModuleCount(std::string(99, 'a'), 4, size), 0u);
  EXPECT_EQ(size, 33);
  EXPECT_GT(referenceModuleCount(std::string(343, 'a'), 10, size), 0u);
  EXPECT_EQ(size, 57);
  EXPECT_GT(referenceModuleCount(std::string(1066, 'a'), 20, size), 0u);
  EXPECT_EQ(size, 97);
  EXPECT_GT(referenceModuleCount(std::string(2110, 'a'), 30, size), 0u);
  EXPECT_EQ(size, 137);
  EXPECT_GT(referenceModuleCount(std::string(2953, 'a'), 40, size), 0u);
  EXPECT_EQ(size, 177);
}

TEST_F(ReaderHelpersTest, QrTinyBoundsClampModuleToOnePixelAndUseMinDimension) {
  GfxRenderer renderer;
  uint8_t size = 0;
  const size_t expected = referenceModuleCount("x", 4, size);
  ASSERT_EQ(size, 33);
  ASSERT_GT(expected, 0u);

  // 20 x 300: the module size comes from the SHORTER side (20 / 33 -> 0, clamped
  // to 1), so the 33 px code overhangs the narrow axis and is centered on both.
  QrUtils::drawQrCode(renderer, Rect(0, 0, 20, 300), "x");
  ASSERT_EQ(renderer.rects.size(), expected);
  int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
  for (const auto& r : renderer.rects) {
    EXPECT_EQ(r.width, 1);
    EXPECT_EQ(r.height, 1);
    minX = std::min(minX, r.x);
    minY = std::min(minY, r.y);
    maxX = std::max(maxX, r.x);
    maxY = std::max(maxY, r.y);
  }
  EXPECT_EQ(minX, (20 - 33) / 2) << "x is centered on the 20 px axis, so it goes negative";
  EXPECT_EQ(minY, (300 - 33) / 2);
  EXPECT_LE(maxX - minX, 32);
  EXPECT_LE(maxY - minY, 32);
}

TEST_F(ReaderHelpersTest, QrOversizedPayloadIsTruncatedAtUtf8BoundaryAndStillRenders) {
  GfxRenderer renderer;
  // 2952 ASCII bytes followed by a 2-byte sequence straddling the 2953-byte cap.
  std::string payload(2952, 'a');
  payload += "\xC3\xA9";
  payload += std::string(100, 'b');
  QrUtils::drawQrCode(renderer, Rect(0, 0, 400, 400), payload);
  ASSERT_FALSE(renderer.rects.empty()) << "truncated payload must still encode";
  EXPECT_EQ(renderer.rects.front().width, 2) << "version 40 (177 modules) at 400 px";
}

TEST_F(ReaderHelpersTest, QrEmptyPayloadStillRendersAVersion4Code) {
  GfxRenderer renderer;
  QrUtils::drawQrCode(renderer, Rect(0, 0, 100, 100), "");
  uint8_t size = 0;
  const size_t expected = referenceModuleCount("", 4, size);
  ASSERT_GT(expected, 0u) << "an empty payload still encodes: the comparison below must not be 0 == 0";
  EXPECT_EQ(size, 33);
  EXPECT_EQ(renderer.rects.size(), expected);
}

}  // namespace
