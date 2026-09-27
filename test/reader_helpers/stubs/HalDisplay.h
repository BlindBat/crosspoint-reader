#pragma once

#include <cstdint>

// Host stand-in: the refresh-mode enum ReaderUtils selects from, plus the
// grayscale capability types its base-display helper inspects.
class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  // Mirrors freeink-sdk GrayscaleCapabilities.h (the real HalDisplay aliases these).
  enum class GrayscaleMode : uint8_t { Overlay, Absolute, Direct };
  enum class GrayscaleEncoding : uint8_t { Unsupported, OverlayMasks, AbsolutePlanes };
  enum class GrayscaleBase : uint8_t { Separate, Combined };
  struct GrayscaleCapabilities {
    GrayscaleEncoding encoding = GrayscaleEncoding::Unsupported;
    GrayscaleBase base = GrayscaleBase::Separate;
    bool stripUploads = false;
    bool asyncBase = false;
    bool stagingWhileBusy = false;
    constexpr bool supported() const { return encoding != GrayscaleEncoding::Unsupported; }
  };
};
