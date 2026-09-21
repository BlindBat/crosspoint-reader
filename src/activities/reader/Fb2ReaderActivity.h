#pragma once

#include <Fb2.h>
#include <Fb2/Fb2Section.h>
#include <HalPowerManager.h>

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
  // Held for the lifetime of an in-flight build. The reader idles at 10 MHz on
  // the C3 (HalPowerManager::LOW_POWER_FREQ, entered 3 s after the last input),
  // which is exactly when prefetch runs: measured on device, a slice that costs
  // 162 ms at 160 MHz costs 2.5-3.2 s downclocked, and throughput falls from
  // 24.7 KB/s to 1.5 KB/s -- slow enough that a chapter never finishes, and long
  // enough that a slice blocks a page turn for longer than the turn itself.
  // Lock is non-movable, hence the unique_ptr.
  std::unique_ptr<HalPowerManager::Lock> prefetchClockLock;
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

  // Slice budgets. Measured on an Xteink X4 (ESP32-C3) against a 1,239,651-byte
  // 23-chapter FB2, logged in research.md R1/R2.
  //
  // The ceiling a slice must stay under is one page render, measured at 996 ms
  // mean / 1174 ms worst on this panel. A slice holds RenderLock, so its worst
  // case is what a page turn can be made to wait for.
  //
  // The BYTE budget is what bounds slice duration, not the page budget: at
  // b=4096 the worst slice was 200 ms, while a one-page budget with bytes
  // unbounded was 322 ms. FB2 scans from byte 0 to reach a chapter, so EPUB's
  // page-only pacing would be the weaker bound here.
  //
  // 4096 is the knee. Total build time is flat across every budget tried
  // (2586-2606 ms from 1 KB to 32 KB), so a larger budget buys no throughput and
  // only lengthens the worst block: 8192 -> 380 ms, 16384 -> 734 ms,
  // 32768 -> 1458 ms, which exceeds a whole page render. Going smaller is safer
  // per slice but multiplies slice count (a late chapter needs 291 slices at
  // 4096 against 1162 at 1024), and each slice is one loop iteration, so
  // preparation would take proportionally longer in wall-clock.
  static constexpr uint32_t PREFETCH_BYTES_PER_TICK = 4096;
  // Backstop only. At 4096 bytes a slice lays out ~1.1 pages, so this never
  // binds in the measured data; it guards a chapter with atypically dense
  // pagination from turning one slice into many page layouts.
  static constexpr int PREFETCH_PAGES_PER_TICK = 4;
  // Idle settle before a prefetch may start. Measured on the X4 over 20 turns
  // (research.md R3): a reader holding page-forward turns at a 998 ms floor --
  // the panel's own render time, 996 ms mean -- while normal reading leaves
  // 17.6 s or more between turns. 1500 ms sits above the skim floor and far
  // below the reading floor, so a skimming reader never starts a prefetch that
  // their next turn would immediately interrupt, and a reading one starts it
  // almost at once.
  static constexpr unsigned long PREFETCH_SETTLE_MS = 1500;
  // Prefetch must not start unless there is room for its own live set AND a page
  // render. Measured: the build peaks at 26,652 bytes live, and rendering took
  // free heap from 154,492 at boot to 127,164 (27,328 bytes, which includes
  // font-cache warm-up that persists, so it over-states the transient need and
  // errs the safe way). 26,652 + 27,328 = 53,980, rounded up.
  static constexpr size_t PREFETCH_MIN_FREE_HEAP = 56 * 1024;
  // Largest-block gate kept at the reader's existing value: maxalloc never moved
  // during a build (90,100 bytes throughout), so the build needs no large
  // contiguous allocation.
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
