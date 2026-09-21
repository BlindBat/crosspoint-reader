// Host suite for the TXT reader (FR-096..FR-098).
//
// Part 1 drives lib/Txt/TxtPageIndex (pagination, index.bin v3, progress.bin)
// through its ContentReader / TextMeasurer / ByteReader interfaces with an
// in-memory file and a 10 px-per-code-point width oracle, so every wrap
// decision is arithmetic rather than font-dependent.
//
// Part 2 compiles the real lib/Txt/Txt.cpp against stdio-backed HAL stubs in a
// temporary directory: title stripping, cache path, load/readContent, cover
// discovery order and cover.bmp generation (BMP copy, JPEG convert, PNG reject).
//
// Part 3 drives the production ReaderProgressGuard (the write guard the TXT,
// XTC and FB2 readers call from saveProgress()) against the same stubs, with
// the stub counting every file opened for writing.

#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <PlatformHost.h>
#include <ReaderProgressGuard.h>
#include <Txt.h>
#include <TxtPageIndex.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "AllocCounter.h"

namespace {

using namespace TxtPageIndex;

// ---------------------------------------------------------------------------
// Part 1 fakes
// ---------------------------------------------------------------------------

// 10 px per UTF-8 code point; continuation bytes are free.
class CodePointMeasurer final : public TextMeasurer {
 public:
  int prepareCalls = 0;
  std::string lastPrepared;

  void prepare(const char* chunkText) override {
    prepareCalls++;
    lastPrepared = chunkText;
  }
  int advanceX(const char* text) override {
    int codePoints = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
      if ((*p & 0xC0) != 0x80) codePoints++;
    }
    return codePoints * 10;
  }
};

// 10 px per byte, so invalid UTF-8 still has a measurable width.
class ByteMeasurer final : public TextMeasurer {
 public:
  int advanceX(const char* text) override { return static_cast<int>(std::strlen(text)) * 10; }
};

// In-memory stand-in for Txt::readContent: a seek past EOF fails, a read of
// zero bytes fails, and one offset can be made to fail on demand.
class MemoryContent final : public ContentReader {
 public:
  std::string data;
  size_t failAtOffset = SIZE_MAX;
  int reads = 0;

  bool readContent(uint8_t* buffer, const size_t offset, const size_t length) override {
    reads++;
    if (offset == failAtOffset) return false;
    if (offset > data.size()) return false;
    const size_t n = std::min(length, data.size() - offset);
    std::memcpy(buffer, data.data() + offset, n);
    return n > 0;
  }
};

class MemoryBytes final : public ByteReader, public ByteWriter {
 public:
  std::vector<uint8_t> bytes;
  size_t pos = 0;

  explicit MemoryBytes(std::vector<uint8_t> b = {}) : bytes(std::move(b)) {}

  size_t read(void* buffer, const size_t count) override {
    const size_t n = std::min(count, bytes.size() - pos);
    if (n == 0) return 0;  // bytes.data() may be null when empty; memcpy's pointers are nonnull
    std::memcpy(buffer, bytes.data() + pos, n);
    pos += n;
    return n;
  }
  size_t size() override { return bytes.size(); }
  size_t write(const void* buffer, const size_t count) override {
    const auto* p = static_cast<const uint8_t*>(buffer);
    bytes.insert(bytes.end(), p, p + count);
    return count;
  }
};

constexpr int LINE_PX = 100;  // 10 code points per line

Layout layoutOf(const int linesPerPage) { return {LINE_PX, linesPerPage}; }

struct PageResult {
  bool ok = false;
  std::vector<std::string> lines;
  size_t next = 0;
};

PageResult loadPage(const std::string& text, const size_t offset, const int linesPerPage,
                    CodePointMeasurer* measurerOut = nullptr) {
  MemoryContent content;
  content.data = text;
  CodePointMeasurer measurer;
  PageResult r;
  r.ok = loadPageAtOffset(content, text.size(), offset, layoutOf(linesPerPage), measurer, r.lines, r.next);
  if (measurerOut) *measurerOut = measurer;
  return r;
}

std::string repeat(const std::string& s, const int n) {
  std::string out;
  out.reserve(s.size() * n);
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

// ---------------------------------------------------------------------------
// Part 1: pagination
// ---------------------------------------------------------------------------

TEST(TxtPagination, EofOffsetLoadsNothing) {
  const auto r = loadPage("abc\n", 4, 5);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.lines.empty());
}

TEST(TxtPagination, EmptyFileLoadsNothing) {
  const auto r = loadPage("", 0, 5);
  EXPECT_FALSE(r.ok);
}

TEST(TxtPagination, SingleLineWithLf) {
  const auto r = loadPage("hello\n", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"hello"}));
  EXPECT_EQ(r.next, 6u);
}

TEST(TxtPagination, LastLineWithoutLfIsComplete) {
  const auto r = loadPage("abc", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"abc"}));
  EXPECT_EQ(r.next, 3u);
}

TEST(TxtPagination, TrailingCrIsStrippedOnly) {
  const auto r = loadPage("ab\r\ncd\r\na\rb\n", 0, 5);
  ASSERT_TRUE(r.ok);
  // A CR that is not immediately before the LF is ordinary content.
  EXPECT_EQ(r.lines, (std::vector<std::string>{"ab", "cd", "a\rb"}));
  EXPECT_EQ(r.next, 12u);
}

TEST(TxtPagination, EmptyLinesArePreserved) {
  const auto r = loadPage("a\n\n\nb\n", 0, 10);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"a", "", "", "b"}));
  EXPECT_EQ(r.next, 6u);
}

TEST(TxtPagination, LinesPerPageCapsThePage) {
  const auto r = loadPage("l1\nl2\nl3\nl4\n", 0, 2);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"l1", "l2"}));
  EXPECT_EQ(r.next, 6u);  // start of "l3"
}

TEST(TxtPagination, WrapsAtLastSpaceAndSkipsIt) {
  const auto r = loadPage("hello world foo\n", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"hello", "world foo"}));
  EXPECT_EQ(r.next, 16u);
}

TEST(TxtPagination, WrapsAtCharacterWhenNoSpaceFits) {
  const auto r = loadPage("abcdefghijklmno\n", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"abcdefghij", "klmno"}));
}

TEST(TxtPagination, OverlongWordBeforeSpaceBreaksAtCharacter) {
  const auto r = loadPage("abcdefghijklmnop q\n", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"abcdefghij", "klmnop q"}));
}

TEST(TxtPagination, WrapNeverSplitsUtf8Sequence) {
  const std::string a = "\xC3\xA4";  // U+00E4, 2 bytes
  const auto r = loadPage(repeat(a, 12) + "\n", 0, 5);
  ASSERT_TRUE(r.ok);
  ASSERT_EQ(r.lines.size(), 2u);
  EXPECT_EQ(r.lines[0], repeat(a, 10));
  EXPECT_EQ(r.lines[1], repeat(a, 2));
}

TEST(TxtPagination, LeadingSpaceOnlyLineBreaksAtCharacterBoundary) {
  // rfind(' ') hitting index 0 is not a usable break point; the loop falls
  // through to the character-boundary path.
  const auto r = loadPage(" abcdefghijkl\n", 0, 5);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{" abcdefghi", "jkl"}));
}

