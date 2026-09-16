#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Host stand-in for Arduino's Print: the byte sink HalFile and the serial
// stubs derive from.
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
  size_t print(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)) : 0; }
  virtual void flush() {}
};
