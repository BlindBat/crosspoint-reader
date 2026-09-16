#pragma once

// stdio-backed HalStorage/HalFile for the platform helper suite. Every
// device path is remapped under HalStorage::root so the compiled production
// code (ZipFile, HalSystem::checkPanic, ScreenshotUtil, sleep image
// validators) reads and writes real files inside the test's temp directory.
//
// Semantics mirror the SdFat-backed HalFile where the production code relies
// on them: read() returns -1 at EOF, read(buf, n) returns the byte count,
// seek() past the end fails, available() clamps at 0.

#include <fcntl.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "Print.h"

typedef int oflag_t;
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif
#ifndef O_READ
#define O_READ O_RDONLY
#endif

class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() override { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept : file_(other.file_), writeCap_(other.writeCap_) { other.file_ = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      writeCap_ = other.writeCap_;
      other.file_ = nullptr;
    }
    return *this;
  }

  bool open(const char* path, const char* mode, size_t writeCap = SIZE_MAX) {
    close();
    file_ = std::fopen(path, mode);
    writeCap_ = writeCap;
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

  size_t write(const void* buffer, size_t count) {
    if (!file_) return 0;
    const size_t allowed = count < writeCap_ ? count : writeCap_;
    const size_t n = std::fwrite(buffer, 1, allowed, file_);
    writeCap_ -= n;
    return n;
  }
  size_t write(const uint8_t* buffer, size_t count) override { return write(static_cast<const void*>(buffer), count); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }

  void flush() override {
    if (file_) std::fflush(file_);
  }

  bool seek(size_t pos) { return file_ && pos <= size() && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) {
    if (!file_) return false;
    const int64_t target = static_cast<int64_t>(position()) + offset;
    if (target < 0 || static_cast<size_t>(target) > size()) return false;
    return std::fseek(file_, static_cast<long>(target), SEEK_SET) == 0;
  }

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
  operator bool() const { return isOpen(); }

 private:
  std::FILE* file_ = nullptr;
  size_t writeCap_ = SIZE_MAX;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  // --- test controls -------------------------------------------------------
  std::string root;                  // filesystem prefix for every mapped path
  bool failNextOpen = false;         // next open for write/read fails
  size_t writeCap = SIZE_MAX;        // bytes the next opened-for-write file accepts
  std::string lastWritePath;         // unmapped device path of the last write open
  int writeOpens = 0;

  void reset() {
    failNextOpen = false;
    writeCap = SIZE_MAX;
    lastWritePath.clear();
    writeOpens = 0;
  }

  std::string mapPath(const char* path) const { return root + path; }

  // --- API surface used by the compiled production code --------------------
  bool exists(const char* path) {
    struct stat st{};
    return ::stat(mapPath(path).c_str(), &st) == 0;
  }

  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string full = mapPath(path);
    if (pFlag) {
      for (size_t pos = full.find('/', root.size() + 1); pos != std::string::npos; pos = full.find('/', pos + 1)) {
        ::mkdir(full.substr(0, pos).c_str(), 0755);
      }
    }
    return ::mkdir(full.c_str(), 0755) == 0 || exists(path);
  }

  bool remove(const char* path) { return ::remove(mapPath(path).c_str()) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to) { return ::rename(mapPath(from).c_str(), mapPath(to).c_str()) == 0; }

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY) {
    HalFile file;
    const bool forWrite = (oflag & (O_WRONLY | O_RDWR)) != 0;
    if (forWrite) {
      openForWrite(path, file);
    } else {
      openForRead(path, file);
    }
    return file;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return openForRead(path, file); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return openForRead(path.c_str(), file); }
  bool openFileForWrite(const char*, const char* path, HalFile& file) { return openForWrite(path, file); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    return openForWrite(path.c_str(), file);
  }

 private:
  bool openForRead(const char* path, HalFile& file) {
    if (failNextOpen) {
      failNextOpen = false;
      return false;
    }
    return file.open(mapPath(path).c_str(), "rb");
  }
  bool openForWrite(const char* path, HalFile& file) {
    writeOpens++;
    lastWritePath = path;
    if (failNextOpen) {
      failNextOpen = false;
      return false;
    }
    const bool ok = file.open(mapPath(path).c_str(), "wb", writeCap);
    writeCap = SIZE_MAX;
    return ok;
  }
};

#define Storage HalStorage::getInstance()
