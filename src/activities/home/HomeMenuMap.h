#pragma once

#include "activities/ActivityManager.h"

// Maps the fixed home menu rows (Browse Files, Recent Books, [OPDS Browser], File
// Transfer, Settings) to and from HomeMenuItem. OPDS Browser occupies a row only
// while a server is configured; indices are relative to the first fixed row, not
// to any Continue Reading entries drawn above them.
namespace HomeMenuMap {

inline int menuItemToIndex(const HomeMenuItem item, const bool hasOpdsUrl) {
  int i = 0;
  if (item == HomeMenuItem::FILE_BROWSER) return i;
  ++i;
  if (item == HomeMenuItem::RECENTS) return i;
  ++i;
  if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
  if (hasOpdsUrl) ++i;
  if (item == HomeMenuItem::FILE_TRANSFER) return i;
  ++i;
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
  return 0;
}

inline HomeMenuItem indexToMenuItem(const int idx, const bool hasOpdsUrl) {
  int i = 0;
  if (idx == i++) return HomeMenuItem::FILE_BROWSER;
  if (idx == i++) return HomeMenuItem::RECENTS;
  if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
  if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
  if (idx == i) return HomeMenuItem::SETTINGS_MENU;
  return HomeMenuItem::NONE;
}

}  // namespace HomeMenuMap
