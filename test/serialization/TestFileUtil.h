#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Scratch-file helpers shared by the serialization suite. The stdio HalStorage
// stub uses paths verbatim, so tests hand it real host paths.
namespace testutil {

// Unique scratch path under gtest's temp dir, removed on scope exit.
class TempPath {
 public:
  explicit TempPath(const char* tag) {
    static int counter = 0;
    path_ = ::testing::TempDir() + "cp_serialization_" + tag + "_" + std::to_string(counter++) + ".bin";
    std::remove(path_.c_str());
  }
  ~TempPath() { std::remove(path_.c_str()); }
  TempPath(const TempPath&) = delete;
  TempPath& operator=(const TempPath&) = delete;

  const char* c_str() const { return path_.c_str(); }

 private:
  std::string path_;
};

inline void writeRaw(const char* path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline std::vector<uint8_t> readRaw(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// writePod copies raw object bytes, so a serialized uint32 is host-endian
// (little on both the ESP32-C3 target and every host these tests run on).
inline void appendU32(std::vector<uint8_t>& out, const uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

inline void appendChars(std::vector<uint8_t>& out, const std::string& text) {
  out.insert(out.end(), text.begin(), text.end());
}

// [uint32 length][payload] — the shape serialization::writeString() emits.
inline void appendLengthPrefixed(std::vector<uint8_t>& out, const std::string& text) {
  appendU32(out, static_cast<uint32_t>(text.size()));
  appendChars(out, text);
}

}  // namespace testutil
