#pragma once

// Recording fake for the two cache callbacks the package parsers invoke.
// The real cache streams these to SD; here they land in vectors so a test can
// assert spine order, TOC nesting and href/anchor splitting directly.

#include <cstdint>
#include <string>
#include <vector>

class BookMetadataCache {
 public:
  struct TocRecord {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
  };

  std::vector<std::string> spine;
  std::vector<TocRecord> toc;

  void createSpineEntry(const std::string& href) { spine.push_back(href); }
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                      const uint8_t level) {
    toc.push_back(TocRecord{title, href, anchor, level});
  }
};
