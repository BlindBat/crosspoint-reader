#pragma once
#include <Fb2.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// TOC-based chapter selection for FB2 books. Returns a ChapterResult whose
// spineIndex carries the selected FB2 section index.
class Fb2ReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Fb2> fb2;
  int currentSectionIndex = 0;

  // Row buffer, built once in onEnter() (the TOC never changes for the
  // lifetime of this screen) and reused by buildScreen() on every repaint.
  std::vector<std::string> rowLabels;
  std::vector<freeink::ui::ListItem> rowItems;
  void buildRowItems();

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
