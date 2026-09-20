#pragma once

#include <Fb2.h>
#include <Fb2/Fb2Section.h>

#include <memory>
#include <string>

#include "EpubReaderMenuActivity.h"
#include "Fb2ReaderMath.h"
#include "ReaderActivity.h"
#include "ReaderProgressGuard.h"

class Fb2ReaderActivity final : public ReaderActivity {
  std::shared_ptr<Fb2> fb2;
  std::unique_ptr<Fb2Section> section;
  int currentSectionIndex = 0;
  // Page to open once the section is (re)loaded; UINT16_MAX means the last page.
  int nextPageNumber = 0;
  // Position restore across a re-pagination (orientation/text-settings change):
  // the old page number is rescaled by oldTotal -> newTotal for the same section.
  int cachedSectionIndex = 0;
  int cachedSectionTotalPageCount = 0;
  // The cover is reader state, not a page: it occupies the position ahead of
  // chapter 0 page 0, so no chapter index, page number or cached page moves.
  // Empty coverBmpPath means the book has no usable cover.
  bool onCoverPage = false;
  std::string coverBmpPath;
  bool renderCoverPage();

  bool pendingPercentJump = false;
  bool pendingScreenshot = false;
  float pendingSectionProgress = 0.0f;
  uint8_t appliedOrientation = 0;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;

  // Skips a progress.bin write when the position has not moved.
  ReaderProgressGuard progressGuard;

  // Chapter-ahead prefetch. The build's only product is the chapter's section
  // file: when the reader crosses the boundary it constructs its own Fb2Section
  // and loadSectionFile() simply finds the work already done. Nothing is handed
  // over, so nothing here is on the reader's critical path.
  std::unique_ptr<Fb2Section> prefetch;
  int prefetchIndex = fb2_reader::NO_PREFETCH_TARGET;
  // Last chapter a prefetch finished (or found already cached), so it is not
  // prepared twice.
  int prefetchDoneIndex = fb2_reader::NO_PREFETCH_TARGET;
  // The spec the in-flight build was started with; a change invalidates it.
  ReaderRenderSpec prefetchSpec;
  // Origin of the idle settle timer, set when a page render finishes.
  unsigned long lastRenderCompleteMs = 0;
  // Viewport of the last render, so the tick can rebuild the spec without
  // repeating the margin arithmetic.
  uint16_t prefetchViewportWidth = 0;
  uint16_t prefetchViewportHeight = 0;

  // One bounded slice of the chapter-ahead build, or nothing if any gate fails.
  void prefetchTick();
  // Drop an in-flight prefetch and release everything it holds. Called whenever
  // the reader moves: a new chapter, a sub-activity, a settings change, exit.
  void stopPrefetch();

  // Slice budgets and the idle settle interval.
  //
  // PROVISIONAL — these three are the numbers research.md R1/R2/R3 exist to
  // choose, and they have NOT been measured on device yet. They are deliberately
  // conservative: too small only makes prefetch slower, while too large would
  // hold RenderLock long enough to stutter a page turn. Do not treat them as
  // measured values, and do not raise them without the R1/R2 readings.
  static constexpr int PREFETCH_PAGES_PER_TICK = 2;
  static constexpr uint32_t PREFETCH_BYTES_PER_TICK = 4096;
  static constexpr unsigned long PREFETCH_SETTLE_MS = 400;
  // Heap floor, reused from the EPUB reader's background build gate rather than
  // invented; research.md R4 confirms or raises it.
  static constexpr size_t PREFETCH_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t PREFETCH_MIN_MAX_ALLOC = 16 * 1024;

  void loadProgress();
  void saveProgress(int sectionIndex, int currentPage, int pageCount);
  void jumpToPercent(int percent);
  void openReaderMenu();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginLeft);
  void renderStatusBar() const;
  float bookProgressPercent() const;

  bool loadBook() override;
  std::string getBookTitle() const override { return fb2 ? fb2->getTitle() : ""; }
  std::string getBookAuthor() const override { return fb2 ? fb2->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return fb2 ? fb2->getThumbBmpPath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;
  static void showIndexingPopup(void* ctx);

 public:
  explicit Fb2ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("Fb2Reader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~Fb2ReaderActivity() override = default;

  void loop() override;
  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
