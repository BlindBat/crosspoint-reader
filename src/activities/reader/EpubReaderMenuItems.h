#pragma once

#include <I18n.h>

#include <cstddef>
#include <vector>

// Row model of the reader menu, shared by the EPUB/FB2 readers and the menu activity.
namespace ReaderMenu {

enum class MenuAction {
  SELECT_CHAPTER,
  FOOTNOTES,
  TEXT_SETTINGS,
  NIGHT_MODE,
  FRONTLIGHT,
  GO_TO_PERCENT,
  AUTO_PAGE_TURN,
  ROTATE_SCREEN,
  BOOKMARKS,
  TOGGLE_BOOKMARK,
  SCREENSHOT,
  DISPLAY_QR,
  GO_HOME,
  SYNC,
  DELETE_CACHE,
  DICTIONARY
};

struct MenuItem {
  MenuAction action;
  StrId labelId;
};

// Every optional row present: the upper bound for fixed-capacity row storage.
constexpr size_t MAX_MENU_ITEMS = 16;

// Fixed menu order. Footnotes, Bookmarks and Frontlight rows appear only when
// the book (or board) offers them; every other row is always present.
inline void buildMenuItems(std::vector<MenuItem>& items, const bool hasFootnotes, const bool hasBookmarks,
                           const bool hasFrontlight) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (hasFrontlight) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
}

}  // namespace ReaderMenu
