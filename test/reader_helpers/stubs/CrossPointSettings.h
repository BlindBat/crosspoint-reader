#pragma once

// Host stand-in for the settings fields ReaderUtils.h reads. Enum values mirror
// src/CrossPointSettings.h exactly; getRefreshFrequency() returns a test-set
// page count instead of mapping the REFRESH_* enum.

#include <cstdint>

class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }

  enum ORIENTATION { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
  enum SHORT_PWRBTN { IGNORE = 0, SLEEP = 1, PAGE_TURN = 2, FORCE_REFRESH = 3, FOOTNOTES = 4, PWR_CONFIRM = 5 };
  enum LONG_PRESS_BUTTON_BEHAVIOR { OFF = 0, CHAPTER_SKIP = 1, ORIENTATION_CHANGE = 2 };
  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_NVERTED = 2 };
  enum TOUCH_READER_CONTROLS {
    TOUCH_READER_OFF = 0,
    TOUCH_READER_ON = 1,
    TOUCH_READER_SWIPE = 2,
    TOUCH_READER_INVERTED_TAP = 3
  };
  enum SHOW_READER_MENU { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2 };

  uint8_t shortPwrBtn = IGNORE;
  uint8_t orientation = PORTRAIT;
  uint8_t longPressButtonBehavior = OFF;
  uint8_t backShortToFileBrowser = 0;
  uint8_t tiltPageTurn = TILT_OFF;
  uint8_t touchReaderControls = TOUCH_READER_SWIPE;
  uint8_t showReaderMenu = READER_MENU_TAP;

  int refreshFrequencyPages = 15;
  int getRefreshFrequency() const { return refreshFrequencyPages; }

  void reset() { *this = CrossPointSettings(); }
};

#define SETTINGS CrossPointSettings::getInstance()
