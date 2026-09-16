#pragma once

#include <util/ScreenshotInfo.h>

// ScreenshotUtil asks the activity manager which reader is on screen.
class ActivityManager {
 public:
  ScreenshotInfo info;
  ScreenshotInfo getScreenshotInfo() const { return info; }
};

inline ActivityManager activityManager;
