#pragma once

#include <cstddef>
#include <cstdint>

// Host test stub mirroring the firmware's Print interface. ZipFile's
// readFileToStream() writes decompressed bytes to a Print sink; the suite
// supplies a collector that captures them for assertions.
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* data, size_t length) {
    size_t written = 0;
    for (; written < length; ++written) {
      if (write(data[written]) == 0) break;
    }
    return written;
  }
  virtual void flush() {}
};
