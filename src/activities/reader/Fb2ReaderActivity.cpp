#include "Fb2ReaderActivity.h"

#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <memory>

#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "Fb2ReaderChapterSelectionActivity.h"
#include "Fb2ReaderMath.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "activities/boot_sleep/SleepImageUtils.h"
#include "activities/settings/TextSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace {
size_t cumulativeSectionSize(const void* ctx, const int index) {
  return static_cast<const Fb2*>(ctx)->getCumulativeSectionSize(index);
}
}  // namespace

bool Fb2ReaderActivity::loadBook() {
  auto loadedFb2 = makeUniqueNoThrow<Fb2>(bookPath, "/.crosspoint");
  if (!loadedFb2) {
    LOG_ERR("FBR", "OOM: Fb2 object");
    return false;
  }
  if (!loadedFb2->load()) {
    LOG_ERR("FBR", "Failed to load FB2");
    return false;
  }
  fb2 = std::move(loadedFb2);
  fb2->setupCacheDir();
  loadProgress();
  // generateCoverBmp() is a no-op when cover.bmp already exists, which it does for
  // any book the home or sleep screen has drawn.
  if (fb2->generateCoverBmp()) {
    coverBmpPath = fb2->getCoverBmpPath();
  }
  // Only a book opened at its very beginning opens on the cover; a resumed
  // position is restored exactly as it was saved.
  onCoverPage = !coverBmpPath.empty() && currentSectionIndex == 0 && nextPageNumber == 0;
  return true;
}

// Draws the cover over the loaded section. Returns false when the bitmap cannot be
// used, so the caller falls through to the normal page render.
bool Fb2ReaderActivity::renderCoverPage() {
  HalFile file;
  if (!Storage.openFileForRead("FBR", coverBmpPath, file)) {
    LOG_ERR("FBR", "Cover unreadable: %s", coverBmpPath.c_str());
    return false;
  }
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("FBR", "Cover header invalid: %s", coverBmpPath.c_str());
    return false;
  }
  const auto placement = sleepimage::calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(),
                                                              renderer.getScreenWidth(), renderer.getScreenHeight(),
                                                              /*crop=*/false);
  renderer.clearScreen();
  // ponytail: black and white only. The sleep screen's three-pass grayscale
  // pipeline is the upgrade path if a dithered cover reads poorly on device.
  renderer.drawBitmap(bitmap, placement.x, placement.y, renderer.getScreenWidth(), renderer.getScreenHeight(),
                      placement.cropX, placement.cropY);
  renderer.displayBuffer();
  return true;
}

void Fb2ReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void Fb2ReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("FBR", fb2->getCachePath() + "/progress.bin", f)) {
    uint8_t data[fb2_reader::PROGRESS_SIZE];
    const auto progress = fb2_reader::decodeProgress(data, f.read(data, sizeof(data)));
    if (progress.valid) {
      // A payload without the marker stored a top-level ordinal, not a chapter
      // index: resolve it to that part's own chapter and restart at its page 0.
      currentSectionIndex =
          progress.legacyOrdinal ? fb2->firstChapterOfTopLevel(progress.sectionIndex) : progress.sectionIndex;
      nextPageNumber = progress.page;
      cachedSectionIndex = currentSectionIndex;
    }
    if (progress.hasPageCount) {
      cachedSectionTotalPageCount = progress.pageCount;
      // progress.bin already holds this position, so an unchanged first render
      // must not write it straight back. A legacy payload is deliberately left
      // unseeded so the first save rewrites it in the current form.
      progressGuard.markSaved(progress.sectionIndex, progress.page, progress.pageCount);
    }
    LOG_DBG("FBR", "Loaded progress: section %d page %d", currentSectionIndex, nextPageNumber);
  }
}

void Fb2ReaderActivity::saveProgress(const int sectionIndex, const int currentPage, const int pageCount) {
  uint8_t data[fb2_reader::PROGRESS_SIZE];
  fb2_reader::encodeProgress(data, sectionIndex, currentPage, pageCount);
  if (!progressGuard.save(fb2->getCachePath(), sectionIndex, currentPage, pageCount, data, sizeof(data))) {
    LOG_ERR("FBR", "Failed to save progress: section %d page %d", sectionIndex, currentPage);
  }
}

float Fb2ReaderActivity::bookProgressPercent() const {
  if (!fb2 || fb2->getBookSize() == 0 || !section || section->pageCount == 0) {
    return 0.0f;
  }
  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
  return fb2->calculateProgress(currentSectionIndex, chapterProgress) * 100.0f;
}

bool Fb2ReaderActivity::handleFormatInput() {
  if (!fb2) {
    return false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    openReaderMenu();
    return true;
  }
  return false;
}

