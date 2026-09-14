#include "Fb2Section.h"

#include <Epub/Page.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <vector>

#include "Fb2SectionParser.h"

namespace {
// FB2 section files track only the spec fields that affect FB2 layout
// (no CSS, no images), so the version is independent of the EPUB one.
// v3: top-level-only section counting in Fb2SectionParser — files built by
// older firmware can hold a nested chapter's pages under a top-level index.
constexpr uint8_t FB2_SECTION_FILE_VERSION = 3;
// version + fontId + lineCompression + extraParagraphSpacing + paragraphAlignment +
// viewportWidth + viewportHeight + hyphenationEnabled + focusReadingEnabled +
// pageCount + lutOffset
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

bool Fb2Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  const auto& sectionInfo = fb2->getSectionInfo(sectionIndex);
  pageCount = 0;

  // Create cache directory
  {
    const auto sectionsDir = fb2->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  if (!Storage.openFileForWrite("FBS", filePath, file)) {
    return false;
  }
  writeSectionFileHeader(spec);
  std::vector<uint32_t> lut = {};

  // If there's only one section with fileOffset 0, the metadata parser found no real <section> tags.
  // Pass -1 to tell the parser to process all body content instead of filtering by section index.
  const int targetIndex = (fb2->getSectionCount() == 1 && sectionInfo.fileOffset == 0) ? -1 : sectionIndex;
  Fb2SectionParser visitor(
      fb2->getPath(), sectionInfo.length, targetIndex, renderer, spec,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); }, popupFn);
  Hyphenator::setPreferredLanguage(fb2->getLanguage());
  const bool success = visitor.parseAndBuildPages();

  if (!success) {
    LOG_ERR("FBS", "Failed to parse and build pages");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("FBS", "Failed LUT records");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Write final page count and LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
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
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * page);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto loadedPage = Page::deserialize(file);
  file.close();
  return loadedPage;
}
