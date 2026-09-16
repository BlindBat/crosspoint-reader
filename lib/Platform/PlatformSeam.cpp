#include "PlatformSeam.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace platform {

uint32_t millis() { return ::millis(); }

void yield() { vTaskDelay(1); }

size_t freeHeap() { return ESP.getFreeHeap(); }

size_t maxAllocHeap() { return ESP.getMaxAllocHeap(); }

}  // namespace platform
