#pragma once

// Host-side stdio-backed stand-in for lib/hal/HalStorage.h, adapted from the
// chapter_html_slim_parser suite with the 64-bit seek/size API XtcParser uses.

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
  size_t read(void* buffer, size_t count) { return file_ ? std::fread(buffer, 1, count, file_) : 0; }
  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  bool seek(size_t pos) { return seek64(pos); }
  bool seek64(uint64_t pos) {
    // SdFat rejects seeks past EOF for read-opened files; mirror that so the
    // parser sees device-like failure modes instead of stdio's permissive fseek.
    if (!file_ || pos > fileSize64()) return false;
    return std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
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
};

#define Storage HalStorage::getInstance()
