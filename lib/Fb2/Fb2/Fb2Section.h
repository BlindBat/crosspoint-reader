#pragma once
#include <Epub/ReaderCallbacks.h>
#include <Epub/ReaderRenderSpec.h>
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Fb2.h"

class Page;
class GfxRenderer;
class Fb2SectionParser;

// One laid-out FB2 section, cached as a binary page file with a page-offset
// LUT for random access. Simpler than the Epub Section: FB2 sections are
// text-only, so there is no CSS, no images and no anchor map.
//
// A build can run in one shot (createSectionFile) or in bounded slices
// (startBuild / buildSomeMore), which is what lets the reader lay out the
// chapter ahead without blocking. Both produce byte-identical files.
class Fb2Section {
  std::shared_ptr<Fb2> fb2;
  const int sectionIndex;
  GfxRenderer& renderer;
  std::string filePath;
  HalFile file;

  void writeSectionFileHeader(const ReaderRenderSpec& spec);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  // Context and trampoline for Fb2PageCompleteFn: the build's page-offset LUT
  // lives in the BuildContext, so the callback carries it alongside `this`.
  struct BuildLutContext {
    Fb2Section* section;
    std::vector<uint32_t>* lut;
  };

 private:
  // Held only while a build is in progress. Carries the live parser (which owns
  // the expat instance and the open input file) and the page-offset LUT, both of
  // which must survive between slices.
  struct BuildContext {
    std::unique_ptr<Fb2SectionParser> parser;
    std::vector<uint32_t> lut;
    BuildLutContext lutCtx;
  };
  std::unique_ptr<BuildContext> build_;
  bool buildComplete_ = false;

  // Builds write here and the result is swapped over filePath only on success,
  // so a pre-existing cache file stays valid until the moment it is replaced.
  std::string binTmpPath() const { return filePath + ".part"; }
  // Write the LUT, patch the header, close, and swap the .part over filePath.
  bool finalizeBuild();

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  // Constructor and destructor are out-of-line: BuildContext holds a unique_ptr
  // to the forward-declared Fb2SectionParser, whose full definition is only
  // visible in the .cpp.
  explicit Fb2Section(const std::shared_ptr<Fb2>& fb2, int sectionIndex, GfxRenderer& renderer);
  ~Fb2Section();
  // Load the cached section file if it exists and matches the render spec.
  bool loadSectionFile(const ReaderRenderSpec& spec);
  bool clearCache() const;
  // Parse the FB2 section and lay it out into the section file, in one call.
  bool createSectionFile(const ReaderRenderSpec& spec, const BuildPopupFn& popupFn = {});

  // Incremental build: lay the section out a slice at a time so the reader can
  // keep serving input between slices.
  //   if (!startBuild(spec)) fail;
  //   each tick: buildSomeMore(pages, bytes); until isBuildComplete()
  bool startBuild(const ReaderRenderSpec& spec, const BuildPopupFn& popupFn = {});
  // Advance the build by at most maxPages pages or maxBytes bytes of input
  // (0 = unbounded for that dimension). Returns false when the build failed, in
  // which case it has already been abandoned. Sets isBuildComplete() when done.
  bool buildSomeMore(int maxPages, uint32_t maxBytes);
  bool isBuilding() const { return static_cast<bool>(build_); }
  bool isBuildComplete() const { return buildComplete_; }
  // Drop an in-progress build and remove its .part. Any pre-existing cache file
  // at filePath is left untouched.
  void abandonBuild();

  static void appendBuiltPage(void* ctx, std::unique_ptr<Page> page);
  std::unique_ptr<Page> loadPage(int page);
};
