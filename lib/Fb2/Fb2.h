#pragma once

#include <string>
#include <vector>

class Fb2 {
 public:
  // Chapter metadata is RAM-resident, so the count is bounded. Past the ceiling a
  // <section> is no longer a chapter boundary: it reads as part of the chapter
  // containing it, so no text is lost.
  //
  // 256 is measured, not rounded. Across 2,899 real FB2 books the worst costs
  // 99,716 bytes of chapter metadata at a ceiling of 1024 — 44.9% of the 222,180
  // bytes of heap the C3 has left after static allocation and the framebuffer —
  // against 43,732 bytes (19.7%) at 256. The price is coarser navigation in 22 of
  // those books (0.76%), none of them novels.
  // ponytail: RAM-resident chapter list, so the count needs a ceiling at all, and a
  // count is a weak proxy for the bytes it is protecting (books at 256 chapters span
  // 9-44KB). Upgrade path is an SD-resident seekable LUT like BookMetadataCache uses
  // for EPUB, which deletes this constant instead of tuning it.
  static constexpr uint16_t FB2_MAX_CHAPTERS = 256;

  // Character cap for a DERIVED label only. A <title> the book supplies is stored
  // as-is: measured across 2,899 real books, capping real titles too saves 3% of
  // the worst book's metadata, which does not pay for changing what the reader
  // sees. A derived label is prose and would otherwise be a whole paragraph.
  static constexpr uint16_t FB2_MAX_LABEL_CHARS = 64;

  // One <section> of a reading body. The vector index is the chapter id used by
  // sections/<index>.bin, progress.bin and the chapter list.
  struct SectionInfo {
    std::string title;         // its OWN title, a derived label, or empty when it has neither
    size_t fileOffset = 0;     // offset of its "<section" start tag
    size_t length = 0;         // own bytes: full span minus child chapters' spans
    uint8_t level = 0;         // nesting depth in the body; 0 = direct child of <body>
    uint8_t titleDerived = 0;  // 1 when `title` came from the first paragraph, not a <title>
  };

 private:
  std::string filepath;
  std::string cachePath;
  std::string title;
  std::string author;
  std::string language;
  std::string coverBinaryId;
  std::vector<SectionInfo> sections;
  bool loaded = false;

  bool parseMetadata();
  bool loadMetadataCache();
  bool saveMetadataCache() const;

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

  // Sections (spine-like navigation)
  int getSectionCount() const;
  const SectionInfo& getSectionInfo(int index) const;
  size_t getBookSize() const;
  size_t getCumulativeSectionSize(int index) const;
  float calculateProgress(int currentSectionIndex, float currentSectionRead) const;

  // TOC: one entry per chapter, so these are projections of `sections` and the
  // two index mappings are the identity.
  int getTocCount() const;
  const SectionInfo& getTocEntry(int index) const;
  int getTocIndexForSectionIndex(int sectionIndex) const;
  int getSectionIndexForTocIndex(int tocIndex) const;

  // Index of the (ordinal+1)-th level-0 chapter, clamped to the last chapter.
  // Resolves a reading position saved under the old top-level-only numbering.
  int firstChapterOfTopLevel(int ordinal) const;

  // Cover binary ID (for cover extractor)
  const std::string& getCoverBinaryId() const { return coverBinaryId; }
};
