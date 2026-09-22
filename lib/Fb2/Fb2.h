#pragma once

#include <cstdint>
#include <string>

class HalFile;

class Fb2 {
 public:
  // Highest chapter the UI list can address: its row value and selection index are
  // int16_t (FreeInkUI lists/list.h), so a higher chapter could not be selected. Not
  // a memory budget: chapter metadata lives in book.bin. 18x the largest of 2,899
  // real books (1,772 chapters). Past it a nested <section> reads as part of the
  // chapter containing it; a top-level one has no containing chapter and is
  // unreachable, a documented ceiling rather than a supported case.
  static constexpr uint16_t FB2_CHAPTER_INDEX_LIMIT = INT16_MAX;

  // Character cap for a DERIVED label only. A <title> the book supplies is stored
  // as-is: measured across 2,899 real books, capping real titles too saves 3% of
  // the worst book's metadata, which does not pay for changing what the reader
  // sees. A derived label is prose and would otherwise be a whole paragraph.
  static constexpr uint16_t FB2_MAX_LABEL_CHARS = 64;

  // One <section> of a reading body, read from book.bin on demand and returned by
  // value: the book keeps no per-chapter record in RAM. Its chapter number is the
  // id used by sections/<index>.bin, progress.bin and the chapter list.
  struct SectionInfo {
    std::string title;            // its OWN title, a derived label, or empty when it has neither
    size_t fileOffset = 0;        // offset of its "<section" start tag
    size_t length = 0;            // own bytes: full span minus child chapters' spans
    size_t cumulativeLength = 0;  // sum of `length` over chapters 0..this one
    uint8_t level = 0;            // nesting depth in the body; 0 = direct child of <body>
    uint8_t titleDerived = 0;     // 1 when `title` came from the first paragraph, not a <title>
  };

 private:
  std::string filepath;
  std::string cachePath;
  std::string title;
  std::string author;
  std::string language;
  std::string coverBinaryId;
  uint16_t chapterCount = 0;
  uint32_t recordsOffset = 0;  // book.bin offset of chapter record 0
  uint32_t titlesOffset = 0;   // book.bin offset of the title area
  uint32_t titlesSize = 0;
  uint32_t bookSize = 0;  // cumulativeLength of the last chapter
  bool loaded = false;

  bool buildMetadataCache();
  // rejectedVersion, when given, receives the version byte of a cache rejected for
  // having the wrong version (0 otherwise).
  bool loadMetadataCache(uint8_t* rejectedVersion = nullptr);

 public:
  explicit Fb2(std::string filepath, const std::string& cacheDir);
  ~Fb2() = default;

  bool load(bool buildIfMissing = true);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;

  // Cover/thumbnail
  std::string getCoverBmpPath() const;
  bool generateCoverBmp() const;
  std::string getThumbBmpPath() const;
  std::string getThumbBmpPath(int height) const;
  bool generateThumbBmp(int height) const;

  // Sections (spine-like navigation). Each lookup is one record read from book.bin;
  // an out-of-range index or a failed read returns an empty SectionInfo, which the
  // UI shows as "Unnamed". The overloads taking `bookBin` reuse a handle the
  // caller opened with openIndex(), so a batch of lookups opens the file once.
  int getSectionCount() const;
  SectionInfo getSectionInfo(int index) const;
  SectionInfo getSectionInfo(int index, HalFile& bookBin) const;
  bool openIndex(HalFile& bookBin) const;
  size_t getBookSize() const;
  size_t getCumulativeSectionSize(int index) const;
  size_t getCumulativeSectionSize(int index, HalFile& bookBin) const;
  // No I/O: the chapter carries its own length and running total.
  float calculateProgress(const SectionInfo& chapter, float chapterRead) const;

  // TOC: one entry per chapter, so these are projections of the chapter list and
  // the two index mappings are the identity.
  int getTocCount() const;
  SectionInfo getTocEntry(int index) const;
  SectionInfo getTocEntry(int index, HalFile& bookBin) const;
  int getTocIndexForSectionIndex(int sectionIndex) const;
  int getSectionIndexForTocIndex(int tocIndex) const;

  // Index of the (ordinal+1)-th level-0 chapter, clamped to the last chapter.
  // Resolves a reading position saved under the old top-level-only numbering.
  int firstChapterOfTopLevel(int ordinal) const;

  // Cover binary ID (for cover extractor)
  const std::string& getCoverBinaryId() const { return coverBinaryId; }
};

// Where Fb2MetadataParser sends chapters. `reserve` runs at a chapter's start tag
// and claims the next chapter number; `write` runs once per reserved chapter with
// its final fields, in end-tag order (children before parents), and may move from
// them. A false return from either stops the parse.
struct Fb2ChapterSink {
  void* ctx;
  bool (*reserve)(void* ctx);
  bool (*write)(void* ctx, uint16_t index, Fb2::SectionInfo& chapter);
};
