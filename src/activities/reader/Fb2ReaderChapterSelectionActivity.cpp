#include "Fb2ReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

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

  buildRowItems();

  // Open on the current chapter, which may sit past the first row; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  const int tocIndex = fb2->getTocIndexForSectionIndex(currentSectionIndex);
  nav.selected = tocIndex >= 0 ? tocIndex : 0;
}

// Derives rowItems from the fb2's TOC. Called once from onEnter() since the
// TOC is static for this screen's lifetime.
void Fb2ReaderChapterSelectionActivity::buildRowItems() {
  const int tocCount = fb2->getTocCount();
  rowLabels.clear();
  rowItems.clear();
  rowLabels.reserve(tocCount);
  rowItems.reserve(tocCount);
  for (int i = 0; i < tocCount; i++) {
    const auto& tocEntry = fb2->getTocEntry(i);
    rowLabels.push_back(tocEntry.title.empty() ? tr(STR_UNNAMED) : tocEntry.title);
    fui::ListItem item;
    item.label = rowLabels.back().c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
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
  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  // rowItems is built once in onEnter() (see buildRowItems()) and reused
  // here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
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
