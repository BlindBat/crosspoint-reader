#include "Fb2.h"

#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

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
// v6: a body's own content ahead of its first <section> is counted in that body's first
// chapter's length; record layout is unchanged, only the values.
constexpr uint8_t FB2_CACHE_VERSION = 6;

// Lowest chapter cap any pre-v5 cache was built under. Layouts built then give a
// chapter past it no boundary of its own, so its ancestors' layouts are stale once
// the cap is gone. An unreadable old book.bin reports no version and keeps its
// sections/: rare, and accepted.
constexpr uint16_t FB2_OLD_CHAPTER_CAP = 256;

// First version built without the chapter cap. The sections/ drop below targets caches
// from the capped era, not every older version: a later bump that changes only chapter
// WEIGHTS leaves page layout valid, and dropping sections/ would cost a needless
// relayout (specs/006 research R10 measured 2.9 s for a 677-chapter metadata rebuild
// alone).
constexpr uint8_t FB2_CAP_FREE_CACHE_VERSION = 5;

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

// Cuts a string to FB2_CACHE_MAX_STRING bytes, backing up to a UTF-8 lead byte.
void clampToCacheString(std::string& value) {
  if (value.size() <= FB2_CACHE_MAX_STRING) return;
  size_t cut = FB2_CACHE_MAX_STRING;
  while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) cut--;
  value.resize(cut);
}

// A record moves as one 20-byte read or write: each HalFile call takes the storage
// mutex, and 7 per-field calls per record made chapter-list windows and first opens
// measurably slow on device (specs/006 research R10). memcpy, never a cast: the
// buffer has no alignment (RISC-V faults on unaligned wide loads).
void packRecord(const Record& r, uint8_t* b) {
  memcpy(b + 0, &r.titleOffset, 4);
  memcpy(b + 4, &r.titleLength, 2);
  memcpy(b + 6, &r.fileOffset, 4);
  memcpy(b + 10, &r.ownLength, 4);
  memcpy(b + 14, &r.cumulativeLength, 4);
  b[RECORD_LEVEL_OFFSET] = r.level;
  b[RECORD_LEVEL_OFFSET + 1] = r.flags;
}

bool readRecord(HalFile& file, Record& r) {
  uint8_t b[RECORD_SIZE];
  if (file.read(b, RECORD_SIZE) != static_cast<int>(RECORD_SIZE)) return false;
  memcpy(&r.titleOffset, b + 0, 4);
  memcpy(&r.titleLength, b + 4, 2);
  memcpy(&r.fileOffset, b + 6, 4);
  memcpy(&r.ownLength, b + 10, 4);
  memcpy(&r.cumulativeLength, b + 14, 4);
  r.level = b[RECORD_LEVEL_OFFSET];
  r.flags = b[RECORD_LEVEL_OFFSET + 1];
  return true;
}

bool writeRecord(HalFile& file, const Record& r) {
  uint8_t b[RECORD_SIZE];
  packRecord(r, b);
  return file.write(b, RECORD_SIZE) == RECORD_SIZE;
}

// Build-time buffer for the append-only streams (titles.tmp during the parse,
// book.bin during assembly), so their small writes do not interleave with the
// source or temp-file reads through SdFat's single shared sector cache. Fixed size,
// independent of chapter count; allocation failure degrades to unbuffered writes.
constexpr size_t BUILD_IO_BUFFER_SIZE = 1024;

// The parser's chapter sink during a build: fixed records in chapters.tmp, one
// slot appended per start tag and patched at the end tag (end tags arrive
// children-first, so slots cannot simply be appended in order), and title bytes
// appended to titles.tmp. Nothing is held per chapter in RAM.
// ponytail: record slots are still unbuffered (they are patched in place); keep a
// RAM window of open-section slots if first open measures slow on device again.
struct BuildSink {
  HalFile chapters;
  HalFile titles;
  serialization::BufferedFileWriter* titlesOut = nullptr;
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
    if (r.titleLength > 0) self->titlesOut->write(chapter.title.data(), r.titleLength);
    self->titlesSize += r.titleLength;
    return self->chapters.seek(static_cast<size_t>(index) * RECORD_SIZE) && writeRecord(self->chapters, r);
  }
};
}  // namespace

Fb2::Fb2(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  cachePath = cacheDir + "/fb2_" + std::to_string(std::hash<std::string>{}(this->filepath));
}

