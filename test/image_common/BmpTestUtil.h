#pragma once

// Shared helpers for the image decode host suites: a Print sink that captures
// converter output, a strict little parser for the BMP streams the converters
// emit (BITMAPINFOHEADER, top-down, 1/2/8-bit paletted), an FNV-1a checksum
// for golden pinning, and file fixture plumbing.

#include <Print.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace imgtest {

// Print sink capturing every byte the converter writes.
class MemoryPrint : public Print {
 public:
  size_t write(uint8_t b) override {
    bytes.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* data, size_t length) override {
    bytes.insert(bytes.end(), data, data + length);
    return length;
  }
  std::vector<uint8_t> bytes;
};

struct ParsedBmp {
  uint32_t fileSizeField = 0;
  uint32_t dataOffset = 0;
  int32_t width = 0;
  int32_t heightRaw = 0;  // negative = top-down
  uint16_t bitCount = 0;
  uint32_t compression = 0;
  uint32_t imageSizeField = 0;
  uint32_t colorsUsed = 0;
  size_t bytesPerRow = 0;
  std::vector<uint8_t> pixelData;  // rows in stored (top-down) order, incl. padding

  int absHeight() const { return heightRaw < 0 ? -heightRaw : heightRaw; }

  const uint8_t* row(int y) const { return pixelData.data() + static_cast<size_t>(y) * bytesPerRow; }

  // Palette index of pixel (x, y) for 1/2/8-bit BMPs.
  int pixel(int x, int y) const {
    const uint8_t* r = row(y);
    switch (bitCount) {
      case 1:
        return (r[x / 8] >> (7 - (x % 8))) & 0x1;
      case 2:
        return (r[(x * 2) / 8] >> (6 - ((x * 2) % 8))) & 0x3;
      case 8:
        return r[x];
      default:
        return -1;
    }
  }
};

inline uint32_t rdLE32(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
         (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

inline uint16_t rdLE16(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint16_t>(static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8));
}

// Strict parse of the converter's BMP output. Returns false on any structural
// inconsistency so tests can assert "output parses" as a single expectation.
inline bool parseBmp(const std::vector<uint8_t>& b, ParsedBmp& out) {
  if (b.size() < 54) return false;
  if (b[0] != 'B' || b[1] != 'M') return false;
  out.fileSizeField = rdLE32(b, 2);
  out.dataOffset = rdLE32(b, 10);
  if (rdLE32(b, 14) != 40) return false;  // BITMAPINFOHEADER
  out.width = static_cast<int32_t>(rdLE32(b, 18));
  out.heightRaw = static_cast<int32_t>(rdLE32(b, 22));
  if (rdLE16(b, 26) != 1) return false;  // planes
  out.bitCount = rdLE16(b, 28);
  out.compression = rdLE32(b, 30);
  out.imageSizeField = rdLE32(b, 34);
  out.colorsUsed = rdLE32(b, 46);
  if (out.compression != 0) return false;
  if (out.bitCount != 1 && out.bitCount != 2 && out.bitCount != 8) return false;
  if (out.width <= 0 || out.heightRaw == 0) return false;
  const size_t paletteBytes = static_cast<size_t>(out.colorsUsed) * 4;
  if (out.dataOffset != 14 + 40 + paletteBytes) return false;
  out.bytesPerRow = (static_cast<size_t>(out.width) * out.bitCount + 31) / 32 * 4;
  const size_t expected = out.dataOffset + out.bytesPerRow * static_cast<size_t>(out.absHeight());
  if (b.size() != expected) return false;
  if (out.fileSizeField != expected) return false;
  out.pixelData.assign(b.begin() + out.dataOffset, b.end());
  return true;
}

// FNV-1a 64-bit, for pinning golden outputs.
inline uint64_t fnv1a64(const std::vector<uint8_t>& data) {
  uint64_t h = 1469598103934665603ULL;
  for (const uint8_t b : data) {
    h ^= b;
    h *= 1099511628211ULL;
  }
  return h;
}

inline std::vector<uint8_t> readFileBytes(const std::string& path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len > 0) {
    out.resize(static_cast<size_t>(len));
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  }
  std::fclose(f);
  return out;
}

inline bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

}  // namespace imgtest
