#pragma once
#include <Epub/ReaderRenderSpec.h>
#include <HalStorage.h>

#include <functional>
#include <memory>
#include <string>

#include "Fb2.h"

class Page;
class GfxRenderer;

// One laid-out FB2 section, cached as a binary page file with a page-offset
// LUT for random access. Simpler than the Epub Section: FB2 sections are
// text-only, so the whole section is built in one shot (no incremental builds
// or partial files).
class Fb2Section {
  std::shared_ptr<Fb2> fb2;
  const int sectionIndex;
  GfxRenderer& renderer;
  std::string filePath;
  HalFile file;

  void writeSectionFileHeader(const ReaderRenderSpec& spec);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit Fb2Section(const std::shared_ptr<Fb2>& fb2, const int sectionIndex, GfxRenderer& renderer)
      : fb2(fb2),
        sectionIndex(sectionIndex),
        renderer(renderer),
        filePath(fb2->getCachePath() + "/sections/" + std::to_string(sectionIndex) + ".bin") {}
  ~Fb2Section() = default;
  // Load the cached section file if it exists and matches the render spec.
  bool loadSectionFile(const ReaderRenderSpec& spec);
  bool clearCache() const;
  // Parse the FB2 section and lay it out into the section file.
  bool createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn = nullptr);
  std::unique_ptr<Page> loadPage(int page);
};
