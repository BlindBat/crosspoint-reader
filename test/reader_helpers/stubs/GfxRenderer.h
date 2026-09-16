#pragma once

// Recording GfxRenderer stand-in for ReaderUtils.h and QrUtils.cpp: screen
// geometry, orientation and the grayscale capabilities are test-set; every
// display/fill/grayscale call is appended to a log so ordering can be asserted.

#include <HalDisplay.h>

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer {
 public:
  enum class Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  struct RectCall {
    int x;
    int y;
    int width;
    int height;
    bool state;
  };

  int width = 480;
  int height = 800;
  Orientation orientation = Orientation::Portrait;
  bool combinesBase = false;
  // Scripted failure of the temporary grayscale buffer allocation.
  bool bwStoreSucceeds = true;
  RenderMode renderMode = BW;

  mutable std::vector<HalDisplay::RefreshMode> displayCalls;
  mutable std::vector<HalDisplay::RefreshMode> asyncCalls;
  mutable std::vector<HalDisplay::RefreshMode> baseCalls;
  mutable std::vector<RectCall> rects;
  // Ordered trace of the grayscale pass: store/clear/mode/copy/display/restore.
  mutable std::vector<std::string> trace;

  void setOrientation(const Orientation o) { orientation = o; }
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }

  void displayBuffer(const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH) const {
    displayCalls.push_back(mode);
  }
  void displayBufferAsync(const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH) const {
    asyncCalls.push_back(mode);
  }
  bool combinesGrayscaleBase() const { return combinesBase; }
  void displayGrayscaleBase(const HalDisplay::RefreshMode fallback = HalDisplay::HALF_REFRESH) const {
    baseCalls.push_back(fallback);
  }

  void fillRect(const int x, const int y, const int w, const int h, const bool state = true) const {
    rects.push_back({x, y, w, h, state});
  }

  bool storeBwBuffer() {
    trace.push_back(bwStoreSucceeds ? "store" : "store-fail");
    return bwStoreSucceeds;
  }
  void restoreBwBuffer(const bool resyncPanelBaseline = true) {
    trace.push_back(resyncPanelBaseline ? "restore" : "restore-noresync");
  }
  void cleanupGrayscaleWithFrameBuffer() const { trace.push_back("cleanup"); }
  void clearScreen(const uint8_t color = 0xFF) const { trace.push_back(color == 0x00 ? "clear:00" : "clear:other"); }
  void setRenderMode(const RenderMode mode) {
    renderMode = mode;
    trace.push_back(mode == BW ? "mode:bw" : (mode == GRAYSCALE_LSB ? "mode:lsb" : "mode:msb"));
  }
  void copyGrayscaleLsbBuffers() const { trace.push_back("copy:lsb"); }
  void copyGrayscaleMsbBuffers() const { trace.push_back("copy:msb"); }
  void displayGrayBuffer() const { trace.push_back("display:gray"); }
};
