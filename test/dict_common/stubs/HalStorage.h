#pragma once

// Host test stub for the firmware storage HAL, backed by stdio, shared by the
// dictionary suites (test/dictionary, test/dict_zip).
//
// Extends the surface of test/zip_file/stubs/HalStorage.h with what
// src/util/Dictionary.cpp needs on top of reads:
//   - openFileForWrite / Storage.open(path, oflag) for the sidecar and the
//     .dict.dz extraction temp file (returns HalFile by value -> movable)
//   - exists()/remove() with const char* paths
//   - a settable sandbox root: production passes device-absolute paths
//     ("/dictionaries/<folder>/<stem>.idx", "/.crosspoint/dict.tmp"), which the
//     stub remaps under dictstub::storageRoot so each test runs in its own
//     host-side sandbox directory. An empty root uses paths verbatim (the
//     dict_zip suite passes host-absolute fixture paths directly).

#include <cstdint>
#include <cstdio>
#include <string>

namespace dictstub {
inline std::string storageRoot;  // empty -> no remapping

inline std::string mapPath(const char* path) {
  if (storageRoot.empty()) return path;
  return storageRoot + path;  // production paths always start with '/'
}
inline std::string mapPath(const std::string& path) { return mapPath(path.c_str()); }
}  // namespace dictstub

// Open flags: the real header gets oflag_t and the O_* names from SdFat's
// FsApiConstants.h. On the host, take the POSIX names from <fcntl.h> (so a
// later system include can't clash) and add SdFat's O_WRITE alias.
#include <fcntl.h>
using oflag_t = int;
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept : file_(other.file_) { other.file_ = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      other.file_ = nullptr;
    }
    return *this;
  }

  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }

  int available() const {
    if (!file_) return 0;
    const size_t sz = size();
    const size_t pos = position();
    return pos < sz ? static_cast<int>(sz - pos) : 0;
  }

  int read(void* buffer, size_t count) {
    if (!file_) return -1;
    return static_cast<int>(std::fread(buffer, 1, count, file_));
  }

  int read() {
    if (!file_) return -1;
    return std::fgetc(file_);
  }

  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(const uint8_t* buffer, size_t count) { return write(static_cast<const void*>(buffer), count); }
  size_t write(uint8_t byte) { return write(&byte, 1); }

  void flush() {
    if (file_) std::fflush(file_);
  }

  bool seek(size_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seek64(uint64_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }

  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const {
    if (!file_) return 0;
    const long here = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, here, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }
  size_t fileSize() const { return size(); }

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

  bool openFileForRead(const char*, const char* path, HalFile& file) {
    return file.open(dictstub::mapPath(path).c_str(), "rb");
  }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    return file.open(dictstub::mapPath(path).c_str(), "wb+");
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    return openFileForWrite(module, path.c_str(), file);
  }

  // Only the write-mode form is exercised (readDefinition's dict.tmp).
  HalFile open(const char* path, const oflag_t oflag = O_RDONLY) {
    HalFile file;
    file.open(dictstub::mapPath(path).c_str(), oflag == O_RDONLY ? "rb" : "wb+");
    return file;
  }

  bool exists(const char* path) const {
    std::FILE* f = std::fopen(dictstub::mapPath(path).c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
  }
  bool remove(const char* path) { return std::remove(dictstub::mapPath(path).c_str()) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to) {
    return std::rename(dictstub::mapPath(from).c_str(), dictstub::mapPath(to).c_str()) == 0;
  }
};

#define Storage HalStorage::getInstance()
