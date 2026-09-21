#pragma once

// POSIX-backed HalStorage/HalFile stub for the reader-helpers host suite.
//
// Every device path ("/.crosspoint/...") is remapped under a test-controlled
// root. Beyond the PersistableStore surface (readFile/writeFile/mkdir/exists)
// it models the streaming HalFile API ProgressFile::writeAtomic drives, with
// fault-injection knobs for short writes, failed opens and failed renames, and
// SdFat's refusal to rename over an existing destination.

#include <Arduino.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  // --- test controls -------------------------------------------------------
  std::string root;  // filesystem prefix for every mapped path
  int writeCount = 0;
  std::vector<std::string> writtenPaths;  // unmapped (device-side) paths
  std::vector<std::string> ops;           // ordered log: "openW:<p>", "remove:<p>", "rename:<a>-><b>"
  bool failNextWrite = false;
  bool failNextOpenForWrite = false;
  bool failNextRename = false;
  // Maximum bytes a single HalFile::write() accepts; SIZE_MAX = unlimited.
  size_t maxWriteBytes = SIZE_MAX;

  void resetControls() {
    writeCount = 0;
    writtenPaths.clear();
    ops.clear();
    failNextWrite = false;
    failNextOpenForWrite = false;
    failNextRename = false;
    maxWriteBytes = SIZE_MAX;
  }

  std::string mapPath(const char* path) const { return root + path; }

  // --- API surface used by the compiled production code --------------------
  // Path-based size query (HalStorage::fileSize): 0 for missing paths and directories.
  size_t fileSize(const char* path) {
    struct ::stat st{};
    if (::stat(mapPath(path).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return 0;
    return static_cast<size_t>(st.st_size);
  }

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

  String readFile(const char* path) {
    std::FILE* f = std::fopen(mapPath(path).c_str(), "rb");
    if (!f) return String();
    std::string content;
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
      content.append(buf, n);
    }
    std::fclose(f);
    return String(content);
  }

  bool writeFile(const char* path, const String& content) {
    writeCount++;
    writtenPaths.push_back(path);
    if (failNextWrite) {
      failNextWrite = false;
      return false;
    }
    std::FILE* f = std::fopen(mapPath(path).c_str(), "wb");
    if (!f) return false;
    const size_t len = content.length();
    const bool ok = std::fwrite(content.c_str(), 1, len, f) == len;
    std::fclose(f);
    return ok;
  }

  bool remove(const char* path) {
    ops.push_back(std::string("remove:") + path);
    return ::remove(mapPath(path).c_str()) == 0;
  }

  // SdFat semantics: the destination must not exist.
  bool rename(const char* oldPath, const char* newPath) {
    ops.push_back(std::string("rename:") + oldPath + "->" + newPath);
    if (failNextRename) {
      failNextRename = false;
      return false;
    }
    if (exists(newPath)) return false;
    return ::rename(mapPath(oldPath).c_str(), mapPath(newPath).c_str()) == 0;
  }

  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
    return openFileForWrite(moduleName, path.c_str(), file);
  }
  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }
};

class HalFile {
  friend class HalStorage;
  std::FILE* fp = nullptr;

 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(HalFile&& other) noexcept : fp(other.fp) { other.fp = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      fp = other.fp;
      other.fp = nullptr;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  size_t write(const uint8_t* buf, const size_t count) {
    if (!fp) return 0;
    const size_t allowed = std::min(count, HalStorage::getInstance().maxWriteBytes);
    if (allowed == 0) return 0;  // buf may be null for an empty write; fwrite's pointer is nonnull
    return std::fwrite(buf, 1, allowed, fp);
  }
  size_t write(const void* buf, const size_t count) { return write(static_cast<const uint8_t*>(buf), count); }
  size_t write(const uint8_t b) { return write(&b, 1); }
  int read(void* buf, const size_t count) { return fp ? static_cast<int>(std::fread(buf, 1, count, fp)) : -1; }
  void flush() {
    if (fp) std::fflush(fp);
  }
  bool close() {
    if (!fp) return false;
    std::fclose(fp);
    fp = nullptr;
    return true;
  }
  bool isOpen() const { return fp != nullptr; }
  explicit operator bool() const { return isOpen(); }
};

inline bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  ops.push_back(std::string("openW:") + path);
  if (failNextOpenForWrite) {
    failNextOpenForWrite = false;
    return false;
  }
  file.close();
  file.fp = std::fopen(mapPath(path).c_str(), "wb");
  return file.fp != nullptr;
}

inline bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  file.close();
  file.fp = std::fopen(mapPath(path).c_str(), "rb");
  return file.fp != nullptr;
}

#define Storage HalStorage::getInstance()
