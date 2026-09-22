#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "AllocCounter.h"
#include "Fb2.h"
#include "Fb2/Fb2Section.h"
#include "Fb2/Fb2SectionParser.h"
#include "Fb2TestSupport.h"

namespace {

// Collects finished pages behind an Fb2PageCompleteFn (function pointer + context).
struct PageSink {
  std::vector<std::unique_ptr<Page>>* pages;

  Fb2PageCompleteFn fn() { return {&PageSink::append, this}; }

  static void append(void* ctx, std::unique_ptr<Page> page) {
    static_cast<PageSink*>(ctx)->pages->push_back(std::move(page));
  }
};

using fb2test::fileExists;
using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

// Mirrors the private HEADER_SIZE constant in Fb2Section.cpp:
// version(1) + fontId(4) + lineCompression(4) + extraParagraphSpacing(1) +
// paragraphAlignment(1) + viewportWidth(2) + viewportHeight(2) +
// hyphenationEnabled(1) + focusReadingEnabled(1) + pageCount(2) + lutOffset(4).
constexpr uint32_t kHeaderSize = 23;
constexpr uint8_t kSectionFileVersion = 5;

ReaderRenderSpec makeSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 0;
  spec.lineCompression = 1.0f;
  spec.viewportWidth = 400;
  spec.viewportHeight = 64;  // 4 lines of 16 px per page
  return spec;
}

template <typename T>
T readAt(const std::string& bytes, size_t offset) {
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

std::vector<std::string> collectWords(const std::vector<std::unique_ptr<Page>>& pages) {
  std::vector<std::string> words;
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        words.emplace_back(block->wordText(i));
      }
    }
  }
  return words;
}

bool containsWord(const std::vector<std::string>& words, const std::string& needle) {
  for (const auto& word : words) {
    if (word == needle) return true;
  }
  return false;
}

class Fb2SectionCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    book = std::make_shared<Fb2>(fixturePath("styles.fb2"), tmp.path());
    ASSERT_TRUE(book->load());
    ASSERT_EQ(book->getSectionCount(), 2);
  }

  std::string sectionFilePath(int index = 0) const {
    return book->getCachePath() + "/sections/" + std::to_string(index) + ".bin";
  }

  // Build section `index` and return the section object (file left on disk).
  std::unique_ptr<Fb2Section> buildSection(const ReaderRenderSpec& spec, int index = 0) {
    auto section = std::make_unique<Fb2Section>(book, index, renderer);
    if (!section->createSectionFile(spec)) return nullptr;
    return section;
  }

  // Reference pages straight from the parser, bypassing serialization.
  std::vector<std::unique_ptr<Page>> parseReferencePages(const ReaderRenderSpec& spec, int index = 0) {
    std::vector<std::unique_ptr<Page>> pages;
    PageSink sink{&pages};
    const auto& info = book->getSectionInfo(index);
    Fb2SectionParser parser(book->getPath(), info.length, index, renderer, spec, sink.fn());
    if (!parser.parseAndBuildPages()) pages.clear();
    return pages;
  }

  fb2test::TempDir tmp;
  GfxRenderer renderer;
  std::shared_ptr<Fb2> book;
};

TEST_F(Fb2SectionCacheTest, CreateSectionFileWritesSpecStampedHeader) {
  const auto spec = makeSpec();
  auto section = buildSection(spec);
  ASSERT_NE(section, nullptr);
  EXPECT_GT(section->pageCount, 0);

  const std::string bytes = readAll(sectionFilePath());
  ASSERT_GE(bytes.size(), kHeaderSize);
  EXPECT_EQ(static_cast<uint8_t>(bytes[0]), kSectionFileVersion);
  EXPECT_EQ(readAt<int>(bytes, 1), spec.fontId);
  EXPECT_FLOAT_EQ(readAt<float>(bytes, 5), spec.lineCompression);
  EXPECT_EQ(readAt<uint8_t>(bytes, 9), spec.extraParagraphSpacing ? 1 : 0);
  EXPECT_EQ(readAt<uint8_t>(bytes, 10), spec.paragraphAlignment);
  EXPECT_EQ(readAt<uint16_t>(bytes, 11), spec.viewportWidth);
  EXPECT_EQ(readAt<uint16_t>(bytes, 13), spec.viewportHeight);
  EXPECT_EQ(readAt<uint16_t>(bytes, 17), section->pageCount);
  const uint32_t lutOffset = readAt<uint32_t>(bytes, 19);
  EXPECT_GE(lutOffset, kHeaderSize);
  EXPECT_LT(lutOffset, bytes.size());
}