TEST(TxtPagination, LongLineSplitAcrossPagesResumesMidLine) {
  const std::string text = "abcdefghijklmnopqrstuvwxy\nz\n";  // 25 chars then "z"
  const auto p0 = loadPage(text, 0, 2);
  ASSERT_TRUE(p0.ok);
  EXPECT_EQ(p0.lines, (std::vector<std::string>{"abcdefghij", "klmnopqrst"}));
  EXPECT_EQ(p0.next, 20u);

  const auto p1 = loadPage(text, p0.next, 2);
  ASSERT_TRUE(p1.ok);
  EXPECT_EQ(p1.lines, (std::vector<std::string>{"uvwxy", "z"}));
  EXPECT_EQ(p1.next, text.size());
}

TEST(TxtPagination, ChunkTextIsPreparedNulTerminated) {
  CodePointMeasurer m;
  const auto r = loadPage("abc\ndef\n", 0, 5, &m);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(m.prepareCalls, 1);
  EXPECT_EQ(m.lastPrepared, "abc\ndef\n");
}

TEST(TxtPagination, ReadFailureLoadsNothing) {
  MemoryContent content;
  content.data = "abc\n";
  content.failAtOffset = 0;
  CodePointMeasurer measurer;
  std::vector<std::string> lines{"stale"};
  size_t next = 0;
  EXPECT_FALSE(loadPageAtOffset(content, content.data.size(), 0, layoutOf(5), measurer, lines, next));
  EXPECT_TRUE(lines.empty());
}

TEST(TxtPagination, IncompleteLineAtChunkEndIsDeferredToNextPage) {
  // 900 lines of "123456789\n": the 8 KB chunk ends two bytes into line 820.
  const std::string text = repeat("123456789\n", 900);
  ASSERT_EQ(text.size(), 9000u);
  const auto p0 = loadPage(text, 0, 1000);
  ASSERT_TRUE(p0.ok);
  EXPECT_EQ(p0.lines.size(), 819u);
  EXPECT_EQ(p0.next, 8190u);

  const auto p1 = loadPage(text, p0.next, 1000);
  ASSERT_TRUE(p1.ok);
  EXPECT_EQ(p1.lines.size(), 81u);
  EXPECT_EQ(p1.next, 9000u);
}

TEST(TxtPagination, IncompleteLineThatIsTheOnlyLineIsStillLaidOut) {
  // No LF anywhere: the chunk is one incomplete line, laid out from the start
  // rather than deferred (deferring would never make progress).
  const std::string text = repeat("aaaa ", 1800);  // 9000 bytes
  const auto r = loadPage(text, 0, 3);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.lines, (std::vector<std::string>{"aaaa aaaa", "aaaa aaaa", "aaaa aaaa"}));
  EXPECT_EQ(r.next, 30u);
}

TEST(TxtPagination, ChunkReadIsCappedAtEightKb) {
  // Assert the chunk handed to prepare() is CHUNK_SIZE bytes, not the file.
  CodePointMeasurer m;
  const std::string text = repeat("x\n", 5000);  // 10000 bytes
  const auto r = loadPage(text, 0, 5, &m);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(CHUNK_SIZE, 8192u);
  EXPECT_EQ(m.lastPrepared.size(), CHUNK_SIZE);
}

TEST(TxtPagination, OffsetFarPastEofLoadsNothing) {
  const auto r = loadPage("abc\n", 4000, 5);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.lines.empty());
}

TEST(TxtPagination, ZeroLinesPerPageProducesNoPage) {
  // The activity clamps linesPerPage to >= 1; the unit itself simply lays out
  // nothing and reports failure, which is what stops buildPageIndex().
  MemoryContent content;
  content.data = "abc\n";
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 42;
  EXPECT_FALSE(loadPageAtOffset(content, content.data.size(), 0, layoutOf(0), measurer, lines, next));
  EXPECT_TRUE(lines.empty());
  EXPECT_EQ(next, 0u);
}

TEST(TxtPagination, ZeroViewportWidthEmitsOneCharacterPerLine) {
  MemoryContent content;
  content.data = "abc\n";
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(loadPageAtOffset(content, content.data.size(), 0, {0, 5}, measurer, lines, next));
  EXPECT_EQ(lines, (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(next, 4u);
}

TEST(TxtPagination, NegativeViewportWidthBehavesLikeZero) {
  MemoryContent content;
  content.data = "ab\n";
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(loadPageAtOffset(content, content.data.size(), 0, {-100, 5}, measurer, lines, next));
  EXPECT_EQ(lines, (std::vector<std::string>{"a", "b"}));
}

// Pinned: width is measured through a C string, so an embedded NUL hides the
// rest of the line from the oracle and the line is emitted unwrapped.
TEST(TxtPagination, EmbeddedNulHidesTheRestOfTheLineFromTheWidthOracle) {
  std::string text("ab", 2);
  text.push_back('\0');
  text += "cdefghijklmnopqrstuvwxyz";
  text.push_back('\n');
  MemoryContent content;
  content.data = text;
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(loadPageAtOffset(content, text.size(), 0, layoutOf(5), measurer, lines, next));
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].size(), text.size() - 1);  // whole line, NUL included
  EXPECT_EQ(next, text.size());
}

// Pinned: the UTF-8 back-off walks over continuation bytes; a run of lone
// continuation bytes has no lead byte to stop at, so breakPos reaches 0 and the
// forced minimum of one byte splits the (already invalid) sequence.
TEST(TxtPagination, LoneUtf8ContinuationBytesFallBackToOneBytePerLine) {
  const std::string text = repeat("\x80", 12) + "\n";
  MemoryContent content;
  content.data = text;
  ByteMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(loadPageAtOffset(content, text.size(), 0, layoutOf(4), measurer, lines, next));
  // One forced byte per over-wide attempt until the remainder finally fits.
  EXPECT_EQ(lines, (std::vector<std::string>{"\x80", "\x80", repeat("\x80", 10)}));
  EXPECT_EQ(next, text.size());
}

TEST(TxtPagination, TruncatedUtf8SequenceAtLineEndIsKeptWhole) {
  // "ä" x 5 then a lead byte with no continuation: measured at 10 px per byte
  // the line wraps at a real boundary and the dangling lead byte survives.
  const std::string text = repeat("\xC3\xA4", 5) + "\xC3" + "\n";
  MemoryContent content;
  content.data = text;
  ByteMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(loadPageAtOffset(content, text.size(), 0, layoutOf(5), measurer, lines, next));
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], repeat("\xC3\xA4", 5));
  EXPECT_EQ(lines[1], "\xC3");
  EXPECT_EQ(next, text.size());
}

// layoutPage() takes the chunk and the file size separately; when they disagree
// (a file truncated under the reader) the next offset is clamped to the file.
TEST(TxtPagination, NextOffsetIsClampedToFileSize) {
  const char chunk[] = "abcdefgh";
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  ASSERT_TRUE(layoutPage(reinterpret_cast<const uint8_t*>(chunk), 8, 0, 5, layoutOf(2), measurer, lines, next));
  EXPECT_EQ(lines, (std::vector<std::string>{"abcdefgh"}));
  EXPECT_EQ(next, 5u);
}

TEST(TxtBuildIndex, EmptyFileHasOnePageStartingAtZero) {
  MemoryContent content;
  CodePointMeasurer measurer;
  std::vector<size_t> offsets{7, 8};
  EXPECT_EQ(buildPageIndex(content, 0, layoutOf(5), measurer, offsets), 1);
  EXPECT_EQ(offsets, (std::vector<size_t>{0}));
  EXPECT_EQ(content.reads, 0);
}

