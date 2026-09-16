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

// progress.bin payload: u16 section, u16 page, optional u16 page count.
struct Progress {
  int sectionIndex;
  int page;
  int pageCount;
  bool hasPageCount;
  bool valid;  // size was 4 or 6
};
Progress decodeProgress(const uint8_t* data, int size);

}  // namespace fb2_reader
