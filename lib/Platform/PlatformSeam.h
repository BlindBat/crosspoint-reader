#pragma once

#include <cstddef>
#include <cstdint>

// Platform seam for pure-logic libraries (Constitution III).
//
// Reader-core code that needs the wall clock, a scheduler yield or heap
// telemetry calls these instead of including <Arduino.h> / FreeRTOS headers,
// so the same translation unit compiles on the device (lib/Platform/PlatformSeam.cpp)
// and in the host test program (test/support/PlatformHost.cpp).
namespace platform {

// Milliseconds since boot (Arduino millis()).
uint32_t millis();

// Microseconds since boot (Arduino micros()); wraps every ~71 minutes.
uint32_t micros();

// Give lower-priority tasks a turn (vTaskDelay(1) on the device, no-op on host).
void yield();

// Free heap in bytes (ESP.getFreeHeap()); host returns a large constant unless a
// test installs a fake via setHeapForTest().
size_t freeHeap();

// Largest allocatable block in bytes (ESP.getMaxAllocHeap()).
size_t maxAllocHeap();

// Write-only byte sink for pure-logic parsers that are fed a stream. Stands in
// for Arduino's Print so a parser header does not drag <Print.h> into the host
// test program; the device-side adapter (e.g. OpdsParserStream) still derives
// from Arduino Stream and forwards into one of these.
class ByteSink {
 public:
  virtual ~ByteSink() = default;
  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual void flush() = 0;
};

}  // namespace platform
