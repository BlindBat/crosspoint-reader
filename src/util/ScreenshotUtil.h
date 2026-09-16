#pragma once
#include <GfxRenderer.h>

#include <cstddef>

#include "ScreenshotInfo.h"

class ScreenshotUtil {
 public:
  static void takeScreenshot(GfxRenderer& renderer);
  static bool saveFramebufferAsBmp(const char* filename, const uint8_t* framebuffer, int width, int height);
  // Composes "/screenshots/<title>/<title>_ch<n>_p<n>_<pct>pct_<ms>.bmp", or a
  // plain "/screenshots/screenshot-<ms>.bmp" when no reader is on screen.
  static void buildFilename(const ScreenshotInfo& info, char* buf, size_t bufSize);
};
