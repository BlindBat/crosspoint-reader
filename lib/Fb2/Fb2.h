#pragma once

#include <string>
#include <vector>

class Fb2 {
 public:
  // Chapter metadata is RAM-resident, so the count is bounded. Past the cap a
  // <section> is no longer a chapter boundary: it reads as part of the chapter
  // containing it, so no text is lost.
  // ponytail: RAM-resident chapter list, capped. Upgrade path is an SD-resident
  // seekable LUT like BookMetadataCache uses for EPUB, if a real book needs more.
  static constexpr uint16_t FB2_MAX_CHAPTERS = 1024;

  // One <section> of a reading body. The vector index is the chapter id used by
  // sections/<index>.bin, progress.bin and the chapter list.
  struct SectionInfo {
    std::string title;      // the section's OWN title, empty when it has none
    size_t fileOffset = 0;  // offset of its "<section" start tag
    size_t length = 0;      // own bytes: full span minus child chapters' spans
    uint8_t level = 0;      // nesting depth in the body; 0 = direct child of <body>
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