void Fb2ReaderActivity::openReaderMenu() {
  const int currentPage = section ? section->currentPage + 1 : 0;
  const int totalPages = section ? section->pageCount : 0;
  const int progressPercent = fb2_reader::roundedPercent(bookProgressPercent());
  startActivityForResult(
      makeUniqueNoThrow<EpubReaderMenuActivity>(renderer, mappedInput, fb2->getTitle(), currentPage, totalPages,
                                                progressPercent, SETTINGS.orientation, /*hasFootnotes=*/false,
                                                /*hasBookmarks=*/false),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        if (SETTINGS.orientation != menu.orientation || appliedOrientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
        }
      });
}

void Fb2ReaderActivity::onReaderMenuConfirm(const EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      onCoverPage = false;
      const int sectionIdx = currentSectionIndex;
      // Release the section while the chapter list is up; cancel restores the
      // position through the cached page/page-count rebuild below.
      {
        RenderLock lock;
        if (section) {
          cachedSectionIndex = currentSectionIndex;
          cachedSectionTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResult(
          makeUniqueNoThrow<Fb2ReaderChapterSelectionActivity>(renderer, mappedInput, fb2, sectionIdx),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            currentSectionIndex = chapterResult.spineIndex;
            nextPageNumber = 0;
            cachedSectionTotalPageCount = 0;
            section.reset();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResult(makeUniqueNoThrow<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                     TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               // The section cache validates against the render spec, so
                               // renderBook() re-paginates with the new settings.
                               {
                                 RenderLock lock;
                                 if (section) {
                                   cachedSectionIndex = currentSectionIndex;
                                   cachedSectionTotalPageCount = section->pageCount;
                                   nextPageNumber = section->currentPage;
                                 }
                                 section.reset();
                               }
                               openReaderMenu();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const int initialPercent = fb2_reader::roundedPercent(bookProgressPercent());
      startActivityForResult(
          makeUniqueNoThrow<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (fb2 && section) {
          const int backupSection = currentSectionIndex;
          const int backupPage = section->currentPage;
          const int backupPageCount = section->pageCount;
          section.reset();
          fb2->clearCache();
          fb2->setupCacheDir();
          // clearCache() removed progress.bin, so this save must not be skipped.
          progressGuard.forget();
          saveProgress(backupSection, backupPage, backupPageCount);
        }
      }
      onGoHome();
      return;
    }
    default:
      // Remaining menu actions are EPUB-only (bookmarks, footnotes, sync, ...).
      break;
  }
}

void Fb2ReaderActivity::applyOrientation(const uint8_t orientation) {
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  RenderLock lock(*this);
  if (section) {
    cachedSectionIndex = currentSectionIndex;
    cachedSectionTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
}

void Fb2ReaderActivity::jumpToPercent(const int percent) {
  onCoverPage = false;
  if (!fb2) return;

  const fb2_reader::SectionSizes sizes{fb2.get(), &cumulativeSectionSize, fb2->getSectionCount(), fb2->getBookSize()};
  const auto target = fb2_reader::percentToSection(percent, sizes);
  if (!target.valid) return;
  pendingSectionProgress = target.sectionProgress;

  RenderLock lock;
  currentSectionIndex = target.sectionIndex;
  nextPageNumber = 0;
  pendingPercentJump = true;
  section.reset();
}

bool Fb2ReaderActivity::pageTurn(const bool isForward) {
  if (!section || !fb2) return false;
  if (onCoverPage) {
    // Forward leaves the cover for chapter 0 page 0; nothing precedes the cover.
    if (!isForward) return false;
    onCoverPage = false;
    return true;
  }
  if (isForward) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
      return true;
    } else if (currentSectionIndex + 1 < fb2->getSectionCount()) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSectionIndex++;
      section.reset();
      return true;
    } else {
      // Walk off the last page into the end-of-book screen; the section stays
      // loaded so backing out restores the last page.
      currentSectionIndex = fb2->getSectionCount();
      return true;
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
      return true;
    } else if (currentSectionIndex > 0) {
      RenderLock lock;
      nextPageNumber = UINT16_MAX;
      currentSectionIndex--;
      section.reset();
      return true;
    } else if (!coverBmpPath.empty()) {
      // Back from the book's first page of text returns to the cover.
      onCoverPage = true;
      return true;
    }
  }
  return false;
}

bool Fb2ReaderActivity::skipPages(const int amount) {
  // Long-press chapter skip: forward goes to the next section, backward to the
  // start of this section (or the previous one when already there).
  if (!section || !fb2) return false;
  onCoverPage = false;
  if (amount > 0) {
    RenderLock lock;
    nextPageNumber = 0;
    currentSectionIndex++;
    section.reset();
    return true;
  } else {
    if (section->currentPage > 0) {
      section->currentPage = 0;
      return true;
    } else if (currentSectionIndex > 0) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSectionIndex--;
      section.reset();
      return true;
    }
  }
  return false;
}

bool Fb2ReaderActivity::isAtEndOfBook() const { return fb2 && currentSectionIndex >= fb2->getSectionCount(); }

void Fb2ReaderActivity::onReturnFromEndOfBook() {
  if (!fb2 || fb2->getSectionCount() == 0) {
    return;
  }
  currentSectionIndex = fb2->getSectionCount() - 1;
  if (!section) {
    nextPageNumber = UINT16_MAX;
  }
}

