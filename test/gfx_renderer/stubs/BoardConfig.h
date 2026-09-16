#pragma once

// Host stand-in for the SDK BoardConfig: the renderer reads only the active
// profile's bezel insets (getOrientedViewableTRBL). Tests may overwrite
// BoardConfig::ACTIVE.viewableInsets with asymmetric values.

#include <cstdint>

namespace BoardConfig {

struct ViewableInsets {
  uint8_t top = 9;
  uint8_t right = 3;
  uint8_t bottom = 3;
  uint8_t left = 3;
};

struct BoardProfile {
  ViewableInsets viewableInsets{};
};

inline BoardProfile ACTIVE;

}  // namespace BoardConfig
