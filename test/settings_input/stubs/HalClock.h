#pragma once

// Host stand-in: SettingsList.h includes HalClock.h but the settings list
// itself never touches the clock, so an empty shell is enough.

class HalClock {
 public:
  bool isAvailable() const { return false; }
};

inline HalClock halClock;
