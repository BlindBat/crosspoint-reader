#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Fb2.h"
#include "Fb2/Fb2Section.h"
#include "Fb2/Fb2SectionParser.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fileExists;
using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

// Mirrors the private HEADER_SIZE constant in Fb2Section.cpp:
// version(1) + fontId(4) + lineCompression(4) + extraParagraphSpacing(1) +
// paragraphAlignment(1) + viewportWidth(2) + viewportHeight(2) +
// hyphenationEnabled(1) + focusReadingEnabled(1) + pageCount(2) + lutOffset(4).
constexpr uint32_t kHeaderSize = 23;
constexpr uint8_t kSectionFileVersion = 4;

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
    const auto& info = book->getSectionInfo(index);
    Fb2SectionParser parser(book->getPath(), info.length, index, renderer, spec,
                            [&pages](std::unique_ptr<Page> page) { pages.push_back(std::move(page)); });
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

}  // namespace
