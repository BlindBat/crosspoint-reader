#pragma once

// Programmatic XTC/XTCH containers for the xtc_fb2_readers suite plus a strict
// reader for the 1-bit/2-bit BMPs Xtc.cpp writes. Layout follows
// contracts/xtc-format.md: 56-byte header, optional 192-byte metadata block,
// optional 96-byte chapter records, 16-byte page table entries, then per-page
// 22-byte XTG/XTH header + bitmap.

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "Xtc/XtcTypes.h"

namespace xtcfix {

inline void putU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v & 0xFF));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
inline void putU32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
inline void putU64(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

// XTG bitmap: row-major, MSB first, bit 1 = white.
inline std::vector<uint8_t> xtgBitmap(uint16_t w, uint16_t h, const std::function<bool(int, int)>& white) {
  const size_t rowBytes = (w + 7) / 8;
  std::vector<uint8_t> out(rowBytes * h, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (white(x, y)) out[y * rowBytes + x / 8] |= static_cast<uint8_t>(1u << (7 - (x % 8)));
    }
  }
  return out;
}

// True when every pixel of a w*h XTH page has an addressable plane offset.
// Planes are (w*h+7)/8 bytes but are indexed column-major at (h+7)/8 bytes per
// column, so the two agree only when the height is a multiple of 8.
inline bool xthLayoutIsAddressable(uint16_t w, uint16_t h) {
  return static_cast<size_t>(w) * ((h + 7) / 8) <= (static_cast<size_t>(w) * h + 7) / 8;
}

// XTH bitmap: two planes, column-major right-to-left, 8 vertical pixels per
// byte (MSB = top), value = (bit1 << 1) | bit2 with 0 white .. 3 black.
// Pixels whose column offset falls past the plane (see xthLayoutIsAddressable)
// are dropped rather than written out of range.
inline std::vector<uint8_t> xthBitmap(uint16_t w, uint16_t h, const std::function<int(int, int)>& value) {
  const size_t planeSize = (static_cast<size_t>(w) * h + 7) / 8;
  const size_t colBytes = (h + 7) / 8;
  std::vector<uint8_t> out(planeSize * 2, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int v = value(x, y) & 3;
      const size_t off = static_cast<size_t>(w - 1 - x) * colBytes + y / 8;
      if (off >= planeSize) continue;
      const uint8_t bit = static_cast<uint8_t>(1u << (7 - (y % 8)));
      if (v & 2) out[off] |= bit;
      if (v & 1) out[planeSize + off] |= bit;
    }
  }
  return out;
}

struct PageSpec {
  uint16_t tableWidth = 0;
  uint16_t tableHeight = 0;
  uint16_t headerWidth = 0;
  uint16_t headerHeight = 0;
  std::vector<uint8_t> bitmap;
};

inline PageSpec page(uint16_t w, uint16_t h, std::vector<uint8_t> bitmap) {
  PageSpec p;
  p.tableWidth = p.headerWidth = w;
  p.tableHeight = p.headerHeight = h;
  p.bitmap = std::move(bitmap);
  return p;
}

// 96-byte chapter record; pages are 1-based on disk.
struct ChapterSpec {
  std::string name;
  uint16_t startPage = 0;
  uint16_t endPage = 0;
};

struct XtcSpec {
  bool twoBit = false;
  bool metadata = true;
  std::string title = "Fixture Book";
  std::string author = "QA";
  std::vector<ChapterSpec> chapters;  // written between the metadata and the page table
  std::vector<PageSpec> pages;
  uint32_t pageMagic = 0;   // 0 = derive from twoBit
  size_t truncateTail = 0;  // bytes dropped from the end of the finished file
};

