#pragma once

// stdio-backed HalStorage/HalFile for the serialization suite.
//
// HalFile mirrors the production handle the Serialization.h / BufferedFile.h
// overloads are written against (int read(), size_t write(), seek/position/
// size) and counts every underlying call so the buffered wrappers' batching
// can be asserted. Whole-file helpers (readFile/writeFile/mkdir/exists) serve
// PersistableStore.cpp, which is compiled for the bounded extractPassword
// overload. Paths are used verbatim (tests pass host paths).

#include <Arduino.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace halstub {
inline size_t readCalls = 0;
inline size_t writeCalls = 0;
inline size_t seekCalls = 0;
inline bool failWrites = false;    // write() returns 0 bytes
inline bool failNextSeek = false;  // one seek() returns false without moving

inline void reset() {
  readCalls = 0;
  writeCalls = 0;
  seekCalls = 0;
  failWrites = false;
  failNextSeek = false;
}
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

  int read(void* buffer, size_t count) {
    ++halstub::readCalls;
    if (!file_) return -1;
    return static_cast<int>(std::fread(buffer, 1, count, file_));
  }

  size_t write(const void* buffer, size_t count) {
    ++halstub::writeCalls;
    if (!file_ || halstub::failWrites) return 0;
    return std::fwrite(buffer, 1, count, file_);
  }
  size_t write(const uint8_t* buffer, size_t count) { return write(static_cast<const void*>(buffer), count); }

  bool seek(size_t pos) {
    ++halstub::seekCalls;
    if (halstub::failNextSeek) {
      halstub::failNextSeek = false;
      return false;
    }
    return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
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

  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForWrite(const char*, const char* path, HalFile& file) { return file.open(path, "wb+"); }

  bool exists(const char* path) {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }
  bool mkdir(const char* path, bool = true) { return ::mkdir(path, 0755) == 0 || exists(path); }

  String readFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return String();
    std::string content;
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    std::fclose(f);
    return String(content);
  }

  bool writeFile(const char* path, const String& content) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(content.c_str(), 1, content.length(), f) == content.length();
    std::fclose(f);
    return ok;
  }
};

#define Storage HalStorage::getInstance()
