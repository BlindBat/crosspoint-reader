#pragma once

// In-memory HalStorage/HalFile stand-in for the KOSync client host suite.
//
// Two surfaces are served:
//  - PersistableStore's whole-file JSON I/O (exists/mkdir/readFile/writeFile),
//    backed by a path -> contents map so no real SD or temp dir is touched.
//  - KOReaderDocumentId's seek/read sampling (openFileForRead/fileSize/
//    seekSet/read). Files are registered either as literal bytes or as a
//    (size, byte generator) pair, so the 2^30-byte tail of KOReader's offset
//    schedule can be exercised without materialising a gigabyte.
//
// Fault injection: seekSet() fails for offsets listed in failSeekOffsets;
// read() reports 0 bytes for chunks starting at offsets in emptyReadOffsets
// and -1 (SdFat's error return) for those in failReadOffsets.

#include <Arduino.h>
#include <Print.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  struct VirtualFile {
    size_t size = 0;
    std::string bytes;                        // literal contents when non-empty
    std::function<uint8_t(size_t)> byteAt;    // used when bytes is empty
    uint8_t at(const size_t i) const { return bytes.empty() ? byteAt(i) : static_cast<uint8_t>(bytes[i]); }
  };

  // --- test controls -------------------------------------------------------
  std::map<std::string, VirtualFile> files;
  std::map<std::string, std::string> jsonFiles;
  std::set<size_t> failSeekOffsets;
  std::set<size_t> emptyReadOffsets;
  std::set<size_t> failReadOffsets;
  int openCount = 0;
  // (offset, requested length) of every read() call, in order.
  std::vector<std::pair<size_t, size_t>> reads;

  void reset() {
    files.clear();
    jsonFiles.clear();
    failSeekOffsets.clear();
    emptyReadOffsets.clear();
    failReadOffsets.clear();
    reads.clear();
    openCount = 0;
  }

  void addFile(const std::string& path, std::string contents) {
    VirtualFile f;
    f.size = contents.size();
    f.bytes = std::move(contents);
    files[path] = std::move(f);
  }

  void addGeneratedFile(const std::string& path, const size_t size, std::function<uint8_t(size_t)> byteAt) {
    VirtualFile f;
    f.size = size;
    f.byteAt = std::move(byteAt);
    files[path] = std::move(f);
  }

  // --- PersistableStore surface -------------------------------------------
  bool exists(const char* path) { return jsonFiles.count(path) > 0; }
  bool mkdir(const char*, bool = true) { return true; }
  String readFile(const char* path) {
    const auto it = jsonFiles.find(path);
    return it == jsonFiles.end() ? String() : String(it->second);
  }
  bool writeFile(const char* path, const String& content) {
    jsonFiles[path] = content.str();
    return true;
  }
  bool remove(const char* path) { return jsonFiles.erase(path) > 0; }

  // --- KOReaderDocumentId surface -----------------------------------------
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const char* path, HalFile& file) {
    return openFileForRead(moduleName, std::string(path), file);
  }
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;

 public:
  HalFile() = default;
  ~HalFile() override = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  size_t fileSize() { return file_ ? file_->size : 0; }
  size_t size() { return fileSize(); }

  bool seekSet(const size_t offset) {
    if (!file_ || offset > file_->size) return false;
    if (Storage.failSeekOffsets.count(offset)) return false;
    pos_ = offset;
    return true;
  }

  int read(void* buf, const size_t count) {
    if (!file_) return -1;
    Storage.reads.emplace_back(pos_, count);
    if (Storage.emptyReadOffsets.count(pos_)) return 0;
    if (Storage.failReadOffsets.count(pos_)) return -1;
    const size_t n = std::min(count, file_->size - pos_);
    auto* out = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < n; i++) out[i] = file_->at(pos_ + i);
    pos_ += n;
    return static_cast<int>(n);
  }

  size_t write(uint8_t) override { return 0; }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  bool close() {
    file_ = nullptr;
    return true;
  }

 private:
  const HalStorage::VirtualFile* file_ = nullptr;
  size_t pos_ = 0;
};

inline bool HalStorage::openFileForRead(const char*, const std::string& path, HalFile& file) {
  const auto it = files.find(path);
  if (it == files.end()) return false;
  openCount++;
  file.file_ = &it->second;
  file.pos_ = 0;
  return true;
}
