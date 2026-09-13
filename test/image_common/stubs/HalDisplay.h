#pragma once

// Host stub of lib/hal/HalDisplay.h: the converters only query the runtime
// display geometry to derive the default cover target size. Small values keep
// the default-target tests fast while still exercising the swap-to-portrait
// logic (targetWidth = getDisplayHeight(), targetHeight = getDisplayWidth()).

#include <Arduino.h>  // the real HalDisplay.h reaches Arduino.h via EInkDisplay

#include <cstdint>

class HalDisplay {
 public:
  uint16_t getDisplayWidth() const { return 24; }
  uint16_t getDisplayHeight() const { return 32; }
};

inline HalDisplay display;
