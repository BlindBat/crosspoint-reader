#include "Fb2.h"

#include <HalStorage.h>
#include <Logging.h>

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
// v5: chapters become fixed 20-byte records followed by a packed title area, so
// one chapter is read with one seek instead of the whole list living in RAM;
// each record gains the running total of chapter lengths.
constexpr uint8_t FB2_CACHE_VERSION = 5;

// The only defined bit of a chapter's `flags` byte.
constexpr uint8_t FB2_CHAPTER_FLAG_TITLE_DERIVED = 0x01;

// Upper bound for any string stored in book.bin (title/author/language/cover
// id/section titles). Real values are far below this (longest real chapter title:
// 1,144 bytes across 2,899 books); a corrupted length field must never drive a
// multi-megabyte resize on a ~380KB-RAM device. Strings are cut to it on write, so
// a valid parse never produces a cache that fails its own validation.
constexpr uint32_t FB2_CACHE_MAX_STRING = 4096;

// titleOffset u32, titleLength u16, fileOffset u32, ownLength u32,
// cumulativeLength u32, level u8, flags u8.
constexpr uint32_t RECORD_SIZE = 20;
constexpr uint32_t RECORD_LEVEL_OFFSET = 18;

struct Record {
  uint32_t titleOffset = 0;
  uint16_t titleLength = 0;
  uint32_t fileOffset = 0;
  uint32_t ownLength = 0;
  uint32_t cumulativeLength = 0;
  uint8_t level = 0;
  uint8_t flags = 0;
};

