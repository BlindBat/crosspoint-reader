#pragma once

// stdio-backed HalStorage/HalFile for the firmware_flash suite: the flasher
// only opens files for reading. Paths are used verbatim (tests pass host
// paths). Reads can be made to fail after a byte budget to drive READ_FAIL.

#include <cstdint>
#include <cstdio>
#include <string>

namespace halstub {
inline size_t readBudget = SIZE_MAX;  // bytes the stub still serves before read() returns 0
}

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

  int read(void* buffer, size_t count) {
    if (!file_) return -1;
    if (count > halstub::readBudget) return 0;
    const size_t got = std::fread(buffer, 1, count, file_);
    halstub::readBudget -= got;
    return static_cast<int>(got);
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
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
};

#define Storage HalStorage::getInstance()
