#pragma once

// Host stub for the EPUB chapter parser as driven by src/util/DictHtmlPages.cpp.
// parseAndBuildPages() reads back the staged XHTML file (so tests can assert
// on the normalised markup) and then emits pages from a test-supplied plan,
// which is how the page-count, element-count and retain-heap gates are hit.

#include <Epub/Page.h>
#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;
class Epub;
class CssParser;

namespace dhtstub {
// Elements per emitted page, in order. Empty -> the parser emits nothing.
inline std::vector<size_t> pagePlan;
// Result of parseAndBuildPages() after the plan has been played.
inline bool parseResult = true;
// Called before page i is handed to the completion callback (heap shaping).
inline void (*beforePage)(size_t index) = nullptr;
// What the parser found in the staged file; empty when it could not open it.
inline std::string stagedHtml;
inline bool stagedOpenFailed = false;
inline int parsersConstructed = 0;

inline void reset() {
  pagePlan.clear();
  parseResult = true;
  beforePage = nullptr;
  stagedHtml.clear();
  stagedOpenFailed = false;
  parsersConstructed = 0;
}
}  // namespace dhtstub

class ChapterHtmlSlimParser {
 public:
  using CompletePageFn = std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>;

  explicit ChapterHtmlSlimParser(std::shared_ptr<Epub>, const std::string& filepath, GfxRenderer&, int, float, bool,
                                 uint8_t, uint16_t, uint16_t, bool, bool, const CompletePageFn& completePageFn, bool,
                                 const std::string&, const std::string&, uint8_t = 0,
                                 std::vector<std::string> = {}, const std::function<void()>& = nullptr,
                                 const CssParser* = nullptr)
      : filepath(filepath), completePageFn(completePageFn) {
    dhtstub::parsersConstructed++;
  }

  bool parseAndBuildPages() {
    dhtstub::stagedHtml.clear();
    HalFile file = Storage.open(filepath.c_str(), O_RDONLY);
    if (!file) {
      dhtstub::stagedOpenFailed = true;
      return false;
    }
    char buf[256];
    int n;
    while ((n = file.read(buf, sizeof(buf))) > 0) dhtstub::stagedHtml.append(buf, static_cast<size_t>(n));
    file.close();

    for (size_t i = 0; i < dhtstub::pagePlan.size(); i++) {
      if (dhtstub::beforePage) dhtstub::beforePage(i);
      auto page = std::make_unique<Page>();
      page->elements.resize(dhtstub::pagePlan[i], std::make_shared<PageElement>());
      completePageFn(std::move(page), 0, 0, 0);
    }
    return dhtstub::parseResult;
  }

 private:
  std::string filepath;
  CompletePageFn completePageFn;
};
