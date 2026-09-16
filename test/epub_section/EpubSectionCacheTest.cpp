// EPUB Section pagination engine: cache validation and corruption handling.
//
// loadSectionFile must reject (and clear) a section file whose render spec no
// longer matches, whose version byte is unknown, or -- for partials -- whose
// watermark trailer is malformed. Corrupt page data must fail a loadPage
// cleanly, with allocation bounded by the deserializer's caps, never a crash.

#include <AllocCounter.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "EpubSectionTestSupport.h"
#include "SectionLinkStubs.h"

namespace {

using namespace sectest;

class EpubSectionCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    // Small chapter: this suite rebuilds a lot, and rejection deletes the file.
    epub = makeEpub(tmp, buildChapterHtml(8, 24));
  }

  std::unique_ptr<Section> makeSection() { return std::make_unique<Section>(epub, 0, renderer); }

  // Full build with `spec`, then drop the Section (file stays on disk).
  void buildWith(const ReaderRenderSpec& spec) {
    auto section = makeSection();
    ASSERT_TRUE(section->createSectionFile(spec));
    ASSERT_GT(section->pageCount, 2);
  }

  // Byte-surgery on the committed section file.
  void patchFile(const std::function<void(std::string&)>& mutate) {
    std::string bytes = readAll(sectionBinPath(*epub));
    ASSERT_FALSE(bytes.empty());
    mutate(bytes);
    ASSERT_TRUE(writeAll(sectionBinPath(*epub), bytes));
  }

  TempDir tmp;
  GfxRenderer renderer;
  std::shared_ptr<Epub> epub;
};

TEST_F(EpubSectionCacheTest, MatchingSpecLoadsWithSamePageCount) {
  const auto spec = makeSpec();
  auto built = makeSection();
  ASSERT_TRUE(built->createSectionFile(spec));
  const uint16_t count = built->pageCount;
  built.reset();

  auto loaded = makeSection();
  ASSERT_TRUE(loaded->loadSectionFile(spec));
  EXPECT_EQ(loaded->pageCount, count);
  EXPECT_FALSE(loaded->isPartial());
  EXPECT_EQ(loaded->getCachedPageCount(), std::optional<uint16_t>(count));
  EXPECT_NE(loaded->loadPage(0), nullptr);
}

TEST_F(EpubSectionCacheTest, AnySpecFieldChangeRejectsAndClearsTheCache) {
  const std::vector<std::pair<const char*, std::function<void(ReaderRenderSpec&)>>> mutations = {
      {"fontId", [](ReaderRenderSpec& s) { s.fontId = 3; }},
      {"lineCompression", [](ReaderRenderSpec& s) { s.lineCompression = 1.2f; }},
      {"extraParagraphSpacing", [](ReaderRenderSpec& s) { s.extraParagraphSpacing = true; }},
      {"paragraphAlignment", [](ReaderRenderSpec& s) { s.paragraphAlignment = 2; }},
      {"viewportWidth", [](ReaderRenderSpec& s) { s.viewportWidth = 300; }},
      {"viewportHeight", [](ReaderRenderSpec& s) { s.viewportHeight = 128; }},
      {"hyphenationEnabled", [](ReaderRenderSpec& s) { s.hyphenationEnabled = true; }},
      {"embeddedStyle", [](ReaderRenderSpec& s) { s.embeddedStyle = true; }},
      {"imageRendering", [](ReaderRenderSpec& s) { s.imageRendering = 1; }},
      {"focusReadingEnabled", [](ReaderRenderSpec& s) { s.focusReadingEnabled = true; }},
  };

  for (const auto& [name, mutate] : mutations) {
    buildWith(makeSpec());
    ASSERT_TRUE(fileExists(sectionBinPath(*epub))) << name;

    ReaderRenderSpec changed = makeSpec();
    mutate(changed);
    auto section = makeSection();
    EXPECT_FALSE(section->loadSectionFile(changed)) << "spec change not detected: " << name;
    // Rejection clears the stale layout cache so the next open rebuilds.
    EXPECT_FALSE(fileExists(sectionBinPath(*epub))) << "stale cache kept after: " << name;
  }
}

