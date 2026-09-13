#pragma once

// Test-controlled Epub stand-in for the Section suite.
//
// Section.h resolves `#include "Epub.h"` through the include path (the real
// header lives one level up, in lib/Epub/), so this stub shadows it. It
// provides exactly the surface Section.cpp and ChapterHtmlSlimParser.cpp
// consume: cache path, language, spine hrefs, TOC entries (for chapter-break
// anchors), the CSS parser hook, and chapter-content streaming. Content is
// served from an in-memory href->bytes map so each test composes its own
// deterministic chapter.

#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CssParser;

class Epub {
 public:
  struct SpineEntry {
    std::string href;
  };
  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    int16_t spineIndex = -1;
  };

  std::string cachePath;
  std::string language = "en";
  std::map<std::string, std::string> items;  // itemHref -> raw content bytes
  std::vector<SpineEntry> spine;
  std::vector<TocEntry> toc;
  CssParser* cssParser = nullptr;
  // Fault injection: makes every stream attempt fail (Section retries 3x).
  bool failStreaming = false;
  // Counts HalFile stream requests, so tests can prove the unzipped-HTML
  // cache short-circuits re-inflation on later builds.
  mutable int streamCalls = 0;

  const std::string& getCachePath() const { return cachePath; }
  const std::string& getLanguage() const { return language; }

  SpineEntry getSpineItem(const int spineIndex) const {
    if (spineIndex < 0 || spineIndex >= static_cast<int>(spine.size())) return {};
    return spine[spineIndex];
  }
  int getTocItemsCount() const { return static_cast<int>(toc.size()); }
  TocEntry getTocItem(const int tocIndex) const {
    if (tocIndex < 0 || tocIndex >= static_cast<int>(toc.size())) return {};
    return toc[tocIndex];
  }
  int getTocIndexForSpineIndex(const int spineIndex) const {
    for (size_t i = 0; i < toc.size(); i++) {
      if (toc[i].spineIndex == spineIndex) return static_cast<int>(i);
    }
    return -1;
  }
  CssParser* getCssParser() const { return cssParser; }

  // Section streams the chapter HTML into a HalFile in chunks. This overload
  // is the one exercised; the template below only satisfies the parser's
  // image-probe instantiation (fixtures contain no images).
  bool readItemContentsToStream(const std::string& itemHref, HalFile& out, const size_t chunkSize,
                                bool /*allowEarlyStop*/ = false) const {
    streamCalls++;
    if (failStreaming) return false;
    const auto it = items.find(itemHref);
    if (it == items.end()) return false;
    const std::string& content = it->second;
    size_t pos = 0;
    while (pos < content.size()) {
      const size_t n = std::min(chunkSize, content.size() - pos);
      if (out.write(reinterpret_cast<const uint8_t*>(content.data()) + pos, n) != n) return false;
      pos += n;
    }
    return true;
  }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {
    return false;  // image header-probe/extract path; never hit by these fixtures
  }
};