// Checked readers and writers: fail instead of accepting short reads or writes, so
// a corrupted/truncated book.bin is rejected (the caller reparses the source file)
// and a failed write is never mistaken for a written cache.
template <typename T>
bool readPodChecked(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePodChecked(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
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

bool writeStringChecked(HalFile& file, const std::string& value) {
  const auto length = static_cast<uint32_t>(value.size());
  return writePodChecked(file, length) &&
         (length == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == length);
}

// Cuts a string to FB2_CACHE_MAX_STRING bytes, backing up to a UTF-8 lead byte.
void clampToCacheString(std::string& value) {
  if (value.size() <= FB2_CACHE_MAX_STRING) return;
  size_t cut = FB2_CACHE_MAX_STRING;
  while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) cut--;
  value.resize(cut);
}

bool readRecord(HalFile& file, Record& r) {
  return readPodChecked(file, r.titleOffset) && readPodChecked(file, r.titleLength) &&
         readPodChecked(file, r.fileOffset) && readPodChecked(file, r.ownLength) &&
         readPodChecked(file, r.cumulativeLength) && readPodChecked(file, r.level) && readPodChecked(file, r.flags);
}

bool writeRecord(HalFile& file, const Record& r) {
  return writePodChecked(file, r.titleOffset) && writePodChecked(file, r.titleLength) &&
         writePodChecked(file, r.fileOffset) && writePodChecked(file, r.ownLength) &&
         writePodChecked(file, r.cumulativeLength) && writePodChecked(file, r.level) && writePodChecked(file, r.flags);
}

// The parser's chapter sink during a build: fixed records in chapters.tmp, one
// slot appended per start tag and patched at the end tag (end tags arrive
// children-first, so slots cannot simply be appended in order), and title bytes
// appended to titles.tmp. Nothing is held per chapter in RAM.
// ponytail: unbuffered, 3 small SD writes per chapter interleaved with source
// reads; buffer titles.tmp (append-only) if first open measures slower on device.
struct BuildSink {
  HalFile chapters;
  HalFile titles;
  uint32_t count = 0;
  uint32_t titlesSize = 0;

  static bool reserve(void* ctx) {
    auto* self = static_cast<BuildSink*>(ctx);
    if (!self->chapters.seek(self->count * RECORD_SIZE) || !writeRecord(self->chapters, Record{})) {
      return false;
    }
    self->count++;
    return true;
  }

  static bool write(void* ctx, const uint16_t index, Fb2::SectionInfo& chapter) {
    auto* self = static_cast<BuildSink*>(ctx);
    clampToCacheString(chapter.title);
    Record r;
    r.titleOffset = self->titlesSize;
    r.titleLength = static_cast<uint16_t>(chapter.title.size());
    r.fileOffset = static_cast<uint32_t>(chapter.fileOffset);
    r.ownLength = static_cast<uint32_t>(chapter.length);
    r.level = chapter.level;
    r.flags = chapter.titleDerived ? FB2_CHAPTER_FLAG_TITLE_DERIVED : 0;
    if (r.titleLength > 0 &&
        self->titles.write(reinterpret_cast<const uint8_t*>(chapter.title.data()), r.titleLength) != r.titleLength) {
      return false;
    }
    self->titlesSize += r.titleLength;
    return self->chapters.seek(static_cast<size_t>(index) * RECORD_SIZE) && writeRecord(self->chapters, r);
  }
};
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
  std::string fileTitle, fileAuthor, fileLanguage, fileCoverId;
  if (!readStringChecked(file, fileTitle) || !readStringChecked(file, fileAuthor) ||
      !readStringChecked(file, fileLanguage) || !readStringChecked(file, fileCoverId)) {
    LOG_DBG("FB2", "Cache metadata strings corrupted");
    return false;
  }

  uint16_t count;
  uint32_t fileTitlesSize;
  if (!readPodChecked(file, count) || !readPodChecked(file, fileTitlesSize)) {
    LOG_DBG("FB2", "Cache header truncated");
    return false;
  }
  // A build always writes at least one chapter (whole-file fallback) and never
  // more than the cap, so anything else is corrupt, not empty.
  if (count == 0 || count > FB2_MAX_CHAPTERS) {
    LOG_DBG("FB2", "Cache chapter count %u invalid", count);
    return false;
  }
  // The file is exactly header + records + titles. Checked before any record is
  // read, so a lying count or a truncated tail is rejected in O(1).
  const uint64_t fileRecordsOffset = file.position();
  const uint64_t fileTitlesOffset = fileRecordsOffset + static_cast<uint64_t>(count) * RECORD_SIZE;
  if (fileTitlesOffset + fileTitlesSize != static_cast<uint64_t>(file.size())) {
    LOG_DBG("FB2", "Cache size mismatch for %u chapters", count);
    return false;
  }

  uint8_t previousLevel = 0;
  uint32_t previousCumulative = 0;
  for (uint16_t i = 0; i < count; i++) {
    Record r;
    if (!readRecord(file, r)) {
      LOG_DBG("FB2", "Cache chapter %u truncated", i);
      return false;
    }
    if (r.titleLength > FB2_CACHE_MAX_STRING || static_cast<uint64_t>(r.titleOffset) + r.titleLength > fileTitlesSize) {
      LOG_DBG("FB2", "Cache chapter %u title out of bounds", i);
      return false;
    }
    // Only bit 0 is defined, and a derived marker on an empty title cannot come
    // from any parse, so either is corruption rather than an unknown future flag.
    if ((r.flags & ~FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 ||
        ((r.flags & FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 && r.titleLength == 0)) {
      LOG_DBG("FB2", "Cache chapter %u flags %u invalid", i, r.flags);
      return false;
    }
    // Nesting grows one step at a time and the first chapter is always a direct
    // child of <body>, so anything else is a corrupted level byte.
    if (r.level > previousLevel + (i == 0 ? 0 : 1)) {
      LOG_DBG("FB2", "Cache chapter %u level %u out of sequence", i, r.level);
      return false;
    }
    // The running total is what progress reads, so it must be exactly the sum.
    if (r.cumulativeLength < previousCumulative || r.cumulativeLength - previousCumulative != r.ownLength) {
      LOG_DBG("FB2", "Cache chapter %u running total invalid", i);
      return false;
    }
    previousLevel = r.level;
    previousCumulative = r.cumulativeLength;
  }

  title = std::move(fileTitle);
  author = std::move(fileAuthor);
  language = std::move(fileLanguage);
  coverBinaryId = std::move(fileCoverId);
  chapterCount = count;
  recordsOffset = static_cast<uint32_t>(fileRecordsOffset);
  titlesOffset = static_cast<uint32_t>(fileTitlesOffset);
  titlesSize = fileTitlesSize;
  bookSize = previousCumulative;
  LOG_DBG("FB2", "Loaded metadata cache: %d chapters", count);
  return true;
}

// Parses the source into chapters.tmp + titles.tmp, then assembles book.bin in one
// sequential pass that fills in the running totals. Any failure removes all three
// files, so a failed build can never be mistaken for a cache.
bool Fb2::buildMetadataCache() {
  const auto cacheFile = cachePath + "/book.bin";
  const auto chaptersPath = cachePath + "/chapters.tmp";
  const auto titlesPath = cachePath + "/titles.tmp";
  auto cleanup = [&]() {
    Storage.remove(cacheFile.c_str());
    Storage.remove(chaptersPath.c_str());
    Storage.remove(titlesPath.c_str());
    return false;
  };

  uint32_t count = 0;
  uint32_t builtTitlesSize = 0;
  {
    BuildSink sink;
    if (!Storage.openFileForWrite("FB2", chaptersPath, sink.chapters) ||
        !Storage.openFileForWrite("FB2", titlesPath, sink.titles)) {
      return cleanup();
    }
    Fb2MetadataParser parser(filepath, Fb2ChapterSink{&sink, &BuildSink::reserve, &BuildSink::write});
    if (!parser.parse()) {
      LOG_ERR("FB2", "Failed to parse metadata");
      return cleanup();
    }
    title = parser.getTitle();
    author = parser.getAuthor();
    language = parser.getLanguage();
    coverBinaryId = parser.getCoverBinaryId();
    count = sink.count;
    builtTitlesSize = sink.titlesSize;
  }  // closes both temp files before they are reopened for reading
  if (count == 0 || count > FB2_MAX_CHAPTERS) {
    LOG_ERR("FB2", "Parsed %u chapters", count);
    return cleanup();
  }
  clampToCacheString(title);
  clampToCacheString(author);
  clampToCacheString(language);
  clampToCacheString(coverBinaryId);

  {
    HalFile out, chapters, titles;
    if (!Storage.openFileForWrite("FB2", cacheFile, out) || !Storage.openFileForRead("FB2", chaptersPath, chapters) ||
        !Storage.openFileForRead("FB2", titlesPath, titles)) {
      return cleanup();
    }
    if (!writePodChecked(out, FB2_CACHE_VERSION) || !writeStringChecked(out, title) ||
        !writeStringChecked(out, author) || !writeStringChecked(out, language) ||
        !writeStringChecked(out, coverBinaryId) || !writePodChecked(out, static_cast<uint16_t>(count)) ||
        !writePodChecked(out, builtTitlesSize)) {
      return cleanup();
    }
    uint32_t cumulative = 0;
    for (uint32_t i = 0; i < count; i++) {
      Record r;
      if (!readRecord(chapters, r) || cumulative + r.ownLength < cumulative) {
        return cleanup();
      }
      cumulative += r.ownLength;
      r.cumulativeLength = cumulative;
      if (!writeRecord(out, r)) {
        return cleanup();
      }
    }
    uint8_t buffer[128];
    for (uint32_t copied = 0; copied < builtTitlesSize;) {
      const uint32_t want = builtTitlesSize - copied < sizeof(buffer) ? builtTitlesSize - copied : sizeof(buffer);
      if (titles.read(buffer, want) != static_cast<int>(want) || out.write(buffer, want) != want) {
        return cleanup();
      }
      copied += want;
    }
  }
  Storage.remove(chaptersPath.c_str());
  Storage.remove(titlesPath.c_str());
  LOG_DBG("FB2", "Built metadata cache: %u chapters", count);
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

  // Parse from scratch, then load what was written: the chapter list lives only
  // in book.bin, and loading it validates the build the same way as any cache.
  LOG_DBG("FB2", "Cache not found, parsing...");
  setupCacheDir();
  if (!buildMetadataCache() || !loadMetadataCache()) {
    LOG_ERR("FB2", "Could not build metadata cache");
    return false;
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

int Fb2::getSectionCount() const { return chapterCount; }

bool Fb2::openIndex(HalFile& bookBin) const {
  return loaded && Storage.openFileForRead("FB2", cachePath + "/book.bin", bookBin);
}

Fb2::SectionInfo Fb2::getSectionInfo(const int index) const {
  if (index < 0 || index >= chapterCount) {
    return {};
  }
  HalFile bookBin;
  if (!openIndex(bookBin)) {
    LOG_ERR("FB2", "Chapter index unavailable");
    return {};
  }
  return getSectionInfo(index, bookBin);
}

Fb2::SectionInfo Fb2::getSectionInfo(const int index, HalFile& bookBin) const {
  SectionInfo info;
  if (index < 0 || index >= chapterCount) {
    return info;
  }
  Record r;
  // Bounds are re-checked because the file is re-read on every lookup; load()
  // validated it, but it is still SD content.
  if (!bookBin.seek(recordsOffset + static_cast<size_t>(index) * RECORD_SIZE) || !readRecord(bookBin, r) ||
      r.titleLength > FB2_CACHE_MAX_STRING || static_cast<uint64_t>(r.titleOffset) + r.titleLength > titlesSize) {
    LOG_ERR("FB2", "Chapter %d record unreadable", index);
    return info;
  }
  if (r.titleLength > 0) {
    info.title.resize(r.titleLength);
    if (!bookBin.seek(titlesOffset + r.titleOffset) ||
        bookBin.read(reinterpret_cast<uint8_t*>(&info.title[0]), r.titleLength) != static_cast<int>(r.titleLength)) {
      LOG_ERR("FB2", "Chapter %d title unreadable", index);
      return {};
    }
  }
  info.fileOffset = r.fileOffset;
  info.length = r.ownLength;
  info.cumulativeLength = r.cumulativeLength;
  info.level = r.level;
  info.titleDerived = (r.flags & FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 ? 1 : 0;
  return info;
}

size_t Fb2::getBookSize() const { return bookSize; }

size_t Fb2::getCumulativeSectionSize(const int index) const {
  if (index < 0 || index >= chapterCount) {
    return 0;
  }
  HalFile bookBin;
  if (!openIndex(bookBin)) {
    return 0;
  }
  return getCumulativeSectionSize(index, bookBin);
}

size_t Fb2::getCumulativeSectionSize(const int index, HalFile& bookBin) const {
  if (index < 0 || index >= chapterCount) {
    return 0;
  }
  Record r;
  if (!bookBin.seek(recordsOffset + static_cast<size_t>(index) * RECORD_SIZE) || !readRecord(bookBin, r)) {
    return 0;
  }
  return r.cumulativeLength;
}

float Fb2::calculateProgress(const SectionInfo& chapter, const float chapterRead) const {
  if (bookSize == 0 || chapter.cumulativeLength < chapter.length) {
    return 0.0f;
  }
  const size_t before = chapter.cumulativeLength - chapter.length;
  const float totalProgress = static_cast<float>(before) + chapterRead * static_cast<float>(chapter.length);
  return totalProgress / static_cast<float>(bookSize);
}

// One chapter per section, so the TOC is the chapter list itself and both index
// mappings are the identity.
int Fb2::getTocCount() const { return getSectionCount(); }

Fb2::SectionInfo Fb2::getTocEntry(const int index) const { return getSectionInfo(index); }

Fb2::SectionInfo Fb2::getTocEntry(const int index, HalFile& bookBin) const { return getSectionInfo(index, bookBin); }

int Fb2::getTocIndexForSectionIndex(int sectionIndex) const {
  if (sectionIndex < 0 || sectionIndex >= chapterCount) {
    return -1;
  }
  return sectionIndex;
}

int Fb2::getSectionIndexForTocIndex(int tocIndex) const {
  if (tocIndex < 0 || tocIndex >= chapterCount) {
    return 0;
  }
  return tocIndex;
}

int Fb2::firstChapterOfTopLevel(const int ordinal) const {
  if (chapterCount == 0) {
    return 0;
  }
  HalFile bookBin;
  if (!openIndex(bookBin)) {
    return 0;
  }
  int seen = 0;
  for (int i = 0; i < chapterCount; i++) {
    uint8_t level;
    if (!bookBin.seek(recordsOffset + static_cast<size_t>(i) * RECORD_SIZE + RECORD_LEVEL_OFFSET) ||
        !readPodChecked(bookBin, level)) {
      return 0;
    }
    if (level != 0) {
      continue;
    }
    if (seen == ordinal) {
      return i;
    }
    seen++;
  }
  return chapterCount - 1;
}
