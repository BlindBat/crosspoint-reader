#pragma once

// Shared helpers for the platform helper suite: a per-test temp directory
// that HalStorage remaps device paths into, and small byte builders.

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <HalStorage.h>
#include <HostControls.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Owns a fresh directory under PLATFORM_HELPERS_TMP_DIR and points Storage at
// it; removed (with contents) on destruction.
class ScopedStorageRoot {
 public:
  ScopedStorageRoot() {
    std::filesystem::create_directories(PLATFORM_HELPERS_TMP_DIR);
    std::string tmpl = std::string(PLATFORM_HELPERS_TMP_DIR) + "/case.XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* made = ::mkdtemp(buf.data());
    root = made ? made : tmpl;
    Storage.root = root;
    Storage.reset();
  }
  ~ScopedStorageRoot() {
    Storage.root.clear();
    Storage.reset();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  ScopedStorageRoot(const ScopedStorageRoot&) = delete;
  ScopedStorageRoot& operator=(const ScopedStorageRoot&) = delete;

  // Writes bytes to a device path (e.g. "/foo.bin"); returns the mapped host path.
  std::string put(const char* devicePath, const std::vector<uint8_t>& bytes) const {
    const std::string full = root + devicePath;
    std::filesystem::create_directories(std::filesystem::path(full).parent_path());
    std::ofstream out(full, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return full;
  }
  std::string put(const char* devicePath, const std::string& bytes) const {
    return put(devicePath, std::vector<uint8_t>(bytes.begin(), bytes.end()));
  }

  std::vector<uint8_t> get(const char* devicePath) const {
    std::ifstream in(root + devicePath, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  bool exists(const char* devicePath) const { return std::filesystem::exists(root + devicePath); }

  std::string root;
};

struct ByteWriter {
  std::vector<uint8_t> bytes;
  ByteWriter& u8(uint32_t v) {
    bytes.push_back(static_cast<uint8_t>(v));
    return *this;
  }
  ByteWriter& le16(uint32_t v) { return u8(v & 0xFF).u8((v >> 8) & 0xFF); }
  ByteWriter& le32(uint32_t v) { return le16(v & 0xFFFF).le16(v >> 16); }
  ByteWriter& be32(uint32_t v) { return u8(v >> 24).u8((v >> 16) & 0xFF).u8((v >> 8) & 0xFF).u8(v & 0xFF); }
  ByteWriter& raw(const std::string& s) {
    bytes.insert(bytes.end(), s.begin(), s.end());
    return *this;
  }
  ByteWriter& raw(const std::vector<uint8_t>& v) {
    bytes.insert(bytes.end(), v.begin(), v.end());
    return *this;
  }
  ByteWriter& fill(size_t n, uint8_t v) {
    bytes.insert(bytes.end(), n, v);
    return *this;
  }
};