// FR-006: a chapter is paginated over its OWN content, so page counts differ
// per chapter and none of them is the whole book's.
TEST_F(Fb2SectionCacheTest, PageCountsArePerChapterNotPerBook) {
  auto nested = std::make_shared<Fb2>(fixturePath("nested-deep.fb2"), tmp.path());
  ASSERT_TRUE(nested->load());
  ASSERT_EQ(nested->getSectionCount(), 4);

  int total = 0;
  std::vector<int> counts;
  for (int i = 0; i < nested->getSectionCount(); i++) {
    auto section = std::make_unique<Fb2Section>(nested, i, renderer);
    ASSERT_TRUE(section->createSectionFile(makeSpec())) << "chapter " << i;
    EXPECT_GT(section->pageCount, 0) << "chapter " << i;
    counts.push_back(section->pageCount);
    total += section->pageCount;
  }
  for (size_t i = 0; i < counts.size(); i++) {
    EXPECT_LT(counts[i], total) << "chapter " << i << " was paginated over the whole book";
  }
}

// Contract C8: a section whose only content is a child section still yields one
// page, so the reader never sees a zero-page chapter (loadPage would return
// nullptr for every page and the progress percentage would divide by zero).
TEST_F(Fb2SectionCacheTest, WrapperChapterGetsExactlyOnePage) {
  auto wrapper = std::make_shared<Fb2>(fixturePath("wrapper-only.fb2"), tmp.path());
  ASSERT_TRUE(wrapper->load());
  ASSERT_EQ(wrapper->getSectionCount(), 3);

  auto section = std::make_unique<Fb2Section>(wrapper, 1, renderer);
  ASSERT_TRUE(section->createSectionFile(makeSpec()));
  EXPECT_EQ(section->pageCount, 1);
  EXPECT_NE(section->loadPage(0), nullptr);
}

// FR-005: far past the old 256 cap, the layout parser still numbers chapters in
// lockstep with the metadata parser, so chapter 3000 lays out its own text and no
// neighbour's (3001 is its child, 2999 the previous chain's child).
TEST_F(Fb2SectionCacheTest, ChapterFarPastTheOldCapLaysOutItsOwnText) {
  const std::string path = tmp.path() + "/tower.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, fb2test::makeSectionTowerFb2(4000, 2)));
  auto tower = std::make_shared<Fb2>(path, tmp.path());
  ASSERT_TRUE(tower->load());
  ASSERT_EQ(tower->getSectionCount(), 4000);

  auto section = std::make_unique<Fb2Section>(tower, 3000, renderer);
  ASSERT_TRUE(section->createSectionFile(makeSpec()));
  std::vector<std::unique_ptr<Page>> pages;
  for (int i = 0; i < section->pageCount; i++) pages.push_back(section->loadPage(i));
  const auto words = collectWords(pages);
  EXPECT_TRUE(containsWord(words, "word3000"));
  EXPECT_FALSE(containsWord(words, "word3001")) << "a child chapter's text leaked into its parent";
  EXPECT_FALSE(containsWord(words, "word2999")) << "chapter numbering drifted between the parsers";
}

