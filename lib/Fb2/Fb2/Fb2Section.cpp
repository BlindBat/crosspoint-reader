#include "Fb2Section.h"

#include <Epub/Page.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <memory>
#include <vector>

#include "Fb2SectionParser.h"

namespace {
// FB2 section files track only the spec fields that affect FB2 layout
// (no CSS, no images), so the version is independent of the EPUB one.
// v3: top-level-only section counting in Fb2SectionParser — files built by
// older firmware can hold a nested chapter's pages under a top-level index.
// v4: container block styles (title centering, epigraph/cite indents) now
// reach wrapped <p> children, changing the laid-out pages.
// v5: every <section> is a chapter, so the file NAME (the chapter index) means
// something different — a v4 sections/1.bin holds the old second top-level
// chapter and would silently render the wrong text. The layout is unchanged.
constexpr uint8_t FB2_SECTION_FILE_VERSION = 5;
// version + fontId + lineCompression + extraParagraphSpacing + paragraphAlignment +
// viewportWidth + viewportHeight + hyphenationEnabled + focusReadingEnabled +
// pageCount + lutOffset
// Page-LUT pre-allocation. Sized from the reference book of issue #4, whose
// chapters are ~15 pages; a growth event would cost three heap operations and
// fragment DRAM mid-build. Chapters longer than this still work, they just
// reallocate once.
// ponytail: fixed capacity, revisit if a real corpus shows a fatter distribution.
constexpr size_t LUT_INITIAL_CAPACITY = 24;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(uint16_t) +
                                 sizeof(uint32_t);
}  // namespace

uint32_t Fb2Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("FBS", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("FBS", "Failed to serialize page %d", pageCount);
    return 0;
  }

  pageCount++;
  return position;
}

void Fb2Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_ERR("FBS", "File not open for writing header");
    return;
  }
  serialization::writePod(file, FB2_SECTION_FILE_VERSION);
  serialization::writePod(file, spec.fontId);
  serialization::writePod(file, spec.lineCompression);
  serialization::writePod(file, spec.extraParagraphSpacing);
  serialization::writePod(file, spec.paragraphAlignment);
  serialization::writePod(file, spec.viewportWidth);
  serialization::writePod(file, spec.viewportHeight);
  serialization::writePod(file, spec.hyphenationEnabled);
  serialization::writePod(file, spec.focusReadingEnabled);
  serialization::writePod(file, pageCount);                 // Placeholder
  serialization::writePod(file, static_cast<uint32_t>(0));  // LUT offset placeholder
}

