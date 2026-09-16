#pragma once

// POSIX-backed HalStorage stub for the EPUB orchestration suite.
//
// Semantics mirror the firmware HAL the production code was written against:
//   - openFileForWrite maps to O_RDWR|O_CREAT|O_TRUNC ("w+b")
//   - read(void*, size_t) returns an int byte count (negative when closed)
//   - available() reports bytes remaining, clamped to 0 past EOF
//   - removeDir() recurses and fails on a missing path or a plain file
// HalFile derives from Print so it can be handed to APIs that stream bytes.
//
// Tests drive the storage failure paths through failOpenForWrite/failRename,
// which match on a path suffix.

#include <Print.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() override { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept : file_(other.file_) { other.file_ = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      other.file_ = nullptr;
    }
    return *this;
  }

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

  int read() {
    if (!file_) return -1;
    return std::fgetc(file_);
  }

  size_t write(const void* buffer, size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }
  size_t write(const uint8_t* buffer, size_t count) override { return write(static_cast<const void*>(buffer), count); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }

  void flush() {
    if (file_) std::fflush(file_);
  }

  bool seek(size_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seek64(uint64_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }

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

  // --- test controls -------------------------------------------------------
  // Any openFileForWrite()/rename() whose target path ends with one of these
  // suffixes fails, reproducing a full or flaky SD card.
  std::vector<std::string> failOpenForWrite;
  std::vector<std::string> failRename;
  // Every path handed to openFileForWrite(), so a test can count SD writes
  // (e.g. how many stylesheets survived CSS deduplication).
  std::vector<std::string> writeOpens;

  void resetForTest() {
    failOpenForWrite.clear();
    failRename.clear();
    writeOpens.clear();
  }

  size_t countWriteOpensEndingWith(const std::string& suffix) const {
    size_t count = 0;
    for (const auto& path : writeOpens) {
      if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        count++;
      }
    }
    return count;
  }

  // --- API surface used by the compiled production code --------------------
  bool exists(const char* path) const {
    struct stat st{};
    return ::stat(path, &st) == 0;
  }

  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string full = path;
    if (pFlag) {
      for (size_t pos = full.find('/', 1); pos != std::string::npos; pos = full.find('/', pos + 1)) {
        ::mkdir(full.substr(0, pos).c_str(), 0755);
      }
    }
    return ::mkdir(full.c_str(), 0755) == 0 || exists(path);
  }

  bool remove(const char* path) { return std::remove(path) == 0; }
  bool remove(const std::string& path) { return remove(path.c_str()); }

  bool rename(const char* from, const char* to) {
    if (matches(failRename, to)) return false;
    return std::rename(from, to) == 0;
  }

  bool removeDir(const char* path) {
    struct stat st{};
    if (::stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    DIR* dir = ::opendir(path);
    if (!dir) return false;
    bool ok = true;
    while (const dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      const std::string child = std::string(path) + "/" + name;
      struct stat childStat{};
      if (::stat(child.c_str(), &childStat) != 0) {
        ok = false;
      } else if (S_ISDIR(childStat.st_mode)) {
        ok = removeDir(child.c_str()) && ok;
      } else {
        ok = std::remove(child.c_str()) == 0 && ok;
      }
    }
    ::closedir(dir);
    return ::rmdir(path) == 0 && ok;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char* m, const std::string& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    writeOpens.emplace_back(path);
    if (matches(failOpenForWrite, path)) return false;
    return file.open(path, "w+b");
  }
  bool openFileForWrite(const char* m, const std::string& path, HalFile& file) {
    return openFileForWrite(m, path.c_str(), file);
  }

 private:
  static bool matches(const std::vector<std::string>& suffixes, const std::string& path) {
    for (const auto& suffix : suffixes) {
      if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return true;
      }
    }
    return false;
  }
};

#define Storage HalStorage::getInstance()
