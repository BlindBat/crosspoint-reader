#include "XtcReaderMath.h"

#include <algorithm>

namespace xtc_reader {

StatusBarLayout statusBarLayout(const bool bottom, const int screenHeight, const int statusBarHeight,
                                const int marginTop, const int marginBottom) {
  StatusBarLayout layout{};
  if (bottom) {
    layout.clearY = screenHeight - marginBottom - statusBarHeight - 4;
    if (layout.clearY < 0) {
      layout.clearY = 0;
    }
    layout.clearHeight = screenHeight - marginBottom - layout.clearY;
    layout.paddingBottom = 0;
  } else {
    layout.clearY = marginTop;
    layout.clearHeight = statusBarHeight + 4;
    layout.paddingBottom = screenHeight - statusBarHeight - marginBottom - marginTop - 4;
  }
  return layout;
}

float progressPercent(const uint32_t currentPage, const uint32_t pageCount) {
  if (pageCount == 0) {
    return 0.0f;
  }
  const int displayPage = static_cast<int>(currentPage) + 1;
  return (static_cast<float>(displayPage) * 100.0f) / static_cast<float>(pageCount);
}

const xtc::ChapterInfo* findChapter(const std::vector<xtc::ChapterInfo>& chapters, const uint32_t page) {
  const auto it = std::find_if(chapters.begin(), chapters.end(), [page](const xtc::ChapterInfo& chapter) {
    return page >= chapter.startPage && page <= chapter.endPage;
  });
  return it != chapters.end() ? &*it : nullptr;
}

int findChapterIndexForPage(const std::vector<xtc::ChapterInfo>& chapters, const uint32_t page) {
  const xtc::ChapterInfo* chapter = findChapter(chapters, page);
  return chapter ? static_cast<int>(chapter - chapters.data()) : 0;
}

PagePosition pagePosition(const std::vector<xtc::ChapterInfo>& chapters, const uint32_t currentPage,
                          const uint32_t bookPageCount) {
  const xtc::ChapterInfo* chapter = findChapter(chapters, currentPage);
  if (!chapter || chapter->endPage < chapter->startPage) {
    return PagePosition{static_cast<int>(currentPage) + 1, static_cast<int>(bookPageCount), nullptr};
  }
  return PagePosition{static_cast<int>(currentPage - chapter->startPage) + 1,
                      static_cast<int>(chapter->endPage - chapter->startPage) + 1, chapter};
}

uint32_t clampPage(const uint32_t page, const uint32_t pageCount) {
  if (pageCount > 0 && page >= pageCount) {
    return pageCount - 1;
  }
  return page;
}

uint32_t decodeProgress(const uint8_t data[4], const uint32_t pageCount) {
  const uint32_t page = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  return clampPage(page, pageCount);
}

uint32_t skipTarget(const uint32_t currentPage, const int amount, const uint32_t pageCount) {
  int newPage = static_cast<int>(currentPage) + amount;
  if (newPage < 0) newPage = 0;
  if (newPage > static_cast<int>(pageCount)) newPage = static_cast<int>(pageCount);
  return static_cast<uint32_t>(newPage);
}

uint8_t xthPixelValue(const uint8_t* planes, const size_t planeSize, const uint16_t width, const uint16_t height,
                      const uint16_t x, const uint16_t y) {
  if (!planes || x >= width || y >= height) return 0;
  const size_t colIndex = static_cast<size_t>(width) - 1 - x;
  const size_t colBytes = (static_cast<size_t>(height) + 7) / 8;
  const size_t byteOffset = colIndex * colBytes + y / 8;
  if (byteOffset >= planeSize) return 0;
  const uint8_t bitInByte = static_cast<uint8_t>(7 - (y % 8));
  const uint8_t bit1 = (planes[byteOffset] >> bitInByte) & 1;
  const uint8_t bit2 = (planes[planeSize + byteOffset] >> bitInByte) & 1;
  return static_cast<uint8_t>((bit1 << 1) | bit2);
}

}  // namespace xtc_reader
