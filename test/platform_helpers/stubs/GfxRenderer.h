#pragma once

#include <HalDisplay.h>

#include <cstdint>

// Just the renderer surface ScreenshotUtil touches, over a test-owned framebuffer.
class GfxRenderer {
 public:
  uint8_t* framebuffer = nullptr;
  uint16_t panelWidth = 0;
  uint16_t panelHeight = 0;
  bool storeBwBufferResult = false;
  mutable int displayCalls = 0;

  uint8_t* getFrameBuffer() const { return framebuffer; }
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  int getScreenWidth() const { return panelHeight; }
  int getScreenHeight() const { return panelWidth; }
  bool storeBwBuffer() { return storeBwBufferResult; }
  void restoreBwBuffer() {}
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const { *t = *r = *b = *l = 0; }
  void drawRect(int, int, int, int, int, bool) const {}
  void displayBuffer(HalDisplay::RefreshMode = HalDisplay::FAST_REFRESH) const { displayCalls++; }
};