TEST(TxtBuildIndex, PageStartsChainThroughTheWholeFile) {
  const std::string text = "hello world foo\r\n\nabcdefghijklmnopqrstuvwxyz\n" + repeat("line\n", 7) + "tail";
  MemoryContent content;
  content.data = text;
  CodePointMeasurer measurer;
  std::vector<size_t> offsets;
  const int pages = buildPageIndex(content, text.size(), layoutOf(3), measurer, offsets);
  ASSERT_EQ(pages, static_cast<int>(offsets.size()));
  ASSERT_GT(pages, 1);
  EXPECT_EQ(offsets.front(), 0u);
  for (size_t i = 0; i < offsets.size(); ++i) {
    std::vector<std::string> lines;
    size_t next = 0;
    ASSERT_TRUE(loadPageAtOffset(content, text.size(), offsets[i], layoutOf(3), measurer, lines, next));
    ASSERT_LE(lines.size(), 3u);
    const size_t expected = (i + 1 < offsets.size()) ? offsets[i + 1] : text.size();
    EXPECT_EQ(next, expected) << "page " << i;
  }
}

TEST(TxtBuildIndex, ChunkBoundaryPagesAreContiguous) {
  const std::string text = repeat("123456789\n", 900);
  MemoryContent content;
  content.data = text;
  CodePointMeasurer measurer;
  std::vector<size_t> offsets;
  EXPECT_EQ(buildPageIndex(content, text.size(), layoutOf(1000), measurer, offsets), 2);
  EXPECT_EQ(offsets, (std::vector<size_t>{0, 8190}));
}

TEST(TxtBuildIndex, YieldsEveryTwentyPages) {
  platform_host::resetCounters();
  const std::string text = repeat("L\n", 90);  // 45 pages of 2 lines
  MemoryContent content;
  content.data = text;
  CodePointMeasurer measurer;
  std::vector<size_t> offsets;
  EXPECT_EQ(buildPageIndex(content, text.size(), layoutOf(2), measurer, offsets), 45);
  EXPECT_EQ(platform_host::yieldCount(), 2u);
}

TEST(TxtBuildIndex, StopsAtFirstUnreadablePage) {
  const std::string text = repeat("L\n", 10);  // 5 pages of 2 lines
  MemoryContent content;
  content.data = text;
  content.failAtOffset = 4;  // page 1
  CodePointMeasurer measurer;
  std::vector<size_t> offsets;
  // Pinned: the unreadable page keeps its slot; the index simply ends there.
  EXPECT_EQ(buildPageIndex(content, text.size(), layoutOf(2), measurer, offsets), 2);
  EXPECT_EQ(offsets, (std::vector<size_t>{0, 4}));
}

// ---------------------------------------------------------------------------
// Part 1: index.bin
// ---------------------------------------------------------------------------

constexpr size_t INDEX_HEADER_SIZE = 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + 4;  // through numPages
static_assert(INDEX_HEADER_SIZE == INDEX_HEADER_BYTES, "index.bin header size drifted from the contract");
static_assert(INDEX_ENTRY_BYTES == 4, "index.bin page starts are u32 records");

CacheKey referenceKey() {
  CacheKey k;
  k.fileSize = 1234;
  k.viewportWidth = 400;
  k.linesPerPage = 25;
  k.fontId = 7;
  k.screenMargin = 10;
  k.paragraphAlignment = 2;
  return k;
}

// The loader checks page starts against the text they index, so a key whose
// fileSize covers the offsets under test.
CacheKey keyForText(const uint32_t textBytes) {
  CacheKey k = referenceKey();
  k.fileSize = textBytes;
  return k;
}

void putLE32(std::vector<uint8_t>& b, const uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

struct IndexBinBuilder {
  uint32_t magic = CACHE_MAGIC;
  uint8_t version = CACHE_VERSION;
  CacheKey key = referenceKey();
  uint32_t numPages = 0;
  std::vector<uint32_t> offsets;

  std::vector<uint8_t> build() const {
    std::vector<uint8_t> b;
    putLE32(b, magic);
    b.push_back(version);
    putLE32(b, key.fileSize);
    putLE32(b, static_cast<uint32_t>(key.viewportWidth));
    putLE32(b, static_cast<uint32_t>(key.linesPerPage));
    putLE32(b, static_cast<uint32_t>(key.fontId));
    putLE32(b, static_cast<uint32_t>(key.screenMargin));
    b.push_back(key.paragraphAlignment);
    putLE32(b, numPages);
    for (const uint32_t o : offsets) putLE32(b, o);
    return b;
  }
};

IndexBinBuilder validIndex() {
  IndexBinBuilder ib;
  ib.offsets = {0, 120, 250, 377};
  ib.numPages = static_cast<uint32_t>(ib.offsets.size());
  return ib;
}

TEST(TxtIndexBin, SaveWritesContractLayout) {
  MemoryBytes out;
  savePageIndexCache(out, referenceKey(), {0, 120, 250, 377});
  EXPECT_EQ(out.bytes, validIndex().build());
  EXPECT_EQ(out.bytes.size(), INDEX_HEADER_SIZE + 4 * 4);
  // Magic is "TXTI" little-endian, version 3.
  EXPECT_EQ(out.bytes[0], 0x49);
  EXPECT_EQ(out.bytes[1], 0x54);
  EXPECT_EQ(out.bytes[2], 0x58);
  EXPECT_EQ(out.bytes[3], 0x54);
  EXPECT_EQ(out.bytes[4], 3);
}

TEST(TxtIndexBin, RoundTrip) {
  const std::vector<size_t> offsets{0, 8190, 16380, 20000};
  const CacheKey key = keyForText(24000);
  MemoryBytes io;
  savePageIndexCache(io, key, offsets);
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(io, key, loaded));
  EXPECT_EQ(loaded, offsets);
}

// The index a real pagination run produces must survive a save/load cycle.
TEST(TxtIndexBin, BuiltIndexRoundTrips) {
  MemoryContent content;
  for (int i = 0; i < 40; ++i) content.data += "line of text here\n";
  CodePointMeasurer measurer;
  std::vector<size_t> built;
  ASSERT_GT(buildPageIndex(content, content.data.size(), layoutOf(3), measurer, built), 1);

  const CacheKey key = keyForText(static_cast<uint32_t>(content.data.size()));
  MemoryBytes io;
  savePageIndexCache(io, key, built);
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(io, key, loaded));
  EXPECT_EQ(loaded, built);
}

TEST(TxtIndexBin, LoadsValidFile) {
  MemoryBytes in(validIndex().build());
  std::vector<size_t> loaded{99};
  ASSERT_TRUE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{0, 120, 250, 377}));
}

// An empty TXT has no pages to index, so a zero count is only legal there.
TEST(TxtIndexBin, ZeroPagesForEmptyTextLoadsEmptyIndex) {
  IndexBinBuilder ib;
  ib.key = keyForText(0);
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  ASSERT_TRUE(loadPageIndexCache(in, keyForText(0), loaded));
  EXPECT_TRUE(loaded.empty());
}

