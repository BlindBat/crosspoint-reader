#pragma once

// POSIX/stdio-backed HalStorage stub for the TXT reader host suite.
//
// Production code addresses absolute SD paths ("/books/x.txt", "/.crosspoint");
// every path is remapped under a test-controlled root so the suite runs in a
// temporary directory. HalFile mirrors the SdFat-backed device handle closely
// enough for Txt.cpp: read() returns int, seek() past EOF fails on a file
// opened for reading, and the destructor closes the file.

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
  int available() const { return file_ ? static_cast<int>(size() - position()) : 0; }
  int read(void* buffer, size_t count) {
    halfile_stack_probe::sample();
    return file_ ? static_cast<int>(std::fread(buffer, 1, count, file_)) : -1;
  }
  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  void flush() {
    if (file_) std::fflush(file_);
  }
  bool seek(size_t pos) {
    if (!file_ || pos > size()) return false;
    return std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const {
    if (!file_) return 0;
    const long offset = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, offset, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  std::string root;  // filesystem prefix for every device-side path
  std::string mapPath(const char* path) const { return root + path; }

  // Counts every file opened for writing so tests can pin the redundant-write
  // guards (AGENTS.md "SD Persistence Throttling").
  int writeCount = 0;
  std::vector<std::string> writtenPaths;  // unmapped (device-side) paths

  void resetCounters() {
    writeCount = 0;
    writtenPaths.clear();
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    return file.open(mapPath(path.c_str()).c_str(), "rb");
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    writeCount++;
    writtenPaths.push_back(path);
    return file.open(mapPath(path.c_str()).c_str(), "wb");
  }
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(mapPath(path).c_str(), &st) == 0;
  }
  bool remove(const char* path) { return ::remove(mapPath(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) { return ::rename(mapPath(from).c_str(), mapPath(to).c_str()) == 0; }
  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string full = mapPath(path);
    if (pFlag) {
      for (size_t pos = full.find('/', root.size() + 1); pos != std::string::npos; pos = full.find('/', pos + 1)) {
        ::mkdir(full.substr(0, pos).c_str(), 0755);
      }
    }
    return ::mkdir(full.c_str(), 0755) == 0 || exists(path);
  }
  bool removeDir(const char* path) { return removeTree(mapPath(path)); }

 private:
  static bool removeTree(const std::string& full) {
    DIR* dir = ::opendir(full.c_str());
    if (!dir) return false;
    while (dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      const std::string child = full + "/" + name;
      struct stat st{};
      if (::stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        removeTree(child);
      } else {
        ::remove(child.c_str());
      }
    }
    ::closedir(dir);
    return ::rmdir(full.c_str()) == 0;
  }
};

#define Storage HalStorage::getInstance()