TEST_F(Fb2SectionCacheTest, LutEntriesAreMonotonicAndStartAfterHeader) {
  auto section = buildSection(makeSpec());
  ASSERT_NE(section, nullptr);

  const std::string bytes = readAll(sectionFilePath());
  const uint16_t pageCount = readAt<uint16_t>(bytes, 17);
  const uint32_t lutOffset = readAt<uint32_t>(bytes, 19);
  ASSERT_EQ(bytes.size(), lutOffset + pageCount * sizeof(uint32_t));

  uint32_t previous = 0;
  for (uint16_t i = 0; i < pageCount; i++) {
    const uint32_t entry = readAt<uint32_t>(bytes, lutOffset + i * sizeof(uint32_t));
    if (i == 0) {
      EXPECT_EQ(entry, kHeaderSize);  // first page starts right after the header
    } else {
      EXPECT_GT(entry, previous);
    }
    EXPECT_LT(entry, lutOffset);
    previous = entry;
  }
}

TEST_F(Fb2SectionCacheTest, LoadSectionFileAcceptsMatchingSpec) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);

  Fb2Section loaded(book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));
  EXPECT_EQ(loaded.pageCount, built->pageCount);
}

TEST_F(Fb2SectionCacheTest, LoadedPagesRoundTripTheParserOutputExactly) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);
  auto reference = parseReferencePages(spec);
  ASSERT_EQ(reference.size(), static_cast<size_t>(built->pageCount));

  Fb2Section loaded(book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));

  for (uint16_t pageIndex = 0; pageIndex < loaded.pageCount; pageIndex++) {
    auto page = loaded.loadPage(pageIndex);
    ASSERT_NE(page, nullptr) << "page " << pageIndex;
    const auto& expected = *reference[pageIndex];
    ASSERT_EQ(page->elements.size(), expected.elements.size()) << "page " << pageIndex;

    for (size_t e = 0; e < expected.elements.size(); e++) {
      const auto& expectedElement = *expected.elements[e];
      const auto& actualElement = *page->elements[e];
      ASSERT_EQ(actualElement.getTag(), expectedElement.getTag());
      EXPECT_EQ(actualElement.xPos, expectedElement.xPos);
      EXPECT_EQ(actualElement.yPos, expectedElement.yPos);
      if (expectedElement.getTag() != TAG_PageLine) continue;

      const auto& expectedBlock = static_cast<const PageLine&>(expectedElement).getBlock();
      const auto& actualBlock = static_cast<const PageLine&>(actualElement).getBlock();
      ASSERT_EQ(actualBlock->wordCount(), expectedBlock->wordCount());
      for (uint16_t w = 0; w < expectedBlock->wordCount(); w++) {
        EXPECT_STREQ(actualBlock->wordText(w), expectedBlock->wordText(w));
        EXPECT_EQ(actualBlock->wordStyle(w), expectedBlock->wordStyle(w));
        EXPECT_EQ(actualBlock->wordXpos(w), expectedBlock->wordXpos(w));
      }
      EXPECT_EQ(actualBlock->getBlockStyle().alignment, expectedBlock->getBlockStyle().alignment);
      EXPECT_EQ(actualBlock->getBlockStyle().marginLeft, expectedBlock->getBlockStyle().marginLeft);
    }
  }
}

