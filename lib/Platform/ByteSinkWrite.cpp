#include "PlatformSeam.h"

// Kept out of PlatformSeam.cpp: that translation unit is device-only (it
// includes <Arduino.h>), while this retry loop is pure logic over the seam and
// is compiled into the host test program as well.
namespace platform {

size_t writeAll(ByteSink& sink, const uint8_t* data, const size_t length, const uint32_t timeoutMs) {
  const uint32_t start = millis();
  size_t written = 0;
  while (written < length) {
    written += sink.write(data + written, length - written);
    if (written >= length || millis() - start >= timeoutMs) {
      break;
    }
    yield();
  }
  return written;
}

}  // namespace platform