TEST_F(EpubSectionCacheTest, UnknownVersionRejectsAndClears) {
  buildWith(makeSpec());
  patchFile([](std::string& bytes) { pokeAt<uint8_t>(bytes, kOffVersion, 99); });

  auto section = makeSection();
  EXPECT_FALSE(section->loadSectionFile(makeSpec()));
  EXPECT_EQ(section->pageCount, 0);
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionCacheTest, IncompleteVersionSentinelRejectsAndClears) {
  // A crash between header write and commit leaves version 0 -- such a file
  // must never be mistaken for a valid section.
  buildWith(makeSpec());
  patchFile([](std::string& bytes) { pokeAt<uint8_t>(bytes, kOffVersion, kIncompleteVersion); });

  auto section = makeSection();
  EXPECT_FALSE(section->loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionCacheTest, TruncatedPartialFailsTrailerCheckAndClears) {
  // Produce a real partial, then cut the trailer off.
  auto builder = std::make_unique<Section>(epub, 0, renderer);
  ASSERT_TRUE(builder->startBuild(makeSpec()));
  ASSERT_TRUE(builder->buildSomeMore(2));
  ASSERT_FALSE(builder->isBuildComplete());
  builder->suspendBuild();
  ASSERT_TRUE(builder->isPartial());
  builder.reset();

  patchFile([](std::string& bytes) { bytes.resize(bytes.size() - 4); });

  auto section = makeSection();
  EXPECT_FALSE(section->loadSectionFile(makeSpec()));
  EXPECT_EQ(section->pageCount, 0);
  EXPECT_FALSE(section->isPartial());
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionCacheTest, PartialWithZeroPagesIsRejected) {
  // Header claims a partial with pageCount 0: the trailer-location math has
  // nothing to stand on and the file must be rejected, not divided by.
  auto builder = std::make_unique<Section>(epub, 0, renderer);
  ASSERT_TRUE(builder->startBuild(makeSpec()));
  ASSERT_TRUE(builder->buildSomeMore(2));
  builder->suspendBuild();
  builder.reset();
  patchFile([](std::string& bytes) { pokeAt<uint16_t>(bytes, kOffPageCount, 0); });

  auto section = makeSection();
  EXPECT_FALSE(section->loadSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionCacheTest, CorruptLutEntryYieldsNullPageNotCrash) {
  const auto spec = makeSpec();
  buildWith(spec);

  // Point page 1's LUT entry into the header: deserialization reads nonsense
  // (with fontId==0 the bytes at offset 0 decode as elementCount=45, tag=0)
  // and must fail cleanly.
  patchFile([](std::string& bytes) {
    const auto lutOffset = readAt<uint32_t>(bytes, kOffLutOffset);
    pokeAt<uint32_t>(bytes, lutOffset + 1 * sizeof(uint32_t), 0);
  });

  auto section = makeSection();
  ASSERT_TRUE(section->loadSectionFile(spec));  // header is still pristine
  EXPECT_EQ(section->loadPage(1), nullptr);
  // Neighboring pages are unaffected.
  EXPECT_NE(section->loadPage(0), nullptr);
  EXPECT_NE(section->loadPage(2), nullptr);
}

TEST_F(EpubSectionCacheTest, CorruptPageDataFailsWithBoundedAllocation) {
  const auto spec = makeSpec();
  buildWith(spec);

  // Poison page 1's body: an absurd element count followed by an unknown tag.
  // Page::deserialize clamps its reserve and rejects the tag, so the load must
  // fail without ballooning allocation (the device has ~380KB of RAM total).
  patchFile([](std::string& bytes) {
    const auto lutOffset = readAt<uint32_t>(bytes, kOffLutOffset);
    const auto page1 = readAt<uint32_t>(bytes, lutOffset + 1 * sizeof(uint32_t));
    pokeAt<uint16_t>(bytes, page1, 0xFFFF);                  // element count
    pokeAt<uint8_t>(bytes, page1 + sizeof(uint16_t), 0x63);  // unknown tag
  });

  auto section = makeSection();
  ASSERT_TRUE(section->loadSectionFile(spec));

  std::unique_ptr<Page> page;
  size_t bytesAllocated = 0;
  {
    alloc_counter::CountingScope scope;
    page = section->loadPage(1);
    bytesAllocated = scope.bytes();
  }
  EXPECT_EQ(page, nullptr);
  EXPECT_LT(bytesAllocated, 64u * 1024u) << "corrupt page load allocated " << bytesAllocated << " bytes";
}

TEST_F(EpubSectionCacheTest, TruncatedFinalizedFileIsRejectedAndCleared) {
  const auto spec = makeSpec();
  buildWith(spec);

  // Cut the file roughly in half: the header survives, the trailing tables and
  // later pages do not. loadSectionFile validates the finalized table extents
  // against the file size, so the torn file is rejected outright and cleared
  // for rebuild instead of being served with nondeterministic page loads.
  patchFile([](std::string& bytes) { bytes.resize(kHeaderSize + (bytes.size() - kHeaderSize) / 2); });

  auto section = makeSection();
  EXPECT_FALSE(section->loadSectionFile(spec));
  EXPECT_EQ(section->pageCount, 0);
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
}

TEST_F(EpubSectionCacheTest, TruncationAfterLoadYieldsNullPagesDeterministically) {
  const auto spec = makeSpec();
  buildWith(spec);
  auto section = makeSection();
  ASSERT_TRUE(section->loadSectionFile(spec));
  const uint16_t claimed = section->pageCount;
  ASSERT_GT(claimed, 2);

  // Truncate underneath the already-loaded section (the validation at load
  // time cannot help here). The page LUT lives at the file's tail, so every
  // page load must fail with a deterministic nullptr -- never garbage pages --
  // and with bounded allocation.
  patchFile([](std::string& bytes) { bytes.resize(kHeaderSize + (bytes.size() - kHeaderSize) / 2); });

  size_t bytesAllocated = 0;
  {
    alloc_counter::CountingScope scope;
    for (uint16_t p = 0; p < claimed; p++) {
      EXPECT_EQ(section->loadPage(p), nullptr) << "page " << p << " must fail deterministically";
    }
    bytesAllocated = scope.bytes();
  }
  EXPECT_LT(bytesAllocated, 64u * 1024u);

  // The tables live past the truncation point, so table lookups come back
  // empty instead of crashing.
  EXPECT_EQ(section->getPageForAnchor("pp0"), std::nullopt);
  EXPECT_EQ(section->getPageForParagraphIndex(1), std::nullopt);
  EXPECT_EQ(section->getPageForListItemIndex(1), std::nullopt);
  EXPECT_EQ(section->getVisibleTextOffsetForPage(0), std::nullopt);
  EXPECT_EQ(section->getPageForVisibleTextOffset(0), std::nullopt);
}

TEST_F(EpubSectionCacheTest, ClearCacheRemovesSectionFileAndStaleTmp) {
  buildWith(makeSpec());
  ASSERT_TRUE(writeAll(sectionTmpPath(*epub), "stale interrupted build"));

  auto section = makeSection();
  EXPECT_TRUE(section->clearCache());
  EXPECT_FALSE(fileExists(sectionBinPath(*epub)));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));

  // Idempotent when nothing is cached.
  EXPECT_TRUE(section->clearCache());
}