TEST_F(Fb2SectionCacheTest, CenteredVerseAlignmentSurvivesSerialization) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);

  Fb2Section loaded(book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));

  bool found = false;
  for (uint16_t pageIndex = 0; pageIndex < loaded.pageCount && !found; pageIndex++) {
    auto page = loaded.loadPage(pageIndex);
    ASSERT_NE(page, nullptr);
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<PageLine&>(*element).getBlock();
      for (uint16_t w = 0; w < block->wordCount(); w++) {
        if (std::string("verseline") == block->wordText(w)) {
          EXPECT_EQ(block->getBlockStyle().alignment, CssTextAlign::Center);
          found = true;
        }
      }
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(Fb2SectionCacheTest, ContainerBlockStylesSurviveIntoRenderedPages) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);

  Fb2Section loaded(book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));

  struct FoundLine {
    std::shared_ptr<TextBlock> block;
    int16_t xPos = -1;
  };
  auto findLine = [&loaded](const std::string& needle) {
    FoundLine result;
    for (uint16_t pageIndex = 0; pageIndex < loaded.pageCount && !result.block; pageIndex++) {
      auto page = loaded.loadPage(pageIndex);
      if (!page) continue;
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& block = static_cast<PageLine&>(*element).getBlock();
        for (uint16_t w = 0; w < block->wordCount(); w++) {
          if (needle == block->wordText(w)) {
            result.block = block;
            result.xPos = element->xPos;
            break;
          }
        }
        if (result.block) break;
      }
    }
    return result;
  };

  // styles.fb2 wraps all container text in <p>, as real FB2 files do.
  const auto title = findLine("Styles");
  ASSERT_NE(title.block, nullptr);
  EXPECT_EQ(title.block->getBlockStyle().alignment, CssTextAlign::Center);

  const auto epigraph = findLine("epigraphword");
  ASSERT_NE(epigraph.block, nullptr);
  EXPECT_EQ(epigraph.block->getBlockStyle().alignment, CssTextAlign::Right);
  EXPECT_EQ(epigraph.block->getBlockStyle().marginLeft, 30);
  EXPECT_EQ(epigraph.xPos, 30);  // the line is drawn inside the indent

  const auto cite = findLine("citeword");
  ASSERT_NE(cite.block, nullptr);
  EXPECT_EQ(cite.block->getBlockStyle().marginLeft, 20);
  EXPECT_EQ(cite.xPos, 20);

  const auto plain = findLine("plainword");
  ASSERT_NE(plain.block, nullptr);
  EXPECT_EQ(plain.block->getBlockStyle().alignment, CssTextAlign::Justify);
  EXPECT_EQ(plain.block->getBlockStyle().marginLeft, 0);
  EXPECT_EQ(plain.xPos, 0);
}

TEST_F(Fb2SectionCacheTest, LoadPageOutOfRangeReturnsNull) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);
  EXPECT_EQ(built->loadPage(-1), nullptr);
  EXPECT_EQ(built->loadPage(built->pageCount), nullptr);
}

TEST_F(Fb2SectionCacheTest, SpecMismatchRejectsAndDeletesTheCacheFile) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);
  ASSERT_TRUE(fileExists(sectionFilePath()));

  auto differentFont = makeSpec();
  differentFont.fontId = 3;
  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(differentFont));
  // The stale file is discarded so the caller rebuilds from scratch.
  EXPECT_FALSE(fileExists(sectionFilePath()));

  // Rebuild with the new spec and it loads again.
  auto rebuilt = buildSection(differentFont);
  ASSERT_NE(rebuilt, nullptr);
  Fb2Section reloaded(book, 0, renderer);
  EXPECT_TRUE(reloaded.loadSectionFile(differentFont));
  EXPECT_NE(reloaded.loadPage(0), nullptr);
}

TEST_F(Fb2SectionCacheTest, ViewportChangeAloneInvalidatesTheCache) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);
  auto tallerViewport = makeSpec();
  tallerViewport.viewportHeight = 800;
  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(tallerViewport));
  EXPECT_FALSE(fileExists(sectionFilePath()));
}

TEST_F(Fb2SectionCacheTest, FileVersionMismatchRejectsAndDeletesTheCacheFile) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);

  std::string bytes = readAll(sectionFilePath());
  bytes[0] = 1;  // pretend an older firmware wrote this file
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes));

  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionFilePath()));
}

TEST_F(Fb2SectionCacheTest, CorruptPageDataYieldsNullPageNotCrash) {
  const auto spec = makeSpec();
  auto built = buildSection(spec);
  ASSERT_NE(built, nullptr);

  // Stomp the first page's serialized bytes (right after the header) with
  // 0xFF: element count becomes 0xFFFF and the first tag 0xFF is unknown.
  std::string bytes = readAll(sectionFilePath());
  ASSERT_GT(bytes.size(), kHeaderSize + 16);
  for (size_t i = kHeaderSize; i < kHeaderSize + 16; i++) bytes[i] = static_cast<char>(0xFF);
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes));

  Fb2Section loaded(book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));  // header itself is intact
  EXPECT_EQ(loaded.loadPage(0), nullptr);     // corrupt page fails gracefully
}

