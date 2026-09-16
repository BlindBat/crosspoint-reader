#pragma once

// Host stub for the Arduino/ESP32 core symbols reached by the renderer and the
// font libraries it links: heap telemetry (SdCardFont prewarm budgets), the
// clocks (stats accounting only) and ESP.restart() (FrameBufferLoan backstop).

#include <cstdint>

struct EspHostStub {
  uint32_t freeHeap = 512u * 1024u * 1024u;
  uint32_t maxAllocHeap = 256u * 1024u * 1024u;
  int restartCount = 0;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAllocHeap; }
  void restart() { restartCount++; }
};
inline EspHostStub ESP;

inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
