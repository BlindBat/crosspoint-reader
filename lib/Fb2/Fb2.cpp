#include "Fb2.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Fb2/Fb2CoverExtractor.h"
#include "Fb2/Fb2MetadataParser.h"

namespace {
// v2: auxiliary <body name="..."> sections are no longer counted as
// chapters, which shifts section numbering for books with footnote bodies.
// v3: every <section> is a chapter at any depth, which renumbers chapters for
// any nesting book, and each entry gains a `level` byte; the trailing TOC list
// is gone because the chapter list IS the TOC.
// v4: each entry gains a `flags` byte whose bit 0 marks a title derived from the
// section's first paragraph rather than read from a <title>.
constexpr uint8_t FB2_CACHE_VERSION = 4;

// The only defined bit of a chapter's `flags` byte.
constexpr uint8_t FB2_CHAPTER_FLAG_TITLE_DERIVED = 0x01;

// Upper bound for any string stored in book.bin (title/author/language/cover
// id/section titles). Real values are far below this; a corrupted length
// field must never drive a multi-megabyte resize on a ~380KB-RAM device.
constexpr uint32_t FB2_CACHE_MAX_STRING = 4096;

// Minimum serialized footprint of one cache chapter entry, derived from
// saveMetadataCache: a length-prefixed string (u32 prefix, possibly empty),
// two u32 fields and the level byte. A count field claiming more entries than
// the remaining file bytes could possibly hold is corrupt and must be rejected
// BEFORE reserve(): a 0xFFFF chapter count would request megabytes of vector
// storage up front, which on the ~380KB-RAM device means a bare-new abort.
constexpr uint32_t FB2_CACHE_MIN_SECTION_ENTRY =
    sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t);

// Checked variants of the serialization readers: fail instead of accepting
// short reads or unbounded string lengths, so a corrupted/truncated book.bin
// is rejected and the caller reparses the source file.
template <typename T>
bool readPodChecked(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

bool readStringChecked(HalFile& file, std::string& value) {
  uint32_t length;
  if (!readPodChecked(file, length)) {
    return false;
  }
  if (length > FB2_CACHE_MAX_STRING) {
    return false;
  }
  value.resize(length);
  if (length == 0) {
    return true;
  }
  return file.read(reinterpret_cast<uint8_t*>(&value[0]), length) == static_cast<int>(length);
}

// True when the unread tail of the file can still hold count entries of at
// least minEntrySize bytes each. Guards reserve() against corrupted counts.
bool countFitsRemainingFile(HalFile& file, const uint32_t count, const uint32_t minEntrySize) {
  const int available = file.available();
  if (available < 0) {
    return false;
  }
  return static_cast<uint64_t>(count) * minEntrySize <= static_cast<uint64_t>(available);
}
}  // namespace

Fb2::Fb2(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  cachePath = cacheDir + "/fb2_" + std::to_string(std::hash<std::string>{}(this->filepath));
}

