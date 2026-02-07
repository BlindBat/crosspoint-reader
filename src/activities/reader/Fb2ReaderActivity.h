#pragma once

#include <Fb2.h>
#include <Fb2/Fb2Section.h>

#include <memory>
#include <string>

#include "EpubReaderMenuActivity.h"
#include "ReaderActivity.h"

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
  bool pendingPercentJump = false;
  float pendingSectionProgress = 0.0f;
  uint8_t appliedOrientation = 0;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;

  void loadProgress();
  void saveProgress(int sectionIndex, int currentPage, int pageCount) const;
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

 public:
  explicit Fb2ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("Fb2Reader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~Fb2ReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
