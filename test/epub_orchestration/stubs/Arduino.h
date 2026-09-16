#pragma once

#include <cstdint>

// Host stand-in for the Arduino globals CssParser.cpp reaches for. Epub.cpp
// itself goes through lib/Platform/PlatformSeam.h, so the heap gate a test
// drives lives in platform_host::setHeap().
struct EspHostStub {
  uint32_t getFreeHeap() const { return UINT32_MAX; }
  uint32_t getMaxAllocHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