bool Fb2::loadMetadataCache(uint8_t* rejectedVersion) {
  if (rejectedVersion) *rejectedVersion = 0;
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
    if (rejectedVersion) *rejectedVersion = version;
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
  // more than the limit, so anything else is corrupt, not empty.
  if (count == 0 || count > FB2_CHAPTER_INDEX_LIMIT) {
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

  // Each phase runs in its own scope so every file is closed before cleanup()
  // removes it: SdFat must not remove a file that is still open.
  uint32_t count = 0;
  uint32_t builtTitlesSize = 0;
  bool ok;
  {
    BuildSink sink;
    ok = Storage.openFileForWrite("FB2", chaptersPath, sink.chapters) &&
         Storage.openFileForWrite("FB2", titlesPath, sink.titles);
    if (ok) {
      serialization::BufferedFileWriter titlesOut(sink.titles, BUILD_IO_BUFFER_SIZE);
      sink.titlesOut = &titlesOut;
      Fb2MetadataParser parser(filepath, Fb2ChapterSink{&sink, &BuildSink::reserve, &BuildSink::write});
      // A failed buffered title write shows up on the flush.
      ok = parser.parse() && titlesOut.flush();
      if (ok) {
        title = parser.getTitle();
        author = parser.getAuthor();
        language = parser.getLanguage();
        coverBinaryId = parser.getCoverBinaryId();
      }
    }
    count = sink.count;
    builtTitlesSize = sink.titlesSize;
  }
  if (!ok || count == 0 || count > FB2_CHAPTER_INDEX_LIMIT) {
    LOG_ERR("FB2", "Failed to parse metadata (%u chapters)", count);
    return cleanup();
  }
  clampToCacheString(title);
  clampToCacheString(author);
  clampToCacheString(language);
  clampToCacheString(coverBinaryId);

  {
    HalFile out, chapters, titles;
    ok = Storage.openFileForWrite("FB2", cacheFile, out) && Storage.openFileForRead("FB2", chaptersPath, chapters) &&
         Storage.openFileForRead("FB2", titlesPath, titles);
    if (ok) {
      serialization::BufferedFileWriter w(out, BUILD_IO_BUFFER_SIZE);
      auto pod = [&w](const auto& value) { w.write(&value, sizeof(value)); };
      auto str = [&](const std::string& value) {
        pod(static_cast<uint32_t>(value.size()));
        w.write(value.data(), value.size());
      };
      pod(FB2_CACHE_VERSION);
      str(title);
      str(author);
      str(language);
      str(coverBinaryId);
      pod(static_cast<uint16_t>(count));
      pod(builtTitlesSize);
      uint32_t cumulative = 0;
      for (uint32_t i = 0; ok && i < count; i++) {
        Record r;
        ok = readRecord(chapters, r) && cumulative + r.ownLength >= cumulative;
        cumulative += r.ownLength;
        r.cumulativeLength = cumulative;
        uint8_t packed[RECORD_SIZE];
        packRecord(r, packed);
        w.write(packed, RECORD_SIZE);
      }
      uint8_t buffer[128];
      for (uint32_t copied = 0; ok && copied < builtTitlesSize;) {
        const uint32_t want = builtTitlesSize - copied < sizeof(buffer) ? builtTitlesSize - copied : sizeof(buffer);
        ok = titles.read(buffer, want) == static_cast<int>(want);
        w.write(buffer, want);
        copied += want;
      }
      ok = w.flush() && ok;
    }
  }
  if (!ok) {
    return cleanup();
  }
  Storage.remove(chaptersPath.c_str());
  Storage.remove(titlesPath.c_str());
  LOG_DBG("FB2", "Built metadata cache: %u chapters", count);
  return true;
}

bool Fb2::load(const bool buildIfMissing) {
  LOG_DBG("FB2", "Loading FB2: %s", filepath.c_str());

  // Try cache first
  uint8_t rejectedVersion = 0;
  if (loadMetadataCache(&rejectedVersion)) {
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
  if (rejectedVersion != 0 && rejectedVersion < FB2_CAP_FREE_CACHE_VERSION && chapterCount > FB2_OLD_CHAPTER_CAP) {
    LOG_DBG("FB2", "Dropping layouts built under the old %u-chapter cap", FB2_OLD_CHAPTER_CAP);
    Storage.removeDir((cachePath + "/sections").c_str());
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

int Fb2::getTocEntries(const int first, const int count, SectionInfo* out, HalFile& bookBin) const {
  // SdFat caches one sector, and the record and title areas are far apart, so
  // alternating record/title reads reloaded it twice per entry: 66 ms per 24-entry
  // window on device (specs/006 research R10). All records first, then all titles.
  if (first < 0 || first >= chapterCount || count <= 0) return 0;
  const int n = std::min({count, TOC_BATCH, chapterCount - first});
  uint32_t titleOffsets[TOC_BATCH];
  uint16_t titleLengths[TOC_BATCH];
  if (!bookBin.seek(recordsOffset + static_cast<size_t>(first) * RECORD_SIZE)) return 0;
  int read = 0;
  for (; read < n; read++) {
    Record r;
    if (!readRecord(bookBin, r)) break;
    const bool titleInBounds =
        r.titleLength <= FB2_CACHE_MAX_STRING && static_cast<uint64_t>(r.titleOffset) + r.titleLength <= titlesSize;
    titleOffsets[read] = r.titleOffset;
    titleLengths[read] = titleInBounds ? r.titleLength : 0;
    SectionInfo& info = out[read];
    info.title.clear();
    info.fileOffset = r.fileOffset;
    info.length = r.ownLength;
    info.cumulativeLength = r.cumulativeLength;
    info.level = r.level;
    info.titleDerived = (r.flags & FB2_CHAPTER_FLAG_TITLE_DERIVED) != 0 ? 1 : 0;
  }
  for (int i = 0; i < read; i++) {
    if (titleLengths[i] == 0) continue;
    std::string& title = out[i].title;
    title.resize(titleLengths[i]);
    if (!bookBin.seek(titlesOffset + titleOffsets[i]) ||
        bookBin.read(reinterpret_cast<uint8_t*>(&title[0]), titleLengths[i]) != static_cast<int>(titleLengths[i])) {
      LOG_ERR("FB2", "Chapter %d title unreadable", first + i);
      title.clear();
    }
  }
  return read;
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