// loadSectionFile must verify that the page data and the complete LUT are
// actually present, not just that the header matches: a truncated file that
// still reported its full page count would send loadPage past EOF.
TEST_F(Fb2SectionCacheTest, TruncatedFileIsRejectedAndCacheCleared) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);

  std::string bytes = readAll(sectionFilePath());
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes.substr(0, kHeaderSize + 2)));

  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(makeSpec()));
  EXPECT_EQ(loaded.pageCount, 0);
  // The invalid file is discarded so the caller rebuilds from scratch.
  EXPECT_FALSE(fileExists(sectionFilePath()));

  auto rebuilt = buildSection(makeSpec());
  ASSERT_NE(rebuilt, nullptr);
  Fb2Section reloaded(book, 0, renderer);
  EXPECT_TRUE(reloaded.loadSectionFile(makeSpec()));
  EXPECT_NE(reloaded.loadPage(0), nullptr);
}

TEST_F(Fb2SectionCacheTest, FileCutInsideTheLutIsRejected) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);
  ASSERT_GT(built->pageCount, 1);

  // Drop the last LUT entry: header and page data intact, LUT incomplete.
  std::string bytes = readAll(sectionFilePath());
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes.substr(0, bytes.size() - sizeof(uint32_t))));

  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionFilePath()));
}

TEST_F(Fb2SectionCacheTest, GarbageLutOffsetIsRejected) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);

  // Stomp the header's LUT offset with 0xFFFFFFFF.
  std::string bytes = readAll(sectionFilePath());
  for (size_t i = 19; i < 23; i++) bytes[i] = static_cast<char>(0xFF);
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes));

  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionFilePath()));
}

TEST_F(Fb2SectionCacheTest, FileShorterThanHeaderIsRejected) {
  auto built = buildSection(makeSpec());
  ASSERT_NE(built, nullptr);

  const std::string bytes = readAll(sectionFilePath());
  ASSERT_TRUE(writeAll(sectionFilePath(), bytes.substr(0, kHeaderSize - 4)));

  Fb2Section loaded(book, 0, renderer);
  EXPECT_FALSE(loaded.loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionFilePath()));
}

TEST_F(Fb2SectionCacheTest, ClearCacheRemovesOnlyThatSectionFile) {
  auto zero = buildSection(makeSpec(), 0);
  auto one = buildSection(makeSpec(), 1);
  ASSERT_NE(zero, nullptr);
  ASSERT_NE(one, nullptr);
  EXPECT_TRUE(zero->clearCache());
  EXPECT_FALSE(fileExists(sectionFilePath(0)));
  EXPECT_TRUE(fileExists(sectionFilePath(1)));
  // Clearing again (file already gone) still succeeds.
  EXPECT_TRUE(zero->clearCache());
}

TEST_F(Fb2SectionCacheTest, SecondSectionBuildsItsOwnContent) {
  const auto spec = makeSpec();
  auto section = buildSection(spec, 1);
  ASSERT_NE(section, nullptr);
  Fb2Section loaded(book, 1, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));
  std::vector<std::unique_ptr<Page>> pages;
  for (uint16_t i = 0; i < loaded.pageCount; i++) {
    auto page = loaded.loadPage(i);
    ASSERT_NE(page, nullptr);
    pages.push_back(std::move(page));
  }
  const auto words = collectWords(pages);
  EXPECT_TRUE(containsWord(words, "decoyword"));
  EXPECT_FALSE(containsWord(words, "plainword"));
}

TEST(Fb2SectionCacheFallback, SectionlessBookBuildsWholeBodyAsOneSection) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  GfxRenderer renderer;
  auto book = std::make_shared<Fb2>(fixturePath("no-sections.fb2"), tmp.path());
  ASSERT_TRUE(book->load());
  ASSERT_EQ(book->getSectionCount(), 1);
  ASSERT_EQ(book->getSectionInfo(0).fileOffset, 0u);

  const auto spec = makeSpec();
  Fb2Section section(book, 0, renderer);
  ASSERT_TRUE(section.createSectionFile(spec));
  ASSERT_GT(section.pageCount, 0);

  std::vector<std::unique_ptr<Page>> pages;
  for (uint16_t i = 0; i < section.pageCount; i++) {
    auto page = section.loadPage(i);
    ASSERT_NE(page, nullptr);
    pages.push_back(std::move(page));
  }
  const auto words = collectWords(pages);
  EXPECT_TRUE(containsWord(words, "Paragraph"));
  EXPECT_TRUE(containsWord(words, "flowing."));
}

