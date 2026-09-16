#pragma once

// Host stand-in: ReaderUtils reads the touch capability and the last touch hold time.
class HalGPIO {
 public:
  bool touch = false;
  unsigned long touchHeldMs = 0;
  bool hasTouch() const { return touch; }
  unsigned long lastTouchHeldMs() const { return touchHeldMs; }
};

inline HalGPIO gpio;