// The single page buildPageIndex() emits for an empty TXT round-trips.
TEST(TxtIndexBin, SinglePageAtZeroForEmptyTextLoads) {
  IndexBinBuilder ib;
  ib.key = keyForText(0);
  ib.offsets = {0};
  ib.numPages = 1;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(in, keyForText(0), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{0}));
}

// Changed at T141: a book with text but no pages is a corrupt index, not an
// empty one. Before it loaded as zero pages and the reader showed nothing.
TEST(TxtIndexBin, ZeroPagesWithNonEmptyTextRejects) {
  IndexBinBuilder ib;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST(TxtIndexBin, WrongMagicRejectsAndLeavesOffsetsUntouched) {
  auto ib = validIndex();
  ib.magic = 0x54585448;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST(TxtIndexBin, WrongVersionRejects) {
  auto ib = validIndex();
  ib.version = 2;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded;
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  ib.version = 4;
  MemoryBytes newer(ib.build());
  EXPECT_FALSE(loadPageIndexCache(newer, referenceKey(), loaded));
}

struct KeyMismatch {
  const char* name;
  void (*mutate)(CacheKey&);
};

class TxtIndexBinKey : public ::testing::TestWithParam<KeyMismatch> {};

TEST_P(TxtIndexBinKey, MismatchRejectsAndLeavesOffsetsUntouched) {
  CacheKey key = referenceKey();
  GetParam().mutate(key);
  MemoryBytes in(validIndex().build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, key, loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

INSTANTIATE_TEST_SUITE_P(Fields, TxtIndexBinKey,
                         ::testing::Values(KeyMismatch{"fileSize", [](CacheKey& k) { k.fileSize++; }},
                                           KeyMismatch{"viewportWidth", [](CacheKey& k) { k.viewportWidth++; }},
                                           KeyMismatch{"linesPerPage", [](CacheKey& k) { k.linesPerPage--; }},
                                           KeyMismatch{"fontId", [](CacheKey& k) { k.fontId = 8; }},
                                           KeyMismatch{"screenMargin", [](CacheKey& k) { k.screenMargin = 0; }},
                                           KeyMismatch{"paragraphAlignment",
                                                       [](CacheKey& k) { k.paragraphAlignment = 1; }}),
                         [](const ::testing::TestParamInfo<KeyMismatch>& info) { return info.param.name; });

TEST(TxtIndexBin, HeaderTruncatedBeforeNumPagesRejects) {
  const auto full = validIndex().build();
  for (size_t len = 0; len < INDEX_HEADER_SIZE - 4; ++len) {
    MemoryBytes in(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<long>(len)));
    std::vector<size_t> loaded{99};
    EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded)) << "truncated at " << len;
    EXPECT_EQ(loaded, (std::vector<size_t>{99})) << "truncated at " << len;
  }
}

TEST(TxtIndexBin, EmptyFileRejects) {
  MemoryBytes in;
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// Changed at T141: the page count field must be read in full. Before a file
// that stopped right in front of it loaded as an empty index.
TEST(TxtIndexBin, TruncatedAtNumPagesRejects) {
  const auto full = validIndex().build();
  MemoryBytes in(std::vector<uint8_t>(full.begin(), full.begin() + static_cast<long>(INDEX_HEADER_SIZE - 4)));
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// Changed at T141: a count larger than the payload is rejected. Before the
// missing offsets read back as 0.
TEST(TxtIndexBin, NumPagesBeyondPayloadRejects) {
  auto ib = validIndex();
  ib.numPages = 8;  // only 4 offsets follow
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// Changed at T141: the count is bounded by the records the file can hold, so a
// lying header no longer drives an unbounded allocation. Before, a 4-byte field
// asked for numPages * sizeof(size_t) bytes of the ~380 KB device heap.
TEST(TxtIndexBin, NumPagesLargerThanTheFileCouldHoldRejects) {
  auto ib = validIndex();
  ib.offsets.clear();
  ib.numPages = 200000;  // header claims 200k pages, zero bytes of payload
  MemoryBytes in(ib.build());
  ASSERT_EQ(in.size(), INDEX_HEADER_SIZE);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// The payload is exactly (size - header) / 4 records; one more is a lie.
TEST(TxtIndexBin, NumPagesExactlyFillingThePayloadLoads) {
  auto ib = validIndex();
  MemoryBytes in(ib.build());
  ASSERT_EQ(in.size(), INDEX_HEADER_SIZE + 4 * INDEX_ENTRY_BYTES);
  std::vector<size_t> loaded;
  EXPECT_TRUE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded.size(), 4u);
}

// Changed at T141: a count above the number of text bytes cannot be honest --
// every page starts at a distinct byte -- so it is rejected before reserving.
TEST(TxtIndexBin, NumPagesAboveTheTextSizeRejects) {
  auto ib = validIndex();
  ib.key = keyForText(3);
  ib.offsets = {0, 1, 2, 3, 4, 5};
  ib.numPages = 6;  // the payload holds all six, but a 3-byte text cannot
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, keyForText(3), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// Rejecting a lying count is not the same as rejecting it BEFORE sizing the
// offsets vector: the per-entry short-read check returns false either way, so
// only the allocation that must not happen pins the ordering. Here the count is
// legal for the text (200000 <= fileSize) and the payload bound alone can
// reject it. Sizing first would ask for 200000 * sizeof(size_t) -- 800 KB on
// the ~380 KB device heap, where -fno-exceptions turns the failure into abort().
TEST(TxtIndexBin, ACountBeyondThePayloadIsRejectedBeforeTheOffsetsAreSized) {
  auto ib = validIndex();
  ib.key = keyForText(200000);
  ib.offsets.clear();
  ib.numPages = 200000;  // header claims 200k pages, zero bytes of payload
  MemoryBytes in(ib.build());
  ASSERT_EQ(in.size(), INDEX_HEADER_SIZE);

  const CacheKey key = keyForText(200000);
  std::vector<size_t> loaded{99};
  bool ok = true;
  size_t allocations = 0;
  {
    alloc_counter::CountingScope scope;
    ok = loadPageIndexCache(in, key, loaded);
    allocations = scope.count();
  }
  EXPECT_FALSE(ok);
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
  EXPECT_EQ(allocations, 0u);
}

// The mirror case: the payload really does hold every record it claims, so the
// text-size bound is the only one that can reject the count, and it too must
// run before the vector is sized.
TEST(TxtIndexBin, ACountTheTextCannotHoldIsRejectedBeforeTheOffsetsAreSized) {
  auto ib = validIndex();
  ib.key = keyForText(100);  // a 100-byte text cannot start 20000 pages
  ib.offsets.resize(20000);
  for (uint32_t i = 0; i < 20000; ++i) ib.offsets[i] = i;
  ib.numPages = 20000;
  MemoryBytes in(ib.build());
  ASSERT_EQ(in.size(), INDEX_HEADER_SIZE + 20000 * INDEX_ENTRY_BYTES);

  const CacheKey key = keyForText(100);
  std::vector<size_t> loaded{99};
  bool ok = true;
  size_t allocations = 0;
  {
    alloc_counter::CountingScope scope;
    ok = loadPageIndexCache(in, key, loaded);
    allocations = scope.count();
  }
  EXPECT_FALSE(ok);
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
  EXPECT_EQ(allocations, 0u);
}

// Changed at T141: a half-written entry is rejected. Before, the bytes that
// were not read stayed at their initial zero and silently corrupted the entry.
TEST(TxtIndexBin, OffsetTruncatedMidEntryRejects) {
  auto bytes = validIndex().build();
  bytes.resize(INDEX_HEADER_SIZE + 4 + 2);  // one whole offset, then two bytes
  bytes[INDEX_HEADER_SIZE + 4] = 0x11;
  bytes[INDEX_HEADER_SIZE + 5] = 0x22;
  MemoryBytes in(bytes);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// A file whose size() promises more than read() delivers (an SD read error part
// way through) is caught by the per-entry check, not by the count bound.
TEST(TxtIndexBin, ShortReadInsideThePayloadRejects) {
  class ShortReadBytes final : public ByteReader {
   public:
    std::vector<uint8_t> bytes;
    size_t deliver;  // bytes read() will hand out before it starts failing
    size_t pos = 0;
    ShortReadBytes(std::vector<uint8_t> b, const size_t deliver) : bytes(std::move(b)), deliver(deliver) {}
    size_t read(void* buffer, const size_t count) override {
      const size_t n = std::min(count, pos >= deliver ? 0 : deliver - pos);
      std::memcpy(buffer, bytes.data() + pos, n);
      pos += n;
      return n;
    }
    size_t size() override { return bytes.size(); }
  };

  ShortReadBytes in(validIndex().build(), INDEX_HEADER_SIZE + 2 * INDEX_ENTRY_BYTES);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// Changed at T141: page starts are validated against the text they index.
// Before they were accepted and the reader only survived because
// loadPageAtOffset() refuses an offset at or past EOF.
TEST(TxtIndexBin, OffsetsPastEofReject) {
  auto ib = validIndex();
  ib.key = keyForText(16);
  ib.offsets = {0, 9999};
  ib.numPages = 2;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, keyForText(16), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));

  // The renderer-side guard that used to absorb this is still in place.
  MemoryContent content;
  content.data = "0123456789abcdef";
  CodePointMeasurer measurer;
  std::vector<std::string> lines;
  size_t next = 0;
  EXPECT_FALSE(loadPageAtOffset(content, content.data.size(), 9999, layoutOf(5), measurer, lines, next));
  EXPECT_TRUE(lines.empty());
}

// The last byte of the text is a legal page start; one past it is not.
TEST(TxtIndexBin, OffsetAtTheLastTextByteLoadsButOneMoreRejects) {
  auto ib = validIndex();
  ib.key = keyForText(16);
  ib.offsets = {0, 15};
  ib.numPages = 2;
  MemoryBytes ok(ib.build());
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(ok, keyForText(16), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{0, 15}));

  ib.offsets = {0, 16};
  MemoryBytes atEof(ib.build());
  std::vector<size_t> rejected{99};
  EXPECT_FALSE(loadPageIndexCache(atEof, keyForText(16), rejected));
  EXPECT_EQ(rejected, (std::vector<size_t>{99}));
}

// Changed at T141: buildPageIndex() emits strictly increasing starts from 0, so
// a repeat, a step backwards or a non-zero first page is corrupt.
TEST(TxtIndexBin, NonMonotonicOffsetsReject) {
  auto ib = validIndex();
  ib.offsets = {0, 250, 120, 377};
  MemoryBytes backwards(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(backwards, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));

  ib.offsets = {0, 120, 120, 377};
  MemoryBytes repeated(ib.build());
  EXPECT_FALSE(loadPageIndexCache(repeated, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST(TxtIndexBin, FirstOffsetOtherThanZeroRejects) {
  auto ib = validIndex();
  ib.offsets = {1, 120, 250, 377};
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST(TxtIndexBin, TrailingBytesAfterOffsetsAreIgnored) {
  auto bytes = validIndex().build();
  bytes.insert(bytes.end(), {0xDE, 0xAD, 0xBE, 0xEF});
  MemoryBytes in(bytes);
  std::vector<size_t> loaded;
  EXPECT_TRUE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded.size(), 4u);
  // The loader stops after numPages entries instead of draining the file, so
  // the trailing bytes are never consumed.
  EXPECT_EQ(in.pos, INDEX_HEADER_SIZE + 4 * 4);
  EXPECT_EQ(in.size(), in.pos + 4);
}

// A stored offset is a u32; widening it must not sign-extend. The 2 GB start
// is in range for the 4 GB-1 text this key describes.
TEST(TxtIndexBin, LargeOffsetsAreWidenedUnsigned) {
  auto ib = validIndex();
  ib.key = keyForText(0xFFFFFFFFu);
  ib.offsets = {0, 0x80000000u, 0xFFFFFFFEu};
  ib.numPages = 3;
  MemoryBytes in(ib.build());
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(in, keyForText(0xFFFFFFFFu), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{0u, 2147483648u, 4294967294u}));
}

// Changed at T141: writes are still fire-and-forget, so a sink that runs out of
// room (a full SD card) leaves a truncated index.bin -- but the loader now
// rejects it and the caller rebuilds. Before, the offsets it never got read
// back as 0 and the book paginated to the wrong places.
TEST(TxtIndexBin, SaveIgnoresShortWritesAndTheResultNoLongerLoads) {
  class ShortWriter final : public ByteWriter {
   public:
    std::vector<uint8_t> bytes;
    size_t limit;
    explicit ShortWriter(const size_t limit) : limit(limit) {}
    size_t write(const void* buffer, const size_t count) override {
      const size_t room = bytes.size() >= limit ? 0 : limit - bytes.size();
      const size_t n = std::min(count, room);
      const auto* p = static_cast<const uint8_t*>(buffer);
      bytes.insert(bytes.end(), p, p + n);
      return n;
    }
  };

  ShortWriter out(INDEX_HEADER_SIZE + 4);  // room for the header and one offset
  savePageIndexCache(out, referenceKey(), {0, 22, 33, 44});
  EXPECT_EQ(out.bytes.size(), INDEX_HEADER_SIZE + 4);

  MemoryBytes in(out.bytes);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(in, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

// ---------------------------------------------------------------------------
// Part 1: progress.bin
// ---------------------------------------------------------------------------

TEST(TxtProgress, EncodesU16PagePlusTwoZeroBytes) {
  uint8_t out[PROGRESS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
  encodeProgress(0x1234, out);
  EXPECT_EQ(out[0], 0x34);
  EXPECT_EQ(out[1], 0x12);
  EXPECT_EQ(out[2], 0);
  EXPECT_EQ(out[3], 0);
}

TEST(TxtProgress, PageAbove65535WrapsToU16) {
  uint8_t out[PROGRESS_SIZE];
  encodeProgress(65536 + 5, out);
  EXPECT_EQ(decodeProgress(out, 100000), 5);
}

TEST(TxtProgress, RoundTripsWithinRange) {
  for (const int page : {0, 1, 255, 256, 4000}) {
    uint8_t out[PROGRESS_SIZE];
    encodeProgress(page, out);
    EXPECT_EQ(decodeProgress(out, 5000), page);
  }
}

TEST(TxtProgress, ClampsToLastPage) {
  const uint8_t data[PROGRESS_SIZE] = {0xFF, 0xFF, 0, 0};
  EXPECT_EQ(decodeProgress(data, 10), 9);
  const uint8_t exact[PROGRESS_SIZE] = {10, 0, 0, 0};
  EXPECT_EQ(decodeProgress(exact, 10), 9);
}

TEST(TxtProgress, ZeroPagesClampsToZero) {
  const uint8_t data[PROGRESS_SIZE] = {3, 0, 0, 0};
  EXPECT_EQ(decodeProgress(data, 0), 0);
}

TEST(TxtProgress, TrailingBytesAreIgnored) {
  const uint8_t data[PROGRESS_SIZE] = {2, 0, 0xAA, 0xBB};
  EXPECT_EQ(decodeProgress(data, 10), 2);
}

// ---------------------------------------------------------------------------
// Part 2: lib/Txt/Txt.cpp against a temporary directory
// ---------------------------------------------------------------------------

class TxtFixture : public ::testing::Test {
 protected:
  std::string root;

  void SetUp() override {
    std::string tmpl = (std::filesystem::temp_directory_path() / "txtreader-XXXXXX").string();
    ASSERT_NE(::mkdtemp(tmpl.data()), nullptr);
    root = tmpl;
    Storage.root = root;
    JpegToBmpConverterStubState::instance().reset();
  }

  void TearDown() override {
    Storage.root.clear();
    std::filesystem::remove_all(root);
  }

  void writeFile(const std::string& devicePath, const std::string& content) const {
    const std::filesystem::path full = root + devicePath;
    std::filesystem::create_directories(full.parent_path());
    std::FILE* f = std::fopen(full.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), f), content.size());
    std::fclose(f);
  }

  std::string readFile(const std::string& devicePath) const {
    std::FILE* f = std::fopen((root + devicePath).c_str(), "rb");
    if (!f) return "<missing>";
    std::string out;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
  }

  bool exists(const std::string& devicePath) const { return std::filesystem::exists(root + devicePath); }
};

TEST_F(TxtFixture, TitleStripsOnlyTxtExtension) {
  EXPECT_EQ(Txt("/books/My Book.txt", "/.crosspoint").getTitle(), "My Book");
  EXPECT_EQ(Txt("/books/UPPER.TXT", "/.crosspoint").getTitle(), "UPPER");
  EXPECT_EQ(Txt("/books/notes.md", "/.crosspoint").getTitle(), "notes.md");
  EXPECT_EQ(Txt("/books/a.txt.txt", "/.crosspoint").getTitle(), "a.txt");
  EXPECT_EQ(Txt("root.txt", "/.crosspoint").getTitle(), "root");
  EXPECT_EQ(Txt("/books/.txt", "/.crosspoint").getTitle(), "");
}

TEST_F(TxtFixture, CachePathIsHashOfFilePath) {
  const std::string path = "/books/a.txt";
  const Txt txt(path, "/.crosspoint");
  EXPECT_EQ(txt.getCachePath(), "/.crosspoint/txt_" + std::to_string(std::hash<std::string>{}(path)));
  EXPECT_EQ(txt.getCoverBmpPath(), txt.getCachePath() + "/cover.bmp");
  EXPECT_NE(Txt("/books/b.txt", "/.crosspoint").getCachePath(), txt.getCachePath());
}

TEST_F(TxtFixture, LoadFailsForMissingFile) {
  Txt txt("/books/missing.txt", "/.crosspoint");
  EXPECT_FALSE(txt.load());
  EXPECT_EQ(txt.getFileSize(), 0u);
}

TEST_F(TxtFixture, LoadRecordsFileSizeAndIsIdempotent) {
  writeFile("/books/a.txt", "hello\nworld\n");
  Txt txt("/books/a.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());
  EXPECT_EQ(txt.getFileSize(), 12u);
  EXPECT_TRUE(txt.load());
  EXPECT_EQ(txt.getFileSize(), 12u);
}

TEST_F(TxtFixture, ReadContentRequiresLoad) {
  writeFile("/books/a.txt", "hello");
  Txt txt("/books/a.txt", "/.crosspoint");
  uint8_t buf[8] = {};
  EXPECT_FALSE(txt.readContent(buf, 0, 5));
  ASSERT_TRUE(txt.load());
  ASSERT_TRUE(txt.readContent(buf, 1, 3));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), 3), "ell");
}

TEST_F(TxtFixture, ReadContentBeyondEofFails) {
  writeFile("/books/a.txt", "hello");
  Txt txt("/books/a.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());
  uint8_t buf[8] = {};
  EXPECT_FALSE(txt.readContent(buf, 5, 1));  // at EOF: zero bytes read
  EXPECT_FALSE(txt.readContent(buf, 6, 1));  // past EOF: seek fails
  // A short read at the tail still succeeds with what is there.
  EXPECT_TRUE(txt.readContent(buf, 3, 8));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), 2), "lo");
}

TEST_F(TxtFixture, SetupAndClearCacheDir) {
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_TRUE(txt.clearCache());  // nothing to clear
  txt.setupCacheDir();
  EXPECT_TRUE(exists("/.crosspoint"));
  EXPECT_TRUE(exists(txt.getCachePath()));
  writeFile(txt.getCachePath() + "/index.bin", "x");
  EXPECT_TRUE(txt.clearCache());
  EXPECT_FALSE(exists(txt.getCachePath()));
  EXPECT_TRUE(exists("/.crosspoint"));
}

TEST_F(TxtFixture, NoCoverImageYieldsEmptyPath) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/other.jpg", "x");
  EXPECT_EQ(Txt("/books/a.txt", "/.crosspoint").findCoverImage(), "");
}

TEST_F(TxtFixture, BasenameCoverBeatsGenericCover) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/cover.bmp", "x");
  writeFile("/books/a.jpg", "x");
  EXPECT_EQ(Txt("/books/a.txt", "/.crosspoint").findCoverImage(), "/books/a.jpg");
}

TEST_F(TxtFixture, BasenameExtensionOrderIsBmpJpgJpegPng) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.png", "x");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_EQ(txt.findCoverImage(), "/books/a.png");
  writeFile("/books/a.jpeg", "x");
  EXPECT_EQ(txt.findCoverImage(), "/books/a.jpeg");
  writeFile("/books/a.jpg", "x");
  EXPECT_EQ(txt.findCoverImage(), "/books/a.jpg");
  writeFile("/books/a.bmp", "x");
  EXPECT_EQ(txt.findCoverImage(), "/books/a.bmp");
}