// ---------------------------------------------------------------------------
// Incremental build (startBuild / buildSomeMore). The rule these all serve:
// slicing must not change a single byte of what lands on disk.
// ---------------------------------------------------------------------------

// Build `index` of `book` in slices of the given budgets and return the file bytes.
std::string buildSliced(const std::shared_ptr<Fb2>& book, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                        int index, int pageBudget, uint32_t byteBudget) {
  Fb2Section section(book, index, renderer);
  if (!section.startBuild(spec)) return {};
  int guard = 0;
  while (!section.isBuildComplete()) {
    if (!section.buildSomeMore(pageBudget, byteBudget)) return {};
    if (++guard > 100000) return {};  // a slice that never advances is a bug, not a hang
  }
  return readAll(book->getCachePath() + "/sections/" + std::to_string(index) + ".bin");
}

std::string buildOneShot(const std::shared_ptr<Fb2>& book, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                         int index) {
  Fb2Section section(book, index, renderer);
  if (!section.createSectionFile(spec)) return {};
  return readAll(book->getCachePath() + "/sections/" + std::to_string(index) + ".bin");
}

// Each book gets its own temp dir so one build never sees another's cache.
struct SliceBook {
  fb2test::TempDir tmp;
  std::shared_ptr<Fb2> book;

  explicit SliceBook(const std::string& fixture) {
    if (!tmp.valid()) return;
    book = std::make_shared<Fb2>(fixturePath(fixture), tmp.path());
    if (!book->load()) book.reset();
  }
};

// T1: a one-page-at-a-time build and a one-shot build agree byte for byte.
// Catches: resetting any parser field at slice entry (e.g. nextWordContinues).
TEST(Fb2SectionSlice, PageSlicedBuildMatchesOneShotByteForByte) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  for (const char* fixture : {"long.fb2", "styles.fb2", "nested-sections.fb2"}) {
    SliceBook oneShot(fixture), sliced(fixture);
    ASSERT_NE(oneShot.book, nullptr) << fixture;
    ASSERT_NE(sliced.book, nullptr) << fixture;
    for (int i = 0; i < oneShot.book->getSectionCount(); i++) {
      const auto expected = buildOneShot(oneShot.book, renderer, spec, i);
      ASSERT_FALSE(expected.empty()) << fixture << " chapter " << i;
      EXPECT_EQ(buildSliced(sliced.book, renderer, spec, i, 1, 0), expected) << fixture << " chapter " << i;
    }
  }
}

// T2: the byte budget is checked BETWEEN XML_ParseBuffer calls, so any budget
// produces the same file. Catches: checking budgets inside a handler, which
// ends a slice mid-node and drops or duplicates text.
TEST(Fb2SectionSlice, ByteBudgetDoesNotAffectOutput) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  // slice-boundary.fb2 is one ~10KB text node of fixed-width words, so several
  // 1024-byte parse buffers end mid-word. That is the case a slice boundary can
  // corrupt: with a one-buffer budget, every buffer boundary is a slice entry.
  SliceBook reference("slice-boundary.fb2");
  ASSERT_NE(reference.book, nullptr);
  const auto expected = buildOneShot(reference.book, renderer, spec, 0);
  ASSERT_FALSE(expected.empty());

  for (const uint32_t budget : {1024u, 3072u, 7168u, 0u}) {
    SliceBook under("slice-boundary.fb2");
    ASSERT_NE(under.book, nullptr);
    EXPECT_EQ(buildSliced(under.book, renderer, spec, 0, 0, budget), expected) << "byteBudget " << budget;
  }
}

