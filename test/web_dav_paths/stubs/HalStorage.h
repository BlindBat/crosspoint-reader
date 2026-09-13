#pragma once

// POSIX-backed HalStorage stub for the WebDAV suite. Virtual absolute paths
// ("/Books/x.epub") are mapped under a per-test sandbox directory, and every
// virtual path the production handler passes to the storage layer is RECORDED
// so tests can assert that no un-normalized path (".." segment, missing
// leading '/') ever reaches the filesystem boundary.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "WString.h"

class HalStorage;

class HalFile {
  friend class HalStorage;

  std::FILE* file_ = nullptr;
  DIR* dir_ = nullptr;
  std::string realPath_;
  std::string name_;  // entry name (SdFat getName semantics)

 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept { *this = static_cast<HalFile&&>(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      dir_ = other.dir_;
      realPath_ = other.realPath_;
      name_ = other.name_;
      other.file_ = nullptr;
      other.dir_ = nullptr;
    }
    return *this;
  }

  bool isOpen() const { return file_ != nullptr || dir_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() const { return dir_ != nullptr; }

  size_t getName(char* name, size_t len) {
    if (len == 0) return 0;
    const size_t n = name_.copy(name, len - 1);
    name[n] = '\0';
    return n;
  }

  size_t size() {
    if (!file_) return 0;
    const long pos = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, pos, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }

  int available() {
    if (!file_) return 0;
    const long pos = std::ftell(file_);
    return static_cast<int>(size()) - static_cast<int>(pos);
  }

  int read(void* buf, size_t count) { return file_ ? static_cast<int>(std::fread(buf, 1, count, file_)) : 0; }
  size_t write(const void* buf, size_t count) { return file_ ? std::fwrite(buf, 1, count, file_) : 0; }
  size_t write(const uint8_t* buf, size_t count) { return write(static_cast<const void*>(buf), count); }
  size_t write(uint8_t b) { return write(&b, 1); }

  bool rename(const char* newVirtualPath);  // defined after HalStorage

  bool close() {
    bool ok = true;
    if (file_) {
      ok = std::fclose(file_) == 0;
      file_ = nullptr;
    }
    if (dir_) {
      closedir(dir_);
      dir_ = nullptr;
    }
    return ok;
  }

  HalFile openNextFile() {
    HalFile result;
    if (!dir_) return result;
    while (dirent* entry = readdir(dir_)) {
      const std::string entryName = entry->d_name;
      if (entryName == "." || entryName == "..") continue;
      const std::string entryReal = realPath_ + "/" + entryName;
      struct stat st{};
      if (::stat(entryReal.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) {
        result.dir_ = opendir(entryReal.c_str());
      } else {
        result.file_ = std::fopen(entryReal.c_str(), "rb");
      }
      result.realPath_ = entryReal;
      result.name_ = entryName;
      break;
    }
    return result;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  // --- test control surface ---
  std::string sandboxRoot;                 // real directory backing virtual "/"
  std::vector<std::string> receivedPaths;  // every virtual path seen, in order

  void resetForTest(const std::string& root) {
    sandboxRoot = root;
    receivedPaths.clear();
  }

  std::string map(const char* virtualPath) {
    receivedPaths.emplace_back(virtualPath ? virtualPath : "");
    std::string p = virtualPath ? virtualPath : "";
    if (p.empty() || p[0] != '/') p = "/" + p;
    return sandboxRoot + p;
  }

  // --- production-facing API (subset used by WebDAVHandler) ---
  bool exists(const char* path) {
    struct stat st{};
    return ::stat(map(path).c_str(), &st) == 0;
  }

  bool mkdir(const char* path, bool = true) { return ::mkdir(map(path).c_str(), 0755) == 0; }

  bool remove(const char* path) { return ::unlink(map(path).c_str()) == 0; }

  bool rmdir(const char* path) { return ::rmdir(map(path).c_str()) == 0; }

  HalFile open(const char* path) {
    const std::string real = map(path);
    HalFile f;
    struct stat st{};
    if (::stat(real.c_str(), &st) != 0) return f;
    if (S_ISDIR(st.st_mode)) {
      f.dir_ = opendir(real.c_str());
    } else {
      f.file_ = std::fopen(real.c_str(), "rb");
    }
    f.realPath_ = real;
    const auto slash = real.find_last_of('/');
    f.name_ = slash == std::string::npos ? real : real.substr(slash + 1);
    return f;
  }

  bool openFileForWrite(const char*, const String& path, HalFile& file) {
    const std::string real = map(path.c_str());
    file.close();
    file.file_ = std::fopen(real.c_str(), "w+b");
    file.realPath_ = real;
    const auto slash = real.find_last_of('/');
    file.name_ = slash == std::string::npos ? real : real.substr(slash + 1);
    return file.file_ != nullptr;
  }
};

#define Storage HalStorage::getInstance()

inline bool HalFile::rename(const char* newVirtualPath) {
  if (realPath_.empty()) return false;
  const std::string newReal = HalStorage::getInstance().map(newVirtualPath);
  close();
  return ::rename(realPath_.c_str(), newReal.c_str()) == 0;
}