TEST_F(TxtFixture, GenericCoverFallsBackInSameFolderOnly) {
  writeFile("/books/sub/a.txt", "x");
  writeFile("/books/cover.jpg", "x");  // parent folder: not considered
  Txt txt("/books/sub/a.txt", "/.crosspoint");
  EXPECT_EQ(txt.findCoverImage(), "");
  writeFile("/books/sub/cover.jpg", "x");
  EXPECT_EQ(txt.findCoverImage(), "/books/sub/cover.jpg");
}

TEST_F(TxtFixture, RootFolderFileLooksInRoot) {
  writeFile("/a.txt", "x");
  writeFile("/cover.bmp", "x");
  // Pinned: a book in the SD root makes `folder` "/" and the candidate path is
  // then built as folder + "/" + name, so the returned path is double-slashed.
  // FatFs skips the duplicate separator, so the cover still opens.
  EXPECT_EQ(Txt("/a.txt", "/.crosspoint").findCoverImage(), "//cover.bmp");
  writeFile("/a.jpg", "x");
  EXPECT_EQ(Txt("/a.txt", "/.crosspoint").findCoverImage(), "//a.jpg");
}

// The uppercase cases assert behaviour, not the returned spelling: on a
// case-insensitive host filesystem the lowercase candidate matches first.
TEST_F(TxtFixture, UppercaseBmpCoverIsCopied) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.BMP", "BMPAYLOAD");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.findCoverImage().empty());
  ASSERT_TRUE(txt.generateCoverBmp());
  EXPECT_EQ(readFile(txt.getCoverBmpPath()), "BMPAYLOAD");
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 0);
}

