#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

// Stack-depth probe. HalFile::read() records the deepest stack address it is
// ever called at, which lets a test bound the frame of the production code
// driving the read loop (CssParser::loadFromStream).
namespace halfile_stack_probe {

#ifndef __has_feature
#define __has_feature(x) 0
#endif

// AddressSanitizer inserts a redzone around every local in every frame on the
// path, so a measured depth reflects instrumentation rather than the production
// frame. Budget assertions skip themselves under it.
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
inline constexpr bool kMeasurementIsReliable = false;
#else
inline constexpr bool kMeasurementIsReliable = true;
#endif

inline uintptr_t& deepest() {
  static uintptr_t value = 0;
  return value;
}

inline void reset() { deepest() = 0; }

inline void sample() {
  const char here = 0;
  const auto addr = reinterpret_cast<uintptr_t>(&here);
  if (deepest() == 0 || addr < deepest()) deepest() = addr;
}

// Bytes of stack consumed between `anchor` (a local in the calling test) and the
// deepest point reached inside read(). 0 when read() was never called.
inline size_t depthFrom(const void* anchor) {
  if (deepest() == 0) return 0;
  return static_cast<size_t>(reinterpret_cast<uintptr_t>(anchor) - deepest());
}

}  // namespace halfile_stack_probe

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }

  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }

  int available() const {
    if (!file_) return 0;
    const long position = std::ftell(file_);
    if (position < 0 || std::fseek(file_, 0, SEEK_END) != 0) return 0;
    const long end = std::ftell(file_);
    std::fseek(file_, position, SEEK_SET);
    return end >= position ? static_cast<int>(end - position) : 0;
  }

  int read(void* buffer, size_t count) {
    halfile_stack_probe::sample();
    if (!file_) return -1;
    return static_cast<int>(std::fread(buffer, 1, count, file_));
  }

  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }

  size_t write(uint8_t byte) { return write(&byte, 1); }

  bool seekCur(size_t count) { return file_ && std::fseek(file_, static_cast<long>(count), SEEK_CUR) == 0; }

  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }

  explicit operator bool() const { return file_ != nullptr; }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool exists(const char* path) const {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
  }

  bool remove(const char* path) { return std::remove(path) == 0; }
  bool rename(const char* from, const char* to) {
    if (from == failedRenameFrom_ && to == failedRenameTo_) {
      clearFailures();
      return false;
    }
    return std::rename(from, to) == 0;
  }

  void failNextRename(std::string from, std::string to) {
    failedRenameFrom_ = std::move(from);
    failedRenameTo_ = std::move(to);
  }

  void clearFailures() {
    failedRenameFrom_.clear();
    failedRenameTo_.clear();
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const char* path, HalFile& file) { return file.open(path, "wb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }

 private:
  std::string failedRenameFrom_;
  std::string failedRenameTo_;
};

#define Storage HalStorage::getInstance()