inline std::vector<uint8_t> buildXtc(const XtcSpec& spec) {
  const size_t metadataSize = spec.metadata ? 128 + 64 : 0;
  const uint64_t chapterOffset = sizeof(xtc::XtcHeader) + metadataSize;
  const size_t chapterSize = spec.chapters.size() * 96;
  const uint64_t pageTableOffset = chapterOffset + chapterSize;
  const uint64_t dataOffset = pageTableOffset + spec.pages.size() * sizeof(xtc::PageTableEntry);

  std::vector<uint8_t> b;
  putU32(b, spec.twoBit ? xtc::XTCH_MAGIC : xtc::XTC_MAGIC);
  b.push_back(1);  // versionMajor
  b.push_back(0);  // versionMinor
  putU16(b, static_cast<uint16_t>(spec.pages.size()));
  b.push_back(0);                              // readDirection
  b.push_back(spec.metadata ? 1 : 0);          // hasMetadata
  b.push_back(0);                              // hasThumbnails
  b.push_back(spec.chapters.empty() ? 0 : 1);  // hasChapters
  putU32(b, 1);                                // currentPage
  putU64(b, spec.metadata ? 0x38 : 0);         // metadataOffset
  putU64(b, pageTableOffset);
  putU64(b, dataOffset);
  putU64(b, 0);  // thumbOffset
  putU32(b, spec.chapters.empty() ? 0 : static_cast<uint32_t>(chapterOffset));
  putU32(b, 0);  // padding

  if (spec.metadata) {
    std::vector<uint8_t> title(128, 0), author(64, 0);
    std::memcpy(title.data(), spec.title.data(), std::min<size_t>(spec.title.size(), 127));
    std::memcpy(author.data(), spec.author.data(), std::min<size_t>(spec.author.size(), 63));
    b.insert(b.end(), title.begin(), title.end());
    b.insert(b.end(), author.begin(), author.end());
  }

  for (const auto& c : spec.chapters) {
    std::vector<uint8_t> record(96, 0);
    std::memcpy(record.data(), c.name.data(), std::min<size_t>(c.name.size(), 79));
    record[0x50] = static_cast<uint8_t>(c.startPage & 0xFF);
    record[0x51] = static_cast<uint8_t>(c.startPage >> 8);
    record[0x52] = static_cast<uint8_t>(c.endPage & 0xFF);
    record[0x53] = static_cast<uint8_t>(c.endPage >> 8);
    b.insert(b.end(), record.begin(), record.end());
  }

  uint64_t cursor = dataOffset;
  for (const auto& p : spec.pages) {
    putU64(b, cursor);
    putU32(b, static_cast<uint32_t>(sizeof(xtc::XtgPageHeader) + p.bitmap.size()));
    putU16(b, p.tableWidth);
    putU16(b, p.tableHeight);
    cursor += sizeof(xtc::XtgPageHeader) + p.bitmap.size();
  }

  for (const auto& p : spec.pages) {
    putU32(b, spec.pageMagic ? spec.pageMagic : (spec.twoBit ? xtc::XTH_MAGIC : xtc::XTG_MAGIC));
    putU16(b, p.headerWidth);
    putU16(b, p.headerHeight);
    b.push_back(0);  // colorMode
    b.push_back(0);  // compression
    putU32(b, static_cast<uint32_t>(p.bitmap.size()));
    putU64(b, 0);  // md5
    b.insert(b.end(), p.bitmap.begin(), p.bitmap.end());
  }

  if (spec.truncateTail > 0 && spec.truncateTail < b.size()) b.resize(b.size() - spec.truncateTail);
  return b;
}

inline bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

inline std::vector<uint8_t> readFile(const std::string& path) {
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

inline bool fileExists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

// RAII temp directory under $TMPDIR, removed recursively.
class TempDir {
 public:
  TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/tmp") + "/xtcfb2test.XXXXXX";
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", tmpl.c_str());
    path_ = ::mkdtemp(buffer) ? buffer : "";
  }
  ~TempDir() {
    if (!path_.empty()) {
      const std::string cmd = "rm -rf '" + path_ + "'";
      [[maybe_unused]] const int rc = std::system(cmd.c_str());
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

inline uint32_t rdU32(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
         (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
inline uint16_t rdU16(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

struct Bmp {
  uint32_t fileSize = 0;
  uint32_t offBits = 0;
  int32_t width = 0;
  int32_t heightRaw = 0;  // negative = top-down
  uint16_t bitCount = 0;
  uint32_t imageSize = 0;
  uint32_t colorsUsed = 0;
  std::vector<uint32_t> palette;  // 0x00RRGGBB per entry
  size_t rowBytes = 0;
  std::vector<uint8_t> pixels;

  int absHeight() const { return heightRaw < 0 ? -heightRaw : heightRaw; }
  const uint8_t* row(int y) const { return pixels.data() + static_cast<size_t>(y) * rowBytes; }
  int pixel(int x, int y) const {
    const uint8_t* r = row(y);
    if (bitCount == 1) return (r[x / 8] >> (7 - (x % 8))) & 1;
    if (bitCount == 2) return (r[(x * 2) / 8] >> (6 - ((x * 2) % 8))) & 3;
    return -1;
  }
};

// Strict parse: every size field must agree with the byte count.
inline bool parseBmp(const std::vector<uint8_t>& b, Bmp& out) {
  if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
  out.fileSize = rdU32(b, 2);
  out.offBits = rdU32(b, 10);
  if (rdU32(b, 14) != 40) return false;
  out.width = static_cast<int32_t>(rdU32(b, 18));
  out.heightRaw = static_cast<int32_t>(rdU32(b, 22));
  if (rdU16(b, 26) != 1) return false;
  out.bitCount = rdU16(b, 28);
  if (rdU32(b, 30) != 0) return false;
  out.imageSize = rdU32(b, 34);
  out.colorsUsed = rdU32(b, 46);
  if (out.width <= 0 || out.heightRaw == 0) return false;
  if (out.offBits != 54 + out.colorsUsed * 4) return false;
  out.palette.clear();
  for (uint32_t i = 0; i < out.colorsUsed; ++i) {
    const size_t p = 54 + i * 4;
    out.palette.push_back((static_cast<uint32_t>(b[p + 2]) << 16) | (static_cast<uint32_t>(b[p + 1]) << 8) | b[p]);
  }
  out.rowBytes = (static_cast<size_t>(out.width) * out.bitCount + 31) / 32 * 4;
  const size_t expected = out.offBits + out.rowBytes * static_cast<size_t>(out.absHeight());
  if (b.size() != expected || out.fileSize != expected) return false;
  if (out.imageSize != out.rowBytes * static_cast<size_t>(out.absHeight())) return false;
  out.pixels.assign(b.begin() + out.offBits, b.end());
  return true;
}

}  // namespace xtcfix
