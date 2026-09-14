#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {

// Longest string readString() accepts. Serialized strings are titles, authors,
// hrefs, anchors and ruby annotations -- legitimately well under 1KB. The length
// prefix is untrusted (it comes straight off the SD card), and with
// -fno-exceptions a failed std::string::resize() aborts the firmware, so a
// corrupt prefix must be rejected before any allocation happens. 8KB leaves
// ample headroom for real content while keeping the worst-case transient
// allocation trivial next to the device's ~380KB heap.
constexpr uint32_t MAX_STRING_LENGTH = 8 * 1024;

template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

// Read functions return false on a short read (truncated/corrupt source); the
// output value/string must not be trusted by the caller in that case.
template <typename T>
bool readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
  return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

template <typename T>
bool readPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline bool readString(std::istream& is, std::string& s, const uint32_t maxLen = MAX_STRING_LENGTH) {
  uint32_t len;
  if (!readPod(is, len) || len > maxLen) {
    s.clear();
    return false;
  }
  s.resize(len);
  if (len > 0) {
    is.read(&s[0], len);
    if (is.gcount() != static_cast<std::streamsize>(len)) {
      s.clear();
      return false;
    }
  }
  return true;
}

inline bool readString(HalFile& file, std::string& s, const uint32_t maxLen = MAX_STRING_LENGTH) {
  uint32_t len;
  if (!readPod(file, len)) {
    s.clear();
    return false;
  }
  // The file knows its own size, so a length that cannot physically fit in the
  // remaining bytes is rejected before the resize allocates anything.
  const size_t fileSize = file.size();
  const size_t pos = file.position();
  if (len > maxLen || pos > fileSize || len > fileSize - pos) {
    s.clear();
    return false;
  }
  s.resize(len);
  if (len > 0 && file.read(&s[0], len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}
}  // namespace serialization
