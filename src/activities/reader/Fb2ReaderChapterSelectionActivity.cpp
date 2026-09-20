#include "Fb2ReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

Fb2ReaderChapterSelectionActivity::Fb2ReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Fb2>& fb2,
                                                                     const int currentSectionIndex)
    : UiListActivity("Fb2ReaderChapterSelection", renderer, mappedInput),
      fb2(fb2),
      currentSectionIndex(currentSectionIndex) {}

void Fb2ReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!fb2) {
    return;
  }

  // Open on the current chapter, which may sit past the first row; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  const int tocIndex = fb2->getTocIndexForSectionIndex(currentSectionIndex);
  nav.selected = tocIndex >= 0 ? tocIndex : 0;
}

// Materializes the ListItem/label window starting at `start` (clamped). Runs only
// when the viewport leaves the current window, so repaints inside it are free.
void Fb2ReaderChapterSelectionActivity::refreshTocWindow(const int start) {
  const int total = listCount();
  int clamped = start;
  if (clamped > total - TOC_WINDOW) clamped = total - TOC_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = total - clamped < TOC_WINDOW ? total - clamped : TOC_WINDOW;
  for (int i = 0; i < windowCount; i++) {
    const auto& tocEntry = fb2->getTocEntry(clamped + i);
    // Indent by nesting depth so a story reads as a story inside its part, the
    // same convention the EPUB chapter list uses. Capped at three steps so a
    // deeply nested title is not pushed off the row.
    const int indentSteps = tocEntry.level > 3 ? 3 : tocEntry.level;
    std::string label(static_cast<size_t>(indentSteps) * 2, ' ');
    label += tocEntry.title.empty() ? tr(STR_UNNAMED) : tocEntry.title;
    windowLabels[i] = std::move(label);
    fui::ListItem item;
    item.label = windowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(clamped + i);
    windowItems[i] = item;
  }
  windowStart = clamped;
}

void Fb2ReaderChapterSelectionActivity::activateIndex(const int index) {
  // The activated row leaves this screen (finish); a lingering flash would
  // gray an unrelated element on the next render.
  app.clearTapFlash();
  if (index >= 0 && index < fb2->getTocCount()) {
    nav.selected = index;
    setResult(ChapterResult{fb2->getSectionIndexForTocIndex(index), ""});
    finish();
  }
}

bool Fb2ReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!fb2) {
    return true;  // no book: nothing else to route this pass
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void Fb2ReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!fb2) {
    return;
  }
  if (listCount() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  // Materialize the row window for the final viewport (syncListViewport just
  // applied follow/clamping to nav.top) and hand list() the window with its
  // absolute base index.
  refreshTocWindow(nav.top);
  props.items = windowItems;
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  screen.list(props);
}

void Fb2ReaderChapterSelectionActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Centered title in the header band the content margin reserves.
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD);
  const int titleX = safe.x + (safe.width - titleWidth) / 2;
  const int titleY = safe.y + metrics.topPadding + (metrics.headerHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);
}
