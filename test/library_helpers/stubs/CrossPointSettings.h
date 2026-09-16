#pragma once

#include <cstdint>

// Host stand-in exposing only the setting NextBookFinder reads.

class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }

  uint8_t showHiddenFiles = 0;
};

#define SETTINGS CrossPointSettings::getInstance()