bool Fb2::loadMetadataCache() {
  const auto cacheFile = cachePath + "/book.bin";
  HalFile file;
  if (!Storage.openFileForRead("FB2", cacheFile, file)) {
    return false;
  }

  uint8_t version;
  if (!readPodChecked(file, version)) {
    LOG_DBG("FB2", "Cache read failed");
    return false;
  }
  if (version != FB2_CACHE_VERSION) {
    LOG_DBG("FB2", "Cache version mismatch: %u vs %u", version, FB2_CACHE_VERSION);
    return false;
  }

  // Every read is checked: a truncated, zero-filled or otherwise corrupted
  // book.bin must be rejected so load() falls back to reparsing the book.
  if (!readStringChecked(file, title) || !readStringChecked(file, author) || !readStringChecked(file, language) ||
      !readStringChecked(file, coverBinaryId)) {
    LOG_DBG("FB2", "Cache metadata strings corrupted");
    return false;
  }

  uint16_t sectionCount;
  if (!readPodChecked(file, sectionCount) || sectionCount == 0 || sectionCount > FB2_MAX_CHAPTERS) {
    // parseMetadata always produces at least one section (whole-file fallback)
    // and never more than the cap, so anything else is corrupt, not empty.
    LOG_DBG("FB2", "Cache section count invalid");
    return false;
  }
  if (!countFitsRemainingFile(file, sectionCount, FB2_CACHE_MIN_SECTION_ENTRY)) {
    LOG_DBG("FB2", "Cache section count %u exceeds file size", sectionCount);
    return false;
  }
  sections.clear();
  sections.reserve(sectionCount);
  for (uint16_t i = 0; i < sectionCount; i++) {
    SectionInfo info;
    uint32_t offset, length;
    uint8_t level, flags;
    if (!readStringChecked(file, info.title) || !readPodChecked(file, offset) || !readPodChecked(file, length) ||
        !readPodChecked(file, level) || !readPodChecked(file, flags)) {
      LOG_DBG("FB2", "Cache section %u corrupted", i);
      sections.clear();
      return false;
    }
    // Only bit 0 is defined, and a derived marker on an empty title cannot come
    // from any parse, so either is corruption rather than an unknown future flag.
    if ((flags & ~FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 ||
        ((flags & FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 && info.title.empty())) {
      LOG_DBG("FB2", "Cache section %u flags %u invalid", i, flags);
      sections.clear();
      return false;
    }
    // Nesting grows one step at a time and the first chapter is always a direct
    // child of <body>, so anything else is a corrupted level byte.
    const uint8_t previousLevel = i == 0 ? 0 : sections.back().level;
    if (level > previousLevel + (i == 0 ? 0 : 1)) {
      LOG_DBG("FB2", "Cache section %u level %u out of sequence", i, level);
      sections.clear();
      return false;
    }
    info.fileOffset = offset;
    info.length = length;
    info.level = level;
    info.titleDerived = (flags & FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 ? 1 : 0;
    sections.push_back(std::move(info));
  }

  LOG_DBG("FB2", "Loaded metadata cache: %d chapters", sectionCount);
  return true;
}

bool Fb2::saveMetadataCache() const {
  const auto cacheFile = cachePath + "/book.bin";
  HalFile file;
  if (!Storage.openFileForWrite("FB2", cacheFile, file)) {
    return false;
  }

  serialization::writePod(file, FB2_CACHE_VERSION);
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, language);
  serialization::writeString(file, coverBinaryId);

  const uint16_t sectionCount = static_cast<uint16_t>(sections.size());
  serialization::writePod(file, sectionCount);
  for (const auto& info : sections) {
    serialization::writeString(file, info.title);
    serialization::writePod(file, static_cast<uint32_t>(info.fileOffset));
    serialization::writePod(file, static_cast<uint32_t>(info.length));
    serialization::writePod(file, info.level);
    serialization::writePod(file, static_cast<uint8_t>(info.titleDerived ? FB2_CHAPTER_FLAG_TITLE_DERIVED : 0));
  }

  LOG_DBG("FB2", "Saved metadata cache");
  return true;
}

bool Fb2::parseMetadata() {
  sections.clear();
  const Fb2ChapterSink sink{&sections,
                            [](void* ctx) {
                              static_cast<std::vector<SectionInfo>*>(ctx)->emplace_back();
                              return true;
                            },
                            [](void* ctx, const uint16_t index, SectionInfo& chapter) {
                              (*static_cast<std::vector<SectionInfo>*>(ctx))[index] = std::move(chapter);
                              return true;
                            }};
  Fb2MetadataParser parser(filepath, sink);
  if (!parser.parse()) {
    LOG_ERR("FB2", "Failed to parse metadata");
    return false;
  }

  title = parser.getTitle();
  author = parser.getAuthor();
  language = parser.getLanguage();
  coverBinaryId = parser.getCoverBinaryId();
  sections.shrink_to_fit();

  LOG_DBG("FB2", "Parsed: title=%s, author=%s, sections=%d", title.c_str(), author.c_str(),
          static_cast<int>(sections.size()));
  return true;
}

bool Fb2::load(const bool buildIfMissing) {
  LOG_DBG("FB2", "Loading FB2: %s", filepath.c_str());

  // Try cache first
  if (loadMetadataCache()) {
    loaded = true;
    return true;
  }

  if (!buildIfMissing) {
    return false;
  }

  // Parse from scratch
  LOG_DBG("FB2", "Cache not found, parsing...");
  setupCacheDir();

  if (!parseMetadata()) {
    return false;
  }

  if (!saveMetadataCache()) {
    LOG_ERR("FB2", "Could not save metadata cache");
  }

  loaded = true;
  return true;
}

bool Fb2::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("FB2", "Failed to clear cache");
    return false;
  }

  LOG_DBG("FB2", "Cache cleared");
  return true;
}

