#pragma once

// POSIX-backed HalStorage stand-in for the library-helpers host suite.
//
// Every device path is remapped under a test-controlled root directory. Only
// the directory-iteration surface NextBookFinder uses is provided: open(),
// HalFile::isDirectory/rewindDirectory/openNextFile/getName/close.

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

class HalFile {
 public:
  HalFile() = default;
  HalFile(std::string fullPath, std::string name, const bool valid)
      : fullPath_(std::move(fullPath)), name_(std::move(name)), valid_(valid) {}
  ~HalFile() { close(); }
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      fullPath_ = std::move(other.fullPath_);
      name_ = std::move(other.name_);
      dir_ = other.dir_;
      valid_ = other.valid_;
      other.dir_ = nullptr;
      other.valid_ = false;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  size_t getName(char* name, const size_t len) {
    if (len == 0) return 0;
    std::snprintf(name, len, "%s", name_.c_str());
    return std::strlen(name);
  }

  bool isDirectory() const {
    struct stat st{};
    return valid_ && ::stat(fullPath_.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  }

  void rewindDirectory() {
    if (dir_) {
      ::rewinddir(dir_);
    } else {
      dir_ = ::opendir(fullPath_.c_str());
    }
  }

  bool close() {
    if (dir_) {
      ::closedir(dir_);
      dir_ = nullptr;
    }
    valid_ = false;
    return true;
  }

  HalFile openNextFile() {
    if (!dir_) dir_ = ::opendir(fullPath_.c_str());
    if (!dir_) return {};
    while (dirent* entry = ::readdir(dir_)) {
      if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
      return HalFile(fullPath_ + "/" + entry->d_name, entry->d_name, true);
    }
    return {};
  }

  operator bool() const { return valid_; }  // NOLINT(google-explicit-constructor)

 private:
  std::string fullPath_;
  std::string name_;
  DIR* dir_ = nullptr;
  bool valid_ = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  std::string root;  // filesystem prefix for every mapped path (test control)

  std::string mapPath(const char* path) const { return root + path; }

  bool exists(const char* path) {
    struct stat st{};
    return ::stat(mapPath(path).c_str(), &st) == 0;
  }

  HalFile open(const char* path) {
    const std::string full = mapPath(path);
    const std::string device(path);
    const auto slash = device.find_last_of('/');
    const std::string name = slash == std::string::npos ? device : device.substr(slash + 1);
    return HalFile(full, name, exists(path));
  }
};

#define Storage HalStorage::getInstance()
