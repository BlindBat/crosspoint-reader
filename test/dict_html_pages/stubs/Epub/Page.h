#pragma once

// Host stub for lib/Epub/Epub/Page.h: DictHtmlPages only counts a page's
// elements, so the real element hierarchy (fonts, renderer) is not needed.

#include <memory>
#include <vector>

class PageElement {
 public:
  virtual ~PageElement() = default;
};

class Page {
 public:
  std::vector<std::shared_ptr<PageElement>> elements;
};