TEST_F(TxtFixture, UppercaseJpegCoverIsConverted) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.JPG", "JPEGDATA");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.findCoverImage().empty());
  ASSERT_TRUE(txt.generateCoverBmp());
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 1);
  EXPECT_EQ(readFile(txt.getCoverBmpPath()), "JPEGDATA");
}

TEST_F(TxtFixture, UppercasePngCoverIsStillRejected) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.PNG", "PNGDATA");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.findCoverImage().empty());
  EXPECT_FALSE(txt.generateCoverBmp());
  EXPECT_FALSE(exists(txt.getCoverBmpPath()));
}

TEST_F(TxtFixture, MarkdownBookKeepsItsExtensionInTheCoverName) {
  // getTitle() strips only ".txt", so a .md book looks for "notes.md.jpg".
  writeFile("/books/notes.md", "x");
  writeFile("/books/notes.md.jpg", "JPEGDATA");
  EXPECT_EQ(Txt("/books/notes.md", "/.crosspoint").findCoverImage(), "/books/notes.md.jpg");
}

TEST_F(TxtFixture, GenerateCoverFailsWithoutImage) {
  writeFile("/books/a.txt", "x");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.generateCoverBmp());
  EXPECT_FALSE(exists(txt.getCoverBmpPath()));
}

