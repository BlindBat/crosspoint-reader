#pragma once

// POSIX/stdio-backed HalStorage stub for the image decode host suites,
// following test/fb2_common/stubs/HalStorage.h. One deliberate difference:
// seek()/seekCur() refuse to move outside [0, size] like the device's
// SdFat-backed HalFile does, so tests of truncated files exercise the same
// failure path the firmware sees (a seek past EOF FAILS instead of silently
// succeeding as plain fseek would).

#include <Print.h>  // the real lib/hal/HalStorage.h pulls Print in transitively
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <string>

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
  int read(void* buffer, size_t count) { return file_ ? static_cast<int>(std::fread(buffer, 1, count, file_)) : -1; }
  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() { return file_ && std::fflush(file_) == 0; }
  bool seek(size_t pos) {
    if (!file_ || pos > size()) return false;
    return std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  bool seekCur(int64_t offset) {
    if (!file_) return false;
    const int64_t target = static_cast<int64_t>(position()) + offset;
    if (target < 0 || target > static_cast<int64_t>(size())) return false;
    return std::fseek(file_, static_cast<long>(target), SEEK_SET) == 0;
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
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }
  bool remove(const char* path) { return ::remove(path) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
};

#define STORAGE (HalStorage::getInstance())