// BuildPopupFn trampoline: a plain function pointer plus this activity as context,
// so the section builder carries no std::function closure.
void Fb2ReaderActivity::showIndexingPopup(void* ctx) {
  auto* self = static_cast<Fb2ReaderActivity*>(ctx);
  GUI.drawPopup(self->renderer, tr(STR_INDEXING));
  self->pagesUntilFullRefresh = 1;
}

void Fb2ReaderActivity::renderBook() {
  if (!fb2) return;

  if (currentSectionIndex < 0) currentSectionIndex = 0;
  if (currentSectionIndex > fb2->getSectionCount()) currentSectionIndex = fb2->getSectionCount();
  if (currentSectionIndex == fb2->getSectionCount()) {
    // End of book is rendered by the ReaderActivity base.
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;
  orientedMarginBottom +=
      std::max(SETTINGS.screenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    LOG_DBG("FBR", "Loading section %d", currentSectionIndex);
    section = makeUniqueNoThrow<Fb2Section>(fb2, currentSectionIndex, renderer);
    if (!section) {
      LOG_ERR("FBR", "OOM: section");
      return;
    }

    if (!section->loadSectionFile(renderSpec)) {
      LOG_DBG("FBR", "Cache not found, building...");
      if (!section->createSectionFile(renderSpec, BuildPopupFn{&showIndexingPopup, this})) {
        LOG_ERR("FBR", "Failed to build section");
        section.reset();
        renderer.clearScreen();
        GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
        return;
      }
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount > 0 ? section->pageCount - 1 : 0;
    } else {
      section->currentPage = nextPageNumber;
    }

    if (cachedSectionTotalPageCount > 0) {
      // Re-paginated section: rescale the saved page into the new page count.
      if (currentSectionIndex == cachedSectionIndex) {
        section->currentPage =
            fb2_reader::rescalePage(section->currentPage, cachedSectionTotalPageCount, section->pageCount);
      }
      cachedSectionTotalPageCount = 0;
    }

    if (pendingPercentJump && section->pageCount > 0) {
      section->currentPage = fb2_reader::percentJumpPage(pendingSectionProgress, section->pageCount);
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0) section->currentPage = 0;
  if (section->currentPage >= section->pageCount) section->currentPage = section->pageCount - 1;

  // The section is loaded first so the menu and the progress percent read the real
  // page count while the cover is up. A cover that cannot be drawn is not an error:
  // clear the flag and render the first page of text instead.
  if (onCoverPage) {
    if (renderCoverPage()) {
      saveProgress(currentSectionIndex, section->currentPage, section->pageCount);
      return;
    }
    onCoverPage = false;
    coverBmpPath.clear();
  }

  auto page = section->loadPage(section->currentPage);
  if (!page) {
    LOG_ERR("FBR", "Failed to load page %d, rebuilding section", section->currentPage);
    section->clearCache();
    section.reset();
    if (pageLoadRetryCount < MAX_PAGE_LOAD_RETRIES) {
      pageLoadRetryCount++;
      renderBook();
    }
    return;
  }
  pageLoadRetryCount = 0;

  renderContents(std::move(page), orientedMarginTop, orientedMarginLeft);
  saveProgress(currentSectionIndex, section->currentPage, section->pageCount);

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void Fb2ReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                       const int orientedMarginLeft) {
  const int fontId = SETTINGS.getReaderFontId();

  // Font prewarm: scan pass accumulates glyphs, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer,
                                   [&]() { page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void Fb2ReaderActivity::renderStatusBar() const {
  const int currentPage = section ? section->currentPage + 1 : 1;
  const int totalPages = section ? section->pageCount : 1;
  const float chapterProg = totalPages > 0 ? static_cast<float>(currentPage) / static_cast<float>(totalPages) : 0;
  const float bookProgress = fb2 ? fb2->calculateProgress(currentSectionIndex, chapterProg) * 100 : 0;

  // Borrowed text only: getTocEntry and getTitle both hand back references the
  // book owns, so the status bar copies nothing per render.
  const char* title = "";
  const auto sb = SETTINGS.statusBarSpec();
  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (fb2) {
      const int tocIndex = fb2->getTocIndexForSectionIndex(currentSectionIndex);
      if (tocIndex != -1) {
        const Fb2::SectionInfo& tocEntry = fb2->getTocEntry(tocIndex);
        if (!tocEntry.title.empty()) title = tocEntry.title.c_str();
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    if (fb2) title = fb2->getTitle().c_str();
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, totalPages, title);
}

ScreenshotInfo Fb2ReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Fb2;
  if (fb2) {
    snprintf(info.title, sizeof(info.title), "%s", fb2->getTitle().c_str());
  }
  info.currentPage = section ? section->currentPage + 1 : 0;
  info.totalPages = section ? section->pageCount : 0;
  info.progressPercent = fb2_reader::roundedPercent(bookProgressPercent());
  return info;
}
