#pragma once

// stdio-backed HalStorage/HalFile stub for the package parser suite.
//
// ContentOpfParser spills manifest items to <cachePath>/.items.bin, reopens
// it for reading during <spine>/<guide>, seeks by recorded offset and probes
// available(); the surface below matches the device HalFile semantics those
// calls rely on (int read counts, size()/position() from the FILE*).

#include <Print.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <string>

class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() override { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

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

  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(const uint8_t* buffer, size_t count) override { return write(static_cast<const void*>(buffer), count); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }

  void flush() {
    if (file_) std::fflush(file_);
  }

  bool seek(size_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const {
    if (!file_) return 0;
    const long here = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, here, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }

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

  // Test control: makes every openFileForWrite fail (simulates a full/absent SD).
  bool failWrites = false;

  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failWrites) return false;
    return file.open(path.c_str(), "wb");
  }
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }
  bool remove(const char* path) { return std::remove(path) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
};

#define Storage HalStorage::getInstance()
