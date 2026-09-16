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

// Give lower-priority tasks a turn (vTaskDelay(1) on the device, no-op on host).
void yield();

// Free heap in bytes (ESP.getFreeHeap()); host returns a large constant unless a
// test installs a fake via setHeapForTest().
size_t freeHeap();

// Largest allocatable block in bytes (ESP.getMaxAllocHeap()).
size_t maxAllocHeap();

}  // namespace platform
