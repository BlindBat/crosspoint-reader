#include "Fb2ReaderMath.h"

#include <algorithm>

namespace fb2_reader {

int clampPercent(const int percent) {
  if (percent < 0) return 0;
  if (percent > 100) return 100;
  return percent;
}

int roundedPercent(const float percent) { return clampPercent(static_cast<int>(percent + 0.5f)); }

PercentTarget percentToSection(int percent, const SectionSizes& sizes) {
  PercentTarget target{0, 0.0f, false};
  if (sizes.bookSize == 0) return target;

  percent = clampPercent(percent);
  size_t targetSize = (sizes.bookSize / 100) * static_cast<size_t>(percent) +
                      (sizes.bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) targetSize = sizes.bookSize - 1;

  if (sizes.count == 0) return target;

  int targetIdx = sizes.count - 1;
  size_t prevCumulative = 0;
  for (int i = 0; i < sizes.count; i++) {
    const size_t cumulative = sizes.cumulative(sizes.ctx, i);
    if (targetSize <= cumulative) {
      targetIdx = i;
      prevCumulative = (i > 0) ? sizes.cumulative(sizes.ctx, i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = sizes.cumulative(sizes.ctx, targetIdx);
  const size_t sectionSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  const float progress =
      (sectionSize == 0) ? 0.0f
                         : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(sectionSize);
  target.sectionIndex = targetIdx;
  target.sectionProgress = std::clamp(progress, 0.0f, 1.0f);
  target.valid = true;
  return target;
}

int rescalePage(const int currentPage, const int oldPageCount, const int newPageCount) {
  if (oldPageCount <= 0 || oldPageCount == newPageCount) return currentPage;
  const float progress = static_cast<float>(currentPage) / static_cast<float>(oldPageCount);
  return static_cast<int>(progress * static_cast<float>(newPageCount));
}

int percentJumpPage(const float sectionProgress, const int pageCount) {
  if (pageCount <= 0) return 0;
  int newPage = static_cast<int>(sectionProgress * static_cast<float>(pageCount));
  if (newPage >= pageCount) newPage = pageCount - 1;
  return newPage;
}

Progress decodeProgress(const uint8_t* data, const int size) {
  Progress progress{0, 0, 0, false, false};
  if (size != 4 && size != 6) return progress;
  progress.sectionIndex = data[0] | (data[1] << 8);
  progress.page = data[2] | (data[3] << 8);
  progress.valid = true;
  if (size == 6) {
    progress.pageCount = data[4] | (data[5] << 8);
    progress.hasPageCount = true;
  }
  return progress;
}

}  // namespace fb2_reader
