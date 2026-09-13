#pragma once

#include <Print.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// In-memory stand-in for lib/Epub's Epub class exposing exactly the surface
// ProgressMapper and ChapterXPathResolver consume. Each spine item carries its
// XHTML bytes; declaredSize lets the byte-based percentage math be controlled
// independently of the markup length.
struct StubSpineEntry {
  std::string href;
};

class Epub {
 public:
  struct SpineItemData {
    std::string href;
    std::string content;
    size_t declaredSize = 0;
  };

  void addSpineItem(std::string href, std::string content, const size_t declaredSize = 0) {
    SpineItemData item;
    item.href = std::move(href);
    item.content = std::move(content);
    item.declaredSize = declaredSize > 0 ? declaredSize : item.content.size();
    items.push_back(std::move(item));
  }

  int getSpineItemsCount() const { return static_cast<int>(items.size()); }

  StubSpineEntry getSpineItem(const int spineIndex) const {
    if (spineIndex < 0 || spineIndex >= getSpineItemsCount()) return {};
    return {items[static_cast<size_t>(spineIndex)].href};
  }

  size_t getCumulativeSpineItemSize(const int spineIndex) const {
    size_t total = 0;
    for (int i = 0; i <= spineIndex && i < getSpineItemsCount(); i++) {
      total += items[static_cast<size_t>(i)].declaredSize;
    }
    return total;
  }

  size_t getBookSize() const { return getCumulativeSpineItemSize(getSpineItemsCount() - 1); }

  // Mirrors Epub::calculateProgress in lib/Epub/Epub.cpp: byte-weighted blend
  // of completed chapters plus the read fraction of the current one.
  float calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
    const size_t bookSize = getBookSize();
    if (bookSize == 0) return 0.0f;
    const size_t prev = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
    const size_t cur = getCumulativeSpineItemSize(currentSpineIndex) - prev;
    return (static_cast<float>(prev) + currentSpineRead * static_cast<float>(cur)) / static_cast<float>(bookSize);
  }

  bool getItemSize(const std::string& href, size_t* size) const {
    const SpineItemData* item = findItem(href);
    if (!item || !size) return false;
    *size = item->content.size();
    return true;
  }

  bool readItemContentsToStream(const std::string& href, Print& out, const size_t chunkSize,
                                bool /*allowEarlyStop*/ = false) const {
    if (failStreaming) return false;
    const SpineItemData* item = findItem(href);
    if (!item) return false;
    const auto* data = reinterpret_cast<const uint8_t*>(item->content.data());
    size_t remaining = item->content.size();
    size_t offset = 0;
    const size_t step = chunkSize > 0 ? chunkSize : 1;
    while (remaining > 0) {
      const size_t n = std::min(step, remaining);
      out.write(data + offset, n);
      offset += n;
      remaining -= n;
    }
    return true;
  }

  std::vector<SpineItemData> items;
  bool failStreaming = false;

 private:
  const SpineItemData* findItem(const std::string& href) const {
    if (href.empty()) return nullptr;
    for (const auto& item : items) {
      if (item.href == href) return &item;
    }
    return nullptr;
  }
};
