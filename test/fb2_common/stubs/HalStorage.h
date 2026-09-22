#pragma once

// POSIX/stdio-backed HalStorage stub shared by the FB2 host test suites.
// Extends the pattern used by test/chapter_html_slim_parser/stubs/HalStorage.h
// with seek(), mkdir(), removeDir() and a directory-aware exists() so the
// Fb2/Fb2Section cache code paths (setupCacheDir/clearCache) work on a host
// filesystem.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>

// Test hooks, shared by every HalFile/HalStorage in the process. openForReadCount counts
// openFileForRead calls, exactly what a device open costs. writeBudget, when set, is the
// number of bytes that may still be written across all files before write() starts
// returning 0, so a test can make a build fail part-way.
namespace halstub {
inline size_t openForReadCount = 0;
inline bool writeBudgetSet = false;
inline size_t writeBudget = 0;
}  // namespace halstub

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
  size_t read(void* buffer, size_t count) { return file_ ? std::fread(buffer, 1, count, file_) : 0; }
  size_t write(const void* buffer, size_t count) {
    if (!file_) return 0;
    if (halstub::writeBudgetSet) {
      if (count > halstub::writeBudget) {
        halstub::writeBudget = 0;
        return 0;
      }
      halstub::writeBudget -= count;
    }
    return std::fwrite(buffer, 1, count, file_);
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() { return file_ && std::fflush(file_) == 0; }
  bool seek(size_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekCur(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }
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
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    ++halstub::openForReadCount;
    return file.open(path.c_str(), "rb");
  }
  size_t openForReadCount() const { return halstub::openForReadCount; }
  void failWritesAfter(const size_t bytes) {
    halstub::writeBudgetSet = true;
    halstub::writeBudget = bytes;
  }
  void resetOpenCounts() {
    halstub::openForReadCount = 0;
    halstub::writeBudgetSet = false;
    halstub::writeBudget = 0;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }
  bool remove(const char* path) { return ::remove(path) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
  // Mirrors FatFile::rename, which opens the destination O_CREAT | O_EXCL and
  // therefore FAILS when it already exists. POSIX ::rename overwrites silently,
  // so forwarding to it would let a missing remove-before-rename pass on host
  // and fail on device.
  bool rename(const char* from, const char* to) {
    if (exists(to)) return false;
    return ::rename(from, to) == 0;
  }
  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string full(path);
    if (pFlag) {
      // Create parent components first (mkdir -p semantics, like SdFat's pFlag).
      for (size_t pos = full.find('/', 1); pos != std::string::npos; pos = full.find('/', pos + 1)) {
        ::mkdir(full.substr(0, pos).c_str(), 0755);
      }
    }
    return ::mkdir(full.c_str(), 0755) == 0 || exists(path);
  }
  bool removeDir(const char* path) {
    DIR* dir = ::opendir(path);
    if (!dir) return false;
    const std::string base(path);
    while (dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      const std::string child = base + "/" + name;
      struct stat st{};
      if (::stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        removeDir(child.c_str());
      } else {
        ::remove(child.c_str());
      }
    }
    ::closedir(dir);
    return ::rmdir(path) == 0;
  }
};

#define Storage HalStorage::getInstance()

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
