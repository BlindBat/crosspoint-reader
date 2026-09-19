#pragma once

// Host stub for the Arduino/ESP32 core symbols reached by the renderer and the
// font libraries it links: ESP.restart() (the FrameBufferLoan backstop). Heap
// telemetry and the clocks go through lib/Platform/PlatformSeam.h instead —
// drive them with platform_host::setHeap()/setClock().

#include <cstdint>

struct EspHostStub {
  int restartCount = 0;
  void restart() { restartCount++; }
};
inline EspHostStub ESP;
