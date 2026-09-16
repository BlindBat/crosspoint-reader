#pragma once

// Host stub of lib/hal/HalDisplay.h: a plain 800x480 1-bpp framebuffer plus
// call recorders for every panel operation GfxRenderer.cpp delegates, so the
// tests can assert refresh modes, baseline resyncs and strip writes without
// a panel driver.

#include <Arduino.h>

#include <cstdint>
#include <cstring>
#include <vector>

class HalDisplay {
 public:
  enum RefreshMode {
    FULL_REFRESH,
    HALF_REFRESH,
    FAST_REFRESH,
  };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  struct StripWrite {
    bool lsbPlane;
    uint16_t yStart;
    uint16_t numRows;
    uint8_t firstByte;
  };

  HalDisplay() : buffer_(BUFFER_SIZE, 0xFF) {}

  // --- geometry ---
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(buffer_.data()); }

  // --- framebuffer ---
  void clearScreen(uint8_t color = 0xFF) const { std::memset(getFrameBuffer(), color, BUFFER_SIZE); }
  void drawImage(const uint8_t*, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool = false) const {
    lastImageX = x;
    lastImageY = y;
    lastImageW = w;
    lastImageH = h;
    drawImageCalls++;
  }

  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut) {
    if (lent_) return nullptr;
    lent_ = true;
    if (sizeOut) *sizeOut = BUFFER_SIZE;
    return getFrameBuffer();
  }
  void returnFrameBufferStorage() {
    lent_ = false;
    clearScreen(0xFF);
  }
  bool isLent() const { return lent_; }

  // --- refresh ---
  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false) {
    displayCalls++;
    lastMode = mode;
    lastTurnOffScreen = turnOffScreen;
  }
  void displayBufferAsync(RefreshMode mode = FAST_REFRESH) {
    asyncCalls++;
    lastMode = mode;
  }
  void waitRefreshComplete() { waitCalls++; }
  bool supportsAsyncRefresh() const { return asyncSupported; }

  void setInverted(bool inverted) { inverted_ = inverted; }
  bool isInverted() const { return inverted_; }

  // --- grayscale ---
  void preconditionGrayscale() { preconditionCalls++; }
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    preconditionCalls++;
    lastPreconditionX = x;
    lastPreconditionY = y;
    lastPreconditionW = w;
    lastPreconditionH = h;
  }
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false) {
    grayscaleBaseCalls++;
    lastMode = fallback;
    lastTurnOffScreen = turnOffScreen;
  }
  void copyGrayscaleLsbBuffers(const uint8_t*) { lsbCopies++; }
  void copyGrayscaleMsbBuffers(const uint8_t*) { msbCopies++; }
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
    cleanupCalls++;
    lastCleanupBuffer = bwBuffer;
  }
  void displayGrayBuffer(bool turnOffScreen = false) {
    grayBufferCalls++;
    lastTurnOffScreen = turnOffScreen;
  }
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
    stripWrites.push_back({lsbPlane, yStart, numRows, rows ? rows[0] : uint8_t{0}});
  }
  bool supportsStripGrayscale() const { return stripSupported; }
  bool combinesGrayscaleBase() const { return combinesBase; }

  // --- test controls / recorders ---
  bool asyncSupported = false;
  bool stripSupported = false;
  bool combinesBase = false;
  int displayCalls = 0;
  int asyncCalls = 0;
  int waitCalls = 0;
  int preconditionCalls = 0;
  int grayscaleBaseCalls = 0;
  int lsbCopies = 0;
  int msbCopies = 0;
  int cleanupCalls = 0;
  int grayBufferCalls = 0;
  mutable int drawImageCalls = 0;
  mutable uint16_t lastImageX = 0, lastImageY = 0, lastImageW = 0, lastImageH = 0;
  uint16_t lastPreconditionX = 0, lastPreconditionY = 0, lastPreconditionW = 0, lastPreconditionH = 0;
  RefreshMode lastMode = FAST_REFRESH;
  bool lastTurnOffScreen = false;
  const uint8_t* lastCleanupBuffer = nullptr;
  std::vector<StripWrite> stripWrites;

 private:
  std::vector<uint8_t> buffer_;
  bool lent_ = false;
  bool inverted_ = false;
};
