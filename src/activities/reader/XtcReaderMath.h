#pragma once

#include <Xtc/XtcTypes.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Pure page/status-bar arithmetic for the XTC reader (FR-101), kept free of
// renderer and settings state so it compiles in the host test program.
namespace xtc_reader {

// Band cleared behind the status-bar overlay and the extra bottom padding the
// theme needs when the bar sits at the top of the page.
struct StatusBarLayout {
  int clearY;
  int clearHeight;
  int paddingBottom;
};
StatusBarLayout statusBarLayout(bool bottom, int screenHeight, int statusBarHeight, int marginTop, int marginBottom);

// Book progress in percent from a 0-based page; 0 when the book is empty.
float progressPercent(uint32_t currentPage, uint32_t pageCount);

// First chapter whose inclusive [startPage, endPage] contains page, or nullptr.
const xtc::ChapterInfo* findChapter(const std::vector<xtc::ChapterInfo>& chapters, uint32_t page);

// Row to open the chapter list on; 0 when no chapter contains page.
int findChapterIndexForPage(const std::vector<xtc::ChapterInfo>& chapters, uint32_t page);

// Status-bar numbering: chapter-relative inside a chapter, book-relative otherwise.
struct PagePosition {
  int currentPage;
  int pageCount;
  const xtc::ChapterInfo* chapter;  // nullptr when book-relative
};
PagePosition pagePosition(const std::vector<xtc::ChapterInfo>& chapters, uint32_t currentPage, uint32_t bookPageCount);

// Pull a page index past the end back onto the last page; pageCount 0 leaves it alone.
uint32_t clampPage(uint32_t page, uint32_t pageCount);

// progress.bin payload: little-endian u32 page index, clamped like clampPage.
uint32_t decodeProgress(const uint8_t data[4], uint32_t pageCount);

// Target of a relative skip, clamped to [0, pageCount]; pageCount is the end-of-book screen.
uint32_t skipTarget(uint32_t currentPage, int amount, uint32_t pageCount);

// XTH 2-bit pixel at (x, y): 0 white, 1 dark gray, 2 light gray, 3 black.
// The two planes are column-major right-to-left with 8 vertical pixels per byte
// (MSB topmost). Each plane holds (width*height+7)/8 bytes but is indexed at
// (height+7)/8 bytes per column, so the tail columns have no storage whenever the
// height is not a multiple of 8; those offsets read as white rather than past the buffer.
uint8_t xthPixelValue(const uint8_t* planes, size_t planeSize, uint16_t width, uint16_t height, uint16_t x, uint16_t y);

}  // namespace xtc_reader