TEST_F(EpubSectionCacheTest, StaleTmpFromInterruptedBuildIsReplacedByRebuild) {
  // A crash can leave a half-written .part behind; the next build must remove
  // it, rebuild, and commit a clean file.
  const std::string sectionsDir = epub->getCachePath() + "/sections";
  ASSERT_TRUE(Storage.mkdir(sectionsDir.c_str()));
  ASSERT_TRUE(writeAll(sectionTmpPath(*epub), std::string(64, '\xAB')));

  auto section = makeSection();
  ASSERT_TRUE(section->createSectionFile(makeSpec()));
  EXPECT_FALSE(fileExists(sectionTmpPath(*epub)));
  EXPECT_TRUE(fileExists(sectionBinPath(*epub)));

  auto loaded = makeSection();
  EXPECT_TRUE(loaded->loadSectionFile(makeSpec()));
  EXPECT_EQ(loaded->pageCount, section->pageCount);
}

/* ---------- PageImage deserialization guards ---------- */

// A section cache is untrusted input, and every image render path dereferences
// PageImage's ImageBlock and draws the rectangle the block reports. A page whose
// image block is missing, or whose stored extent cannot describe anything the
// panel could hold, must fail the whole page load rather than be handed to the
// renderer.

// Force ImageBlock::deserialize to yield nullptr for the scope's duration,
// standing in for the production nothrow allocation failing under memory
// pressure (see SectionLinkStubs.cpp).
class NullImageBlockScope {
 public:
  NullImageBlockScope() { gImageBlockDeserializeFails = true; }
  ~NullImageBlockScope() { gImageBlockDeserializeFails = false; }
};

template <typename T>
void appendPod(std::string& out, const T& value) {
  char raw[sizeof(T)];
  std::memcpy(raw, &value, sizeof(T));
  out.append(raw, sizeof(T));
}

void appendString(std::string& out, const std::string& value) {
  appendPod(out, static_cast<uint32_t>(value.size()));
  out.append(value);
}