TEST_F(TxtFixture, GenerateCoverCopiesBmpByteForByte) {
  writeFile("/books/a.txt", "x");
  std::string bmp = "BM";
  for (int i = 0; i < 3000; ++i) bmp.push_back(static_cast<char>(i * 7));  // spans several 1 KB reads
  writeFile("/books/a.bmp", bmp);
  Txt txt("/books/a.txt", "/.crosspoint");
  ASSERT_TRUE(txt.generateCoverBmp());
  EXPECT_EQ(readFile(txt.getCoverBmpPath()), bmp);
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 0);
}

TEST_F(TxtFixture, GenerateCoverConvertsJpeg) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/cover.jpeg", "JPEGDATA");
  Txt txt("/books/a.txt", "/.crosspoint");
  ASSERT_TRUE(txt.generateCoverBmp());
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 1);
  EXPECT_EQ(JpegToBmpConverterStubState::instance().lastInputSize, 8u);
  EXPECT_EQ(readFile(txt.getCoverBmpPath()), "JPEGDATA");
}

TEST_F(TxtFixture, FailedJpegConversionRemovesPartialCover) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.jpg", "JPEGDATA");
  JpegToBmpConverterStubState::instance().failNextConversion = true;
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.generateCoverBmp());
  EXPECT_FALSE(exists(txt.getCoverBmpPath()));
}

TEST_F(TxtFixture, GenerateCoverRejectsPng) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.png", "PNGDATA");
  Txt txt("/books/a.txt", "/.crosspoint");
  EXPECT_FALSE(txt.generateCoverBmp());
  EXPECT_FALSE(exists(txt.getCoverBmpPath()));
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 0);
}

// ---------------------------------------------------------------------------
// Part 2: index.bin / progress.bin through a real file handle
//
// Mirrors the HalFile adapter TxtReaderActivity wraps around the cache files,
// so the byte layout is pinned against the handle the device actually uses
// (read() returns int, a short read returns fewer bytes than requested).
// ---------------------------------------------------------------------------

class HalFileBytes final : public ByteReader, public ByteWriter {
  HalFile& file;

 public:
  explicit HalFileBytes(HalFile& file) : file(file) {}
  size_t read(void* buffer, const size_t count) override {
    const int n = file.read(buffer, count);
    return n < 0 ? 0 : static_cast<size_t>(n);
  }
  size_t size() override { return file.size(); }
  size_t write(const void* buffer, const size_t count) override { return file.write(buffer, count); }
};

TEST_F(TxtFixture, IndexBinRoundTripsThroughAFileHandle) {
  const std::vector<size_t> offsets{0, 8190, 16380, 20000};
  const CacheKey key = keyForText(24000);
  const std::string path = "/.crosspoint/txt_1/index.bin";
  std::filesystem::create_directories(root + "/.crosspoint/txt_1");
  {
    HalFile out;
    ASSERT_TRUE(Storage.openFileForWrite("TRS", path, out));
    HalFileBytes bytes(out);
    savePageIndexCache(bytes, key, offsets);
  }
  EXPECT_EQ(readFile(path).size(), INDEX_HEADER_SIZE + 4 * 4);

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  HalFileBytes bytes(in);
  EXPECT_EQ(bytes.size(), INDEX_HEADER_SIZE + 4 * 4);
  std::vector<size_t> loaded;
  ASSERT_TRUE(loadPageIndexCache(bytes, key, loaded));
  EXPECT_EQ(loaded, offsets);
}