bool Fb2Section::loadSectionFile(const ReaderRenderSpec& spec) {
  if (!Storage.openFileForRead("FBS", filePath, file)) {
    return false;
  }

  // Anything shorter than the fixed header cannot be a valid section file;
  // reject before reading fields that would come back uninitialized.
  if (file.size() < HEADER_SIZE) {
    file.close();
    LOG_DBG("FBS", "Section file shorter than header");
    clearCache();
    return false;
  }

  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != FB2_SECTION_FILE_VERSION) {
      file.close();
      LOG_DBG("FBS", "Version mismatch: %u", version);
      clearCache();
      return false;
    }

    int fileFontId;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    uint16_t fileViewportWidth, fileViewportHeight;
    bool fileHyphenationEnabled;
    bool fileFocusReadingEnabled;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileFocusReadingEnabled);

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing || spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.focusReadingEnabled != fileFocusReadingEnabled) {
      file.close();
      LOG_DBG("FBS", "Render spec does not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);

  // The header alone is not enough: a truncated or corrupted file would
  // still report its full page count and loadPage would then seek past EOF.
  // Validate that the page data region and the complete LUT actually fit.
  const size_t fileSize = file.size();
  const size_t lutSize = static_cast<size_t>(pageCount) * sizeof(uint32_t);
  const bool extentValid = fileSize >= HEADER_SIZE && lutOffset >= HEADER_SIZE &&
                           static_cast<size_t>(lutOffset) + lutSize <= fileSize &&
                           (pageCount == 0 || lutOffset > HEADER_SIZE);
  if (!extentValid) {
    file.close();
    LOG_DBG("FBS", "Section file extent invalid (size %u, pages %d, lut %u)", static_cast<unsigned>(fileSize),
            pageCount, static_cast<unsigned>(lutOffset));
    pageCount = 0;
    clearCache();
    return false;
  }

  file.close();
  LOG_DBG("FBS", "Loaded section: %d pages", pageCount);
  return true;
}

bool Fb2Section::clearCache() const {
  if (!Storage.exists(filePath.c_str())) {
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("FBS", "Failed to clear cache");
    return false;
  }

  LOG_DBG("FBS", "Cache cleared");
  return true;
}

void Fb2Section::appendBuiltPage(void* ctx, std::unique_ptr<Page> page) {
  auto* lutCtx = static_cast<BuildLutContext*>(ctx);
  lutCtx->lut->emplace_back(lutCtx->section->onPageComplete(std::move(page)));
}

Fb2Section::Fb2Section(const std::shared_ptr<Fb2>& fb2, const int sectionIndex, GfxRenderer& renderer)
    : fb2(fb2),
      sectionIndex(sectionIndex),
      renderer(renderer),
      filePath(fb2->getCachePath() + "/sections/" + std::to_string(sectionIndex) + ".bin") {}

Fb2Section::~Fb2Section() { abandonBuild(); }

bool Fb2Section::startBuild(const ReaderRenderSpec& spec, const BuildPopupFn& popupFn) {
  if (build_) {
    LOG_ERR("FBS", "Build already running for section %d", sectionIndex);
    return false;
  }

  pageCount = 0;
  buildComplete_ = false;

  {
    const auto sectionsDir = fb2->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // A .part left by an interrupted build is stale: overwrite it, never append.
  const auto tmpPath = binTmpPath();
  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }
  if (!Storage.openFileForWrite("FBS", tmpPath, file)) {
    return false;
  }
  writeSectionFileHeader(spec);

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    LOG_ERR("FBS", "OOM: BuildContext");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  // Most FB2 chapters are a handful of pages; reserve enough that the common
  // case never reallocates mid-build, which would fragment DRAM on device.
  ctx->lut.reserve(LUT_INITIAL_CAPACITY);
  ctx->lutCtx = BuildLutContext{this, &ctx->lut};

  const auto sectionInfo = fb2->getSectionInfo(sectionIndex);
  // If there's only one section with fileOffset 0, the metadata parser found no real <section> tags.
  // Pass -1 to tell the parser to process all body content instead of filtering by section index.
  const int targetIndex = (fb2->getSectionCount() == 1 && sectionInfo.fileOffset == 0) ? -1 : sectionIndex;
  ctx->parser =
      makeUniqueNoThrow<Fb2SectionParser>(fb2->getPath(), sectionInfo.length, targetIndex, renderer, spec,
                                          Fb2PageCompleteFn{&Fb2Section::appendBuiltPage, &ctx->lutCtx}, popupFn);
  if (!ctx->parser) {
    LOG_ERR("FBS", "OOM: Fb2SectionParser");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  Hyphenator::setPreferredLanguage(fb2->getLanguage());
  if (!ctx->parser->beginParse()) {
    LOG_ERR("FBS", "Failed to start section parse");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  build_ = std::move(ctx);
  return true;
}

bool Fb2Section::buildSomeMore(const int maxPages, const uint32_t maxBytes) {
  if (!build_) return false;

  const auto status = build_->parser->parseSome(maxPages, maxBytes);
  if (status == Fb2SectionParser::ParseStatus::Failed) {
    LOG_ERR("FBS", "Failed to parse and build pages");
    abandonBuild();
    return false;
  }
  if (status == Fb2SectionParser::ParseStatus::Paused) {
    return true;
  }

  build_->parser->finishParse();
  return finalizeBuild();
}

bool Fb2Section::finalizeBuild() {
  const auto tmpPath = binTmpPath();
  const auto failCommit = [&]() {
    file.close();
    Storage.remove(tmpPath.c_str());
    build_.reset();
    pageCount = 0;
    return false;
  };

  const uint32_t lutOffset = file.position();
  for (const uint32_t& pos : build_->lut) {
    if (pos == 0) {
      LOG_ERR("FBS", "Failed LUT records");
      return failCommit();
    }
    serialization::writePod(file, pos);
  }

  // Write final page count and LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  // Explicit close() required: member variable persists beyond function scope,
  // and the swap below must not race an open write handle.
  file.close();
  build_.reset();

  // Swap into place. The remove is not optional: FatFile::rename opens the
  // destination O_CREAT | O_EXCL, so renaming onto an existing path fails.
  // Same sequence (and same crash window) as Section::commitBuildFile.
  if (Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!Storage.rename(tmpPath.c_str(), filePath.c_str())) {
    LOG_ERR("FBS", "Failed to move built section into place");
    Storage.remove(tmpPath.c_str());
    pageCount = 0;
    return false;
  }

  buildComplete_ = true;
  return true;
}

// ponytail: an in-flight build is discarded, not resumed. expat cannot start
// mid-document and FB2 has no per-chapter extracted file to seek within, so
// there is no cheap byte watermark to resume from (EPUB has one only because
// its parser starts at the chapter's own unzipped HTML). Upgrade path: give FB2
// chapters an extracted-text intermediate file, which would make builds
// resumable AND remove the per-chapter full-file rescan -- see issue #8.
void Fb2Section::abandonBuild() {
  if (!build_) return;
  build_.reset();
  if (file) {
    // Explicit close() required before remove (member variable write handle).
    file.close();
  }
  Storage.remove(binTmpPath().c_str());
  pageCount = 0;
  buildComplete_ = false;
}

bool Fb2Section::createSectionFile(const ReaderRenderSpec& spec, const BuildPopupFn& popupFn) {
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  while (!buildComplete_) {
    if (!buildSomeMore(0, 0)) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<Page> Fb2Section::loadPage(const int page) {
  if (page < 0 || page >= pageCount) {
    return nullptr;
  }

  if (!Storage.openFileForRead("FBS", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset = 0;
  if (!serialization::readPod(file, lutOffset)) {
    LOG_ERR("FBS", "Page %d load failed: truncated LUT offset", page);
    file.close();
    return nullptr;
  }
  file.seek(lutOffset + sizeof(uint32_t) * page);
  uint32_t pagePos = 0;
  if (!serialization::readPod(file, pagePos)) {
    LOG_ERR("FBS", "Page %d load failed: truncated LUT entry", page);
    file.close();
    return nullptr;
  }
  file.seek(pagePos);

  auto loadedPage = Page::deserialize(file);
  file.close();
  return loadedPage;
}