// One-element page holding a single PageImage, laid out exactly as
// Page::serialize writes it (see contracts/cache-formats.md).
std::string imagePageBytes(const int16_t width, const int16_t height) {
  std::string out;
  appendPod<uint16_t>(out, 1);  // element count
  appendPod<uint8_t>(out, static_cast<uint8_t>(TAG_PageImage));
  appendPod<int16_t>(out, 40);                           // xPos
  appendPod<int16_t>(out, 12);                           // yPos
  appendString(out, "/.crosspoint/epub_1/img_0_0.png");  // ImageBlock::imagePath
  appendString(out, "OEBPS/images/cover.png");           // ImageBlock::srcPath
  appendPod(out, width);
  appendPod(out, height);
  appendPod<uint16_t>(out, 0);  // footnote count
  appendPod<uint16_t>(out, 0);  // link count
  return out;
}

class PageImageDeserializeTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(tmp.valid()); }

  std::unique_ptr<Page> deserialize(const std::string& bytes) {
    const std::string path = tmp.path() + "/page.bin";
    EXPECT_TRUE(writeAll(path, bytes));
    HalFile file;
    EXPECT_TRUE(Storage.openFileForRead("TST", path, file));
    return Page::deserialize(file);
  }

  TempDir tmp;
};

TEST_F(PageImageDeserializeTest, PanelSizedImageRoundTrips) {
  const auto page = deserialize(imagePageBytes(200, 150));
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->elements.size(), 1u);
  ASSERT_EQ(page->elements[0]->getTag(), TAG_PageImage);
  const auto& block = static_cast<const PageImage&>(*page->elements[0]).getImageBlock();
  EXPECT_EQ(block.getWidth(), 200);
  EXPECT_EQ(block.getHeight(), 150);
}

TEST_F(PageImageDeserializeTest, MissingImageBlockFailsThePage) {
  const NullImageBlockScope forceNull;
  EXPECT_EQ(deserialize(imagePageBytes(200, 150)), nullptr);
}

TEST_F(PageImageDeserializeTest, NegativeDimensionsFailThePage) {
  EXPECT_EQ(deserialize(imagePageBytes(-200, 150)), nullptr);
  EXPECT_EQ(deserialize(imagePageBytes(200, -1)), nullptr);
  EXPECT_EQ(deserialize(imagePageBytes(-1, -1)), nullptr);
}

// A zero edge is something the layout really produces: fit-to-container scaling
// truncates, so a full-width 1 px divider image is cached as width x 0. The
// block draws nothing, but the page around it is real text and must survive the
// load -- rejecting it would silently drop a page on every visit.
TEST_F(PageImageDeserializeTest, ZeroExtentImageKeepsThePage) {
  const auto flat = deserialize(imagePageBytes(760, 0));
  ASSERT_NE(flat, nullptr);
  ASSERT_EQ(flat->elements.size(), 1u);
  const auto thin = deserialize(imagePageBytes(0, 300));
  ASSERT_NE(thin, nullptr);
  ASSERT_EQ(thin->elements.size(), 1u);
}

TEST_F(PageImageDeserializeTest, DimensionsBeyondThePanelFailThePage) {
  // Layout fits every image inside the viewport, which never exceeds the
  // panel's long edge; 801 px and INT16_MAX can only come from corruption.
  EXPECT_EQ(deserialize(imagePageBytes(801, 150)), nullptr);
  EXPECT_EQ(deserialize(imagePageBytes(200, 801)), nullptr);
  EXPECT_EQ(deserialize(imagePageBytes(INT16_MAX, INT16_MAX)), nullptr);
}

TEST_F(PageImageDeserializeTest, TruncatedImageBlockFailsThePage) {
  // Cut the file so the block's width/height never arrive: the page must be
  // rejected because the footnote/link counts behind them cannot be read, not
  // because of whatever those truncated fields happened to hold.
  std::string bytes = imagePageBytes(200, 150);
  bytes.resize(bytes.size() - (2 * sizeof(int16_t) + 2 * sizeof(uint16_t)));
  EXPECT_EQ(deserialize(bytes), nullptr);

  // Cut only the trailer: the image itself is intact and valid.
  std::string noTrailer = imagePageBytes(200, 150);
  noTrailer.resize(noTrailer.size() - 2 * sizeof(uint16_t));
  EXPECT_EQ(deserialize(noTrailer), nullptr);

  // Empty file: not even the element count arrives.
  EXPECT_EQ(deserialize(std::string()), nullptr);
}

}  // namespace