// A cache file the size() of which outruns its payload -- an index.bin the
// device truncated on a power loss -- is rejected through the real handle.
TEST_F(TxtFixture, IndexBinTruncatedInsideThePayloadOnDiskIsRejected) {
  auto full = validIndex().build();
  full.resize(INDEX_HEADER_SIZE + 2 * INDEX_ENTRY_BYTES);  // header claims 4
  const std::string path = "/.crosspoint/txt_1/index.bin";
  writeFile(path, std::string(reinterpret_cast<const char*>(full.data()), full.size()));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  HalFileBytes bytes(in);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(bytes, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST_F(TxtFixture, TruncatedIndexBinOnDiskIsRejected) {
  const auto full = validIndex().build();
  const std::string path = "/.crosspoint/txt_1/index.bin";
  writeFile(path, std::string(reinterpret_cast<const char*>(full.data()), 6));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  HalFileBytes bytes(in);
  std::vector<size_t> loaded{99};
  EXPECT_FALSE(loadPageIndexCache(bytes, referenceKey(), loaded));
  EXPECT_EQ(loaded, (std::vector<size_t>{99}));
}

TEST_F(TxtFixture, EmptyIndexBinOnDiskIsRejected) {
  const std::string path = "/.crosspoint/txt_1/index.bin";
  writeFile(path, "");
  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  HalFileBytes bytes(in);
  std::vector<size_t> loaded;
  EXPECT_FALSE(loadPageIndexCache(bytes, referenceKey(), loaded));
}

// TxtReaderActivity::loadProgress() only applies the payload on a full 4-byte
// read, so a truncated progress.bin leaves the page where it was.
TEST_F(TxtFixture, ProgressBinShorterThanFourBytesIsIgnored) {
  const std::string path = "/.crosspoint/txt_1/progress.bin";
  writeFile(path, std::string("\x05\x00", 2));
  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  uint8_t data[PROGRESS_SIZE] = {};
  EXPECT_NE(in.read(data, sizeof(data)), static_cast<int>(sizeof(data)));
  // The partial payload would still decode to a page; the 4-byte read guard in
  // loadProgress() is the only thing that keeps it from being applied.
  EXPECT_EQ(decodeProgress(data, 100), 5);
}

TEST_F(TxtFixture, ProgressBinWithStalePageCountClampsOnLoad) {
  const std::string path = "/.crosspoint/txt_1/progress.bin";
  uint8_t written[PROGRESS_SIZE];
  encodeProgress(9000, written);  // saved when the book had >9000 pages
  writeFile(path, std::string(reinterpret_cast<const char*>(written), PROGRESS_SIZE));

  HalFile in;
  ASSERT_TRUE(Storage.openFileForRead("TRS", path, in));
  uint8_t data[PROGRESS_SIZE] = {};
  ASSERT_EQ(in.read(data, sizeof(data)), static_cast<int>(sizeof(data)));
  EXPECT_EQ(decodeProgress(data, 12), 11);  // index rebuilt with 12 pages
}

TEST_F(TxtFixture, ExistingCoverBmpShortCircuits) {
  writeFile("/books/a.txt", "x");
  writeFile("/books/a.jpg", "JPEGDATA");
  Txt txt("/books/a.txt", "/.crosspoint");
  writeFile(txt.getCoverBmpPath(), "OLD");
  EXPECT_TRUE(txt.generateCoverBmp());
  EXPECT_EQ(readFile(txt.getCoverBmpPath()), "OLD");
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 0);
}

// ---------------------------------------------------------------------------
// Part 3: ReaderProgressGuard (src/activities/reader) -- redundant progress
// writes, counted through the stub.
// ---------------------------------------------------------------------------

class ProgressGuardFixture : public TxtFixture {
 protected:
  std::string cachePath = "/.crosspoint/txt_guard";

  void SetUp() override {
    TxtFixture::SetUp();
    ASSERT_TRUE(Storage.mkdir(cachePath.c_str()));
    Storage.resetCounters();
  }

  void TearDown() override {
    Storage.resetCounters();
    TxtFixture::TearDown();
  }

  // Exactly what TxtReaderActivity::saveProgress() does.
  bool saveTxtProgress(ReaderProgressGuard& guard, const int page) const {
    uint8_t data[PROGRESS_SIZE];
    encodeProgress(page, data);
    return guard.save(cachePath, page, data, sizeof(data));
  }

  int savedPage() const {
    HalFile in;
    EXPECT_TRUE(Storage.openFileForRead("TRS", cachePath + "/progress.bin", in));
    uint8_t data[PROGRESS_SIZE] = {};
    EXPECT_EQ(in.read(data, sizeof(data)), static_cast<int>(sizeof(data)));
    return decodeProgress(data, 10000);
  }
};

TEST_F(ProgressGuardFixture, RepaintingTheSamePageWritesNothingAfterTheFirstSave) {
  ReaderProgressGuard guard;
  EXPECT_TRUE(saveTxtProgress(guard, 7));
  EXPECT_EQ(Storage.writeCount, 1);  // one temp file, renamed into place
  EXPECT_EQ(Storage.writtenPaths[0], cachePath + "/progress.bin.tmp");
  EXPECT_EQ(savedPage(), 7);

  // renderBook() runs on every repaint, not only on a page turn.
  for (int repaint = 0; repaint < 5; repaint++) {
    EXPECT_TRUE(saveTxtProgress(guard, 7));
  }
  EXPECT_EQ(Storage.writeCount, 1);
  EXPECT_EQ(savedPage(), 7);
}

TEST_F(ProgressGuardFixture, EveryPageTurnStillWrites) {
  ReaderProgressGuard guard;
  for (int page = 0; page < 4; page++) {
    EXPECT_TRUE(saveTxtProgress(guard, page));
    EXPECT_TRUE(saveTxtProgress(guard, page));  // repaint of the same page
  }
  EXPECT_EQ(Storage.writeCount, 4);
  EXPECT_EQ(savedPage(), 3);

  // Paging back is a move too.
  EXPECT_TRUE(saveTxtProgress(guard, 2));
  EXPECT_EQ(Storage.writeCount, 5);
  EXPECT_EQ(savedPage(), 2);
}

TEST_F(ProgressGuardFixture, MarkSavedAfterLoadSuppressesTheOpeningRewrite) {
  // loadProgress() read page 3 out of progress.bin; the file already says 3.
  uint8_t data[PROGRESS_SIZE];
  encodeProgress(3, data);
  writeFile(cachePath + "/progress.bin", std::string(reinterpret_cast<const char*>(data), PROGRESS_SIZE));
  Storage.resetCounters();

  ReaderProgressGuard guard;
  guard.markSaved(3);
  EXPECT_TRUE(saveTxtProgress(guard, 3));
  EXPECT_EQ(Storage.writeCount, 0);
  EXPECT_EQ(savedPage(), 3);

  EXPECT_TRUE(saveTxtProgress(guard, 4));
  EXPECT_EQ(Storage.writeCount, 1);
  EXPECT_EQ(savedPage(), 4);
}

TEST_F(ProgressGuardFixture, ForgetReenablesTheWriteAfterACacheClear) {
  ReaderProgressGuard guard;
  EXPECT_TRUE(saveTxtProgress(guard, 5));
  EXPECT_EQ(Storage.writeCount, 1);

  // The reader menu's "delete cache" removes progress.bin and re-saves the
  // position it backed up; the guard must not treat that as unchanged.
  ASSERT_TRUE(Storage.remove((cachePath + "/progress.bin").c_str()));
  guard.forget();
  EXPECT_TRUE(saveTxtProgress(guard, 5));
  EXPECT_EQ(Storage.writeCount, 2);
  EXPECT_EQ(savedPage(), 5);
}

TEST_F(ProgressGuardFixture, AFailedWriteIsRetriedOnTheNextRepaint) {
  ReaderProgressGuard guard;
  uint8_t data[PROGRESS_SIZE];
  encodeProgress(6, data);
  // No cache directory: writeAtomic cannot open the temp file.
  EXPECT_FALSE(guard.save("/.crosspoint/does_not_exist", 6, data, sizeof(data)));
  EXPECT_FALSE(guard.save("/.crosspoint/does_not_exist", 6, data, sizeof(data)));

  // The guard never recorded the failed position, so the real path still writes.
  EXPECT_TRUE(saveTxtProgress(guard, 6));
  EXPECT_EQ(savedPage(), 6);
}

// generateCoverBmp()'s 1KB copy buffer lives on the heap, not in its frame,
// where it cost ~1.1KB -- four times the 256-byte stack budget. HalFile::read()
// samples the deepest stack address the copy loop reaches.
TEST_F(TxtFixture, CoverCopyRunsInASmallStackFrame) {
  if (!halfile_stack_probe::kMeasurementIsReliable) {
    GTEST_SKIP() << "AddressSanitizer pads every frame on the path; the measurement is not the production frame";
  }

  writeFile("/books/a.txt", "x");
  writeFile("/books/a.bmp", std::string(8192, 'B'));
  Txt txt("/books/a.txt", "/.crosspoint");

  const char anchor = 0;
  halfile_stack_probe::reset();
  ASSERT_TRUE(txt.generateCoverBmp());
  const size_t depth = halfile_stack_probe::depthFrom(&anchor);

  ASSERT_GT(depth, 0u) << "read() was never reached; the probe measured nothing";
  EXPECT_LT(depth, 640u);
}

}  // namespace
