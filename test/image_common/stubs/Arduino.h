#pragma once

// Host stub for the few Arduino/ESP32 core symbols the image converters use:
// ESP.getFreeHeap() (heap headroom gate) and millis() (yield pacing).

#include <freertos/task.h>  // Arduino.h brings FreeRTOS on ESP32 (vTaskDelay)

#include <cstdint>

struct EspHostStub {
  uint32_t getFreeHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;

inline uint32_t millis() { return 0; }
