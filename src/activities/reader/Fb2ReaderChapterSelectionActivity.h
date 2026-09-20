#pragma once
#include <Fb2.h>

#include <memory>
#include <string>

#include "activities/UiListActivity.h"

// TOC-based chapter selection for FB2 books. Returns a ChapterResult whose
// spineIndex carries the selected FB2 section index.
class Fb2ReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Fb2> fb2;
  int currentSectionIndex = 0;

  // Windowed row buffers: only the rows around the viewport are materialized, so
  // the list costs the same for a 4-chapter book and one at FB2_MAX_CHAPTERS.
  // Materializing every row instead held a second std::string copy of every
  // chapter title plus a ListItem each. The window follows nav.top via
  // itemsWindowFirst (see fui::ListProps), the way the EPUB chapter list does.
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  freeink::ui::ListItem windowItems[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start);

  int listCount() const override { return fb2 ? fb2->getTocCount() : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Back cancels with a result and Confirm activates on RELEASE here; a `!fb2`
  // guard consumes the pass after Back so nothing else runs without a book.
  bool handleButtons() override;
  void drawChrome() override;

 public:
  explicit Fb2ReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::shared_ptr<Fb2>& fb2, int currentSectionIndex);
  void onEnter() override;
};
