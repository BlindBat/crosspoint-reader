#pragma once

#include <cstddef>
#include <cstdint>

// Pure progress/percent arithmetic for the FB2 reader (FR-105), kept free of
// Fb2/section state so it compiles in the host test program.
namespace fb2_reader {

int clampPercent(int percent);

// Nearest whole percent of a 0-100 float, clamped.
int roundedPercent(float percent);

// Cumulative section byte sizes, read through a callback so no vector is built.
struct SectionSizes {
  const void* ctx;
  size_t (*cumulative)(const void* ctx, int index);  // bytes through the end of section index
  int count;
  size_t bookSize;
};

// Section holding a book percentage and the fraction of that section read;
// valid is false when the book has no bytes or no sections.
struct PercentTarget {
  int sectionIndex;
  float sectionProgress;
  bool valid;
};
PercentTarget percentToSection(int percent, const SectionSizes& sizes);

// Same relative position after a re-pagination; unchanged when the old count is
// unusable or equal to the new one.
int rescalePage(int currentPage, int oldPageCount, int newPageCount);

// Page for a fraction of a section, clamped to the last page.
int percentJumpPage(float sectionProgress, int pageCount);

// progress.bin payload: u16 chapter, u16 page, u16 page count, u16 marker.
// The marker is what distinguishes the current form from the pre-nested-chapter
// one: a 4- or 6-byte payload has no marker, so its first field is a TOP-LEVEL
// ORDINAL under the old numbering rather than a chapter index. The caller then
// resolves it through Fb2::firstChapterOfTopLevel() and starts at page 0, and the
// next save writes the 8-byte form, so each book migrates once.
constexpr uint16_t PROGRESS_MARKER = 0xFB02;
constexpr int PROGRESS_SIZE = 8;

struct Progress {
  int sectionIndex;  // chapter index, or a top-level ordinal when legacyOrdinal
  int page;          // always 0 for a legacy payload
  int pageCount;
  bool hasPageCount;
  bool legacyOrdinal;  // payload predates one-chapter-per-section numbering
  bool valid;          // size was 4, 6, or 8 with the right marker
};
Progress decodeProgress(const uint8_t* data, int size);

// Fills `data` (PROGRESS_SIZE bytes) with the current payload form and returns
// the number of bytes written.
int encodeProgress(uint8_t* data, int sectionIndex, int page, int pageCount);

}  // namespace fb2_reader