void Fb2::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }
  Storage.mkdir(cachePath.c_str());
}

const std::string& Fb2::getCachePath() const { return cachePath; }

const std::string& Fb2::getPath() const { return filepath; }

const std::string& Fb2::getTitle() const {
  static std::string blank;
  return loaded ? title : blank;
}

const std::string& Fb2::getAuthor() const {
  static std::string blank;
  return loaded ? author : blank;
}

const std::string& Fb2::getLanguage() const {
  static std::string blank;
  return loaded ? language : blank;
}

std::string Fb2::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Fb2::generateCoverBmp() const {
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || coverBinaryId.empty()) {
    LOG_DBG("FB2", "No cover image available");
    return false;
  }

  setupCacheDir();
  Fb2CoverExtractor extractor(filepath, coverBinaryId, getCoverBmpPath());
  return extractor.extract();
}

std::string Fb2::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }

std::string Fb2::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Fb2::generateThumbBmp(int height) const {
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    return true;
  }

  if (!loaded || coverBinaryId.empty()) {
    LOG_DBG("FB2", "No cover image for thumbnail");
    // Write empty file to avoid future attempts
    HalFile thumbBmp;
    setupCacheDir();
    Storage.openFileForWrite("FB2", getThumbBmpPath(height), thumbBmp);
    return false;
  }

  setupCacheDir();
  Fb2CoverExtractor extractor(filepath, coverBinaryId, "");
  return extractor.extractThumb(getThumbBmpPath(height), height);
}

int Fb2::getSectionCount() const { return static_cast<int>(sections.size()); }

const Fb2::SectionInfo& Fb2::getSectionInfo(int index) const {
  static SectionInfo empty;
  if (index < 0 || index >= static_cast<int>(sections.size())) {
    return empty;
  }
  return sections[index];
}

size_t Fb2::getBookSize() const {
  if (sections.empty()) {
    return 0;
  }
  return getCumulativeSectionSize(static_cast<int>(sections.size()) - 1);
}

size_t Fb2::getCumulativeSectionSize(int index) const {
  if (index < 0 || index >= static_cast<int>(sections.size())) {
    return 0;
  }
  size_t cumulative = 0;
  for (int i = 0; i <= index; i++) {
    cumulative += sections[i].length;
  }
  return cumulative;
}

float Fb2::calculateProgress(int currentSectionIndex, float currentSectionRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevSize = (currentSectionIndex >= 1) ? getCumulativeSectionSize(currentSectionIndex - 1) : 0;
  const size_t curSize = getCumulativeSectionSize(currentSectionIndex) - prevSize;
  const float sectionProgSize = currentSectionRead * static_cast<float>(curSize);
  const float totalProgress = static_cast<float>(prevSize) + sectionProgSize;
  return totalProgress / static_cast<float>(bookSize);
}

// One chapter per section, so the TOC is `sections` itself and both index
// mappings are the identity.
int Fb2::getTocCount() const { return getSectionCount(); }

const Fb2::SectionInfo& Fb2::getTocEntry(int index) const { return getSectionInfo(index); }

int Fb2::getTocIndexForSectionIndex(int sectionIndex) const {
  if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) {
    return -1;
  }
  return sectionIndex;
}

int Fb2::getSectionIndexForTocIndex(int tocIndex) const {
  if (tocIndex < 0 || tocIndex >= static_cast<int>(sections.size())) {
    return 0;
  }
  return tocIndex;
}

int Fb2::firstChapterOfTopLevel(const int ordinal) const {
  if (sections.empty()) {
    return 0;
  }
  int seen = 0;
  for (int i = 0; i < static_cast<int>(sections.size()); i++) {
    if (sections[i].level != 0) {
      continue;
    }
    if (seen == ordinal) {
      return i;
    }
    seen++;
  }
  return static_cast<int>(sections.size()) - 1;
}
