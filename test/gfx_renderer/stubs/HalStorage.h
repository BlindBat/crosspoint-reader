#pragma once

// POSIX-backed HalStorage stub shared by the rendering and font-system suites.
//
// Adapted from test/sdcard_font/stubs/HalStorage.h with the additional HalFile
// surface Bitmap.cpp and FontInstaller.cpp use on the device:
//   - read() of a single byte, seek()/seekCur() with SdFat's past-EOF failure,
//     fileSize(), and openFileForWrite()/write(),
//   - mkdir()/remove()/removeDir() over the host filesystem.
// Every path is remapped under halstub::root so the production code's absolute
// SD paths ("/.fonts/...") land in a per-test sandbox. Directory entries are
// sorted by name so registry discovery is deterministic.

#include <Arduino.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace halstub {
// Prepended to every path the production code passes in. Empty = passthrough.
inline std::string root;
inline std::string map(const char* path) { return root + path; }
}  // namespace halstub

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      other.file_ = nullptr;
      isDir_ = other.isDir_;
      valid_ = other.valid_;
      name_ = std::move(other.name_);
      entries_ = std::move(other.entries_);
      nextEntry_ = other.nextEntry_;
      other.valid_ = false;
      other.isDir_ = false;
      other.entries_.clear();
      other.nextEntry_ = 0;
    }
    return *this;
  }

  bool openRead(const std::string& mappedPath) {
    close();
    file_ = std::fopen(mappedPath.c_str(), "rb");
    valid_ = file_ != nullptr;
    return valid_;
  }
  bool openWrite(const std::string& mappedPath) {
    close();
    file_ = std::fopen(mappedPath.c_str(), "wb");
    valid_ = file_ != nullptr;
    return valid_;
  }

  bool openAny(const std::string& mappedPath) {
    close();
    struct stat st{};
    if (::stat(mappedPath.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
      DIR* dir = ::opendir(mappedPath.c_str());
      if (!dir) return false;
      while (dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat entrySt{};
        const bool entryIsDir = ::stat((mappedPath + "/" + name).c_str(), &entrySt) == 0 && S_ISDIR(entrySt.st_mode);
        entries_.emplace_back(name, entryIsDir);
      }
      ::closedir(dir);
      std::sort(entries_.begin(), entries_.end());
      isDir_ = true;
      valid_ = true;
      return true;
    }
    return openRead(mappedPath);
  }

  int read(void* buffer, size_t count) {
    if (!file_) return -1;
    return static_cast<int>(std::fread(buffer, 1, count, file_));
  }
  int read() {
    if (!file_) return -1;
    const int c = std::fgetc(file_);
    return c == EOF ? -1 : c;
  }
  size_t write(const void* buffer, size_t count) {
    if (!file_) return 0;
    return std::fwrite(buffer, 1, count, file_);
  }
  size_t write(uint8_t b) { return write(&b, 1); }
  bool seekSet(size_t offset) {
    // SdFat refuses to seek past the end of a file.
    if (!file_ || offset > size()) return false;
    return std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0;
  }
  bool seek(size_t pos) { return seekSet(pos); }
  bool seekCur(int64_t offset) {
    if (!file_) return false;
    const long pos = std::ftell(file_);
    if (pos < 0 || pos + offset < 0) return false;
    return seekSet(static_cast<size_t>(pos + offset));
  }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const {
    if (!file_) return 0;
    const long pos = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, pos, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }
  size_t fileSize() const { return size(); }

  bool isDirectory() const { return isDir_; }
  HalFile openNextFile() {
    HalFile next;
    if (isDir_ && nextEntry_ < entries_.size()) {
      next.name_ = entries_[nextEntry_].first;
      next.isDir_ = entries_[nextEntry_].second;
      next.valid_ = true;
      nextEntry_++;
    }
    return next;
  }
  size_t getName(char* name, size_t len) {
    if (len == 0) return 0;
    const size_t n = std::min(name_.size(), len - 1);
    std::memcpy(name, name_.data(), n);
    name[n] = '\0';
    return n;
  }

  bool close() {
    entries_.clear();
    nextEntry_ = 0;
    isDir_ = false;
    valid_ = false;
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }
  bool isOpen() const { return valid_; }
  explicit operator bool() const { return valid_; }

 private:
  std::FILE* file_ = nullptr;
  bool isDir_ = false;
  bool valid_ = false;
  std::string name_;
  std::vector<std::pair<std::string, bool>> entries_;
  size_t nextEntry_ = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.openRead(halstub::map(path)); }
  bool openFileForRead(const char* mod, const std::string& path, HalFile& file) {
    return openFileForRead(mod, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) { return file.openWrite(halstub::map(path)); }
  HalFile open(const char* path) {
    HalFile file;
    file.openAny(halstub::map(path));
    return file;
  }
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(halstub::map(path).c_str(), &st) == 0;
  }
  bool mkdir(const char* path, const bool pFlag = true) {
    std::error_code ec;
    const std::filesystem::path full = halstub::map(path);
    if (pFlag) {
      std::filesystem::create_directories(full, ec);
    } else {
      std::filesystem::create_directory(full, ec);
    }
    return exists(path);
  }
  bool remove(const char* path) { return ::remove(halstub::map(path).c_str()) == 0; }
  bool removeDir(const char* path) {
    std::error_code ec;
    std::filesystem::remove_all(halstub::map(path), ec);
    return !ec && !exists(path);
  }
};

#define Storage HalStorage::getInstance()