// T3: the words either side of a mid-word slice boundary survive intact. The
// byte-identity test above would catch corruption too, but this one says what
// actually broke. Catches: clearing partWordBuffer/nextWordContinues per slice.
TEST(Fb2SectionSlice, WordsSurviveAMidWordSliceBoundary) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  SliceBook b("slice-boundary.fb2");
  ASSERT_NE(b.book, nullptr);

  Fb2Section section(b.book, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  while (!section.isBuildComplete()) ASSERT_TRUE(section.buildSomeMore(0, 1024));

  std::vector<std::unique_ptr<Page>> pages;
  for (uint16_t i = 0; i < section.pageCount; i++) {
    auto page = section.loadPage(i);
    ASSERT_NE(page, nullptr);
    pages.push_back(std::move(page));
  }
  const auto words = collectWords(pages);
  // Every one of the fixture's 700 words must appear whole. Sampling is not
  // enough: only the handful sitting on a buffer boundary can break, so a
  // sparse check passes against a parser that drops the part-word each slice.
  int missing = 0;
  for (int i = 0; i < 700; i++) {
    char expectedWord[32];
    std::snprintf(expectedWord, sizeof(expectedWord), "wordpart%06d", i);
    if (!containsWord(words, expectedWord)) {
      if (missing < 5) ADD_FAILURE() << expectedWord << " did not survive slicing";
      missing++;
    }
  }
  EXPECT_EQ(missing, 0) << missing << " of 700 words lost at slice boundaries";
}

// T4: abandoning a build removes its .part and leaves an existing cache intact.
// Catches: writing in place instead of to .part, which clobbers the good file.
TEST(Fb2SectionSlice, AbandonLeavesExistingCacheUntouched) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  SliceBook b("long.fb2");
  ASSERT_NE(b.book, nullptr);
  const std::string path = b.book->getCachePath() + "/sections/0.bin";

  const auto good = buildOneShot(b.book, renderer, spec, 0);
  ASSERT_FALSE(good.empty());

  Fb2Section section(b.book, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  ASSERT_TRUE(section.buildSomeMore(1, 0));
  section.abandonBuild();

  EXPECT_FALSE(section.isBuilding());
  EXPECT_FALSE(fileExists(path + ".part"));
  EXPECT_EQ(readAll(path), good);
}

// T5: the destructor abandons, so no .part survives a dropped build.
TEST(Fb2SectionSlice, DestructorRemovesPartFile) {
  GfxRenderer renderer;
  SliceBook b("long.fb2");
  ASSERT_NE(b.book, nullptr);
  const std::string partPath = b.book->getCachePath() + "/sections/0.bin.part";
  {
    Fb2Section section(b.book, 0, renderer);
    ASSERT_TRUE(section.startBuild(makeSpec()));
    ASSERT_TRUE(section.buildSomeMore(1, 0));
    ASSERT_TRUE(fileExists(partPath));
  }
  EXPECT_FALSE(fileExists(partPath));
}

// T6: buildSomeMore without a build is a no-op false, not a crash.
TEST(Fb2SectionSlice, BuildSomeMoreWithoutBuildReturnsFalse) {
  GfxRenderer renderer;
  SliceBook b("basic.fb2");
  ASSERT_NE(b.book, nullptr);
  Fb2Section section(b.book, 0, renderer);
  EXPECT_FALSE(section.isBuilding());
  EXPECT_FALSE(section.buildSomeMore(2, 0));
  EXPECT_FALSE(section.isBuildComplete());
}

// T7: a sliced build commits a file the reader can load, with the same pages.
// Catches: renaming before the LUT is written (extent check rejects the file).
TEST(Fb2SectionSlice, SlicedBuildProducesLoadableSection) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  SliceBook b("long.fb2");
  ASSERT_NE(b.book, nullptr);

  Fb2Section built(b.book, 0, renderer);
  ASSERT_TRUE(built.startBuild(spec));
  while (!built.isBuildComplete()) ASSERT_TRUE(built.buildSomeMore(2, 0));
  ASSERT_GT(built.pageCount, 0);

  Fb2Section loaded(b.book, 0, renderer);
  ASSERT_TRUE(loaded.loadSectionFile(spec));
  ASSERT_EQ(loaded.pageCount, built.pageCount);

  std::vector<std::unique_ptr<Page>> pages;
  for (uint16_t i = 0; i < loaded.pageCount; i++) {
    auto page = loaded.loadPage(i);
    ASSERT_NE(page, nullptr) << "page " << i;
    pages.push_back(std::move(page));
  }
  EXPECT_FALSE(collectWords(pages).empty());
}

