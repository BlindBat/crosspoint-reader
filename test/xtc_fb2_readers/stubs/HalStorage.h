#pragma once

// POSIX/stdio-backed HalStorage stub for the xtc_fb2_readers suite: the
// xtc_parser stub (64-bit seek/size, seek past EOF refused like SdFat) plus the
// write/exists/mkdir/removeDir surface Xtc.cpp needs for cover and thumbnail
// generation. Paths are used verbatim; tests pass absolute temp-dir paths.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Stack-depth probe. HalFile::read() records the deepest stack address it is
// ever called at, so a test can bound the stack frame of the production code
// driving the read loop.
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
  int available() { return file_ ? static_cast<int>(fileSize64() - position()) : 0; }
  size_t read(void* buffer, size_t count) {
    halfile_stack_probe::sample();
    return file_ ? std::fread(buffer, 1, count, file_) : 0;
  }
  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  void flush() {
    if (file_) std::fflush(file_);
  }
  bool seek(size_t pos) { return seek64(pos); }
  bool seek64(uint64_t pos) {
    if (!file_ || pos > fileSize64()) return false;
    return std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  uint64_t fileSize64() {
    if (!file_) return 0;
    const long offset = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, offset, SEEK_SET);
    return end > 0 ? static_cast<uint64_t>(end) : 0;
  }
  size_t size() { return static_cast<size_t>(fileSize64()); }
  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    writeCount++;
    writtenPaths.emplace_back(path);
    return file.open(path, "wb");
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    return openFileForWrite(module, path.c_str(), file);
  }
  bool rename(const char* from, const char* to) { return ::rename(from, to) == 0; }

  // Counts every file opened for writing so tests can pin the redundant-write
  // guards (AGENTS.md "SD Persistence Throttling").
  int writeCount = 0;
  std::vector<std::string> writtenPaths;

  void resetCounters() {
    writeCount = 0;
    writtenPaths.clear();
  }

  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }
  bool mkdir(const char* path, bool = true) { return ::mkdir(path, 0755) == 0 || exists(path); }
  bool remove(const char* path) { return ::remove(path) == 0; }
  bool removeDir(const char* path) {
    DIR* dir = ::opendir(path);
    if (!dir) return false;
    while (dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      const std::string full = std::string(path) + "/" + name;
      struct stat st{};
      if (::stat(full.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) {
        removeDir(full.c_str());
      } else {
        ::unlink(full.c_str());
      }
    }
    ::closedir(dir);
    return ::rmdir(path) == 0;
  }
};

#define Storage HalStorage::getInstance()