// The swap must survive an existing cache file. FatFile::rename opens the
// destination O_CREAT | O_EXCL and fails if it is there, so finalizeBuild must
// remove first. Catches: dropping that remove -- every rebuild would fail.
TEST(Fb2SectionSlice, RebuildOverExistingCacheSucceeds) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  SliceBook b("long.fb2");
  ASSERT_NE(b.book, nullptr);
  const std::string path = b.book->getCachePath() + "/sections/0.bin";

  const auto first = buildOneShot(b.book, renderer, spec, 0);
  ASSERT_FALSE(first.empty());
  ASSERT_TRUE(fileExists(path));

  // Same spec, same content: the rebuild must still commit over the old file.
  const auto second = buildOneShot(b.book, renderer, spec, 0);
  ASSERT_FALSE(second.empty()) << "rebuild over an existing cache file failed";
  EXPECT_EQ(second, first);
  EXPECT_FALSE(fileExists(path + ".part"));
}

// A .part left by an interrupted build is overwritten, never appended to.
TEST(Fb2SectionSlice, StalePartFileIsOverwrittenNotAppended) {
  GfxRenderer renderer;
  const auto spec = makeSpec();
  SliceBook clean("long.fb2"), stale("long.fb2");
  ASSERT_NE(clean.book, nullptr);
  ASSERT_NE(stale.book, nullptr);

  const auto expected = buildOneShot(clean.book, renderer, spec, 0);
  ASSERT_FALSE(expected.empty());

  const std::string sectionsDir = stale.book->getCachePath() + "/sections";
  ASSERT_EQ(::system(("mkdir -p '" + sectionsDir + "'").c_str()), 0);
  ASSERT_TRUE(writeAll(sectionsDir + "/0.bin.part", std::string(4096, '\xAB')));

  EXPECT_EQ(buildOneShot(stale.book, renderer, spec, 0), expected);
}

// Slicing must not cost allocations. On a ~380KB device, per-slice heap churn is
// the fragmentation hazard; allocation count is the accepted host proxy for it
// (Constitution IV). The budget is the one-shot build's own count plus the
// single BuildContext a sliced build adds.
TEST(Fb2SectionSlice, SlicingCostsNoExtraAllocations) {
  GfxRenderer renderer;
  const auto spec = makeSpec();

  size_t oneShotAllocs = 0;
  bool oneShotOk = false;
  {
    SliceBook b("slice-boundary.fb2");
    ASSERT_NE(b.book, nullptr);
    Fb2Section section(b.book, 0, renderer);
    {
      // gtest assertions allocate, so none may appear inside the scope.
      alloc_counter::CountingScope counting;
      oneShotOk = section.createSectionFile(spec);
      oneShotAllocs = counting.count();
    }
  }
  ASSERT_TRUE(oneShotOk);
  ASSERT_GT(oneShotAllocs, 0u);

  size_t slicedAllocs = 0;
  bool slicedOk = true;
  {
    SliceBook b("slice-boundary.fb2");
    ASSERT_NE(b.book, nullptr);
    Fb2Section section(b.book, 0, renderer);
    {
      alloc_counter::CountingScope counting;
      slicedOk = section.startBuild(spec);
      int guard = 0;
      while (slicedOk && !section.isBuildComplete() && ++guard < 100000) {
        slicedOk = section.buildSomeMore(2, 4096);
      }
      slicedAllocs = counting.count();
    }
  }
  ASSERT_TRUE(slicedOk);

  // createSectionFile IS startBuild + buildSomeMore, so the counts must match
  // exactly: pausing between slices allocates nothing. A per-slice allocation
  // (a re-created parser, a re-grown LUT, a temporary path string) lands here.
  EXPECT_EQ(slicedAllocs, oneShotAllocs);
}

}  // namespace
