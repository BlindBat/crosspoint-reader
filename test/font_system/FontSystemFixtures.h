#pragma once

// Shared helpers for the font-system suite: a per-test SD sandbox that the
// HalStorage stub remaps onto, .cpfont installation into either fonts root,
// and small UTF-8 builders for the prewarm tests.
//
// Paths passed to production code are device-style absolute ("/.fonts/..."),
// and halstub::root rewrites them into FONT_SYSTEM_SANDBOX_DIR. Helpers here
// talk to the host filesystem directly so a test never validates the stub
// against itself.

#include <HalStorage.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fontfx {

inline constexpr const char* HIDDEN_ROOT = "/.fonts";
inline constexpr const char* VISIBLE_ROOT = "/fonts";

// ctest runs each discovered test as its own process, in parallel, so the
// sandbox is per-process: a shared one would have concurrent tests deleting
// each other's files in resetSandbox().
inline std::string sandbox() {
  static const std::string dir = std::string(FONT_SYSTEM_SANDBOX_DIR) + "/p" + std::to_string(::getpid());
  return dir;
}
inline std::string resource(const std::string& name) { return std::string(FONT_SYSTEM_RESOURCES_DIR "/") + name; }

// Host path for a device path inside the sandbox.
inline std::string hostPath(const std::string& devicePath) { return sandbox() + devicePath; }

// Wipe and recreate the sandbox, and point the HalStorage stub at it.
inline void resetSandbox() {
  std::error_code ec;
  std::filesystem::remove_all(sandbox(), ec);
  std::filesystem::create_directories(sandbox(), ec);
  halstub::root = sandbox();
}

inline void teardownSandbox() {
  std::error_code ec;
  std::filesystem::remove_all(sandbox(), ec);
  halstub::root.clear();
}

inline std::vector<uint8_t> readFile(const std::string& hostFile) {
  std::ifstream in(hostFile, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline void writeFile(const std::string& devicePath, const std::vector<uint8_t>& bytes) {
  const std::string full = hostPath(devicePath);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Install <fixture> as "<root>/<family>/<family>_<pointSize>.cpfont" and
// return that device path.
inline std::string installFont(const std::string& family, uint8_t pointSize, const std::string& fixture,
                               const char* root = HIDDEN_ROOT) {
  const std::string devicePath =
      std::string(root) + "/" + family + "/" + family + "_" + std::to_string(pointSize) + ".cpfont";
  writeFile(devicePath, readFile(resource(fixture)));
  return devicePath;
}

// Create "<root>/<family>/" with no font files in it.
inline void makeEmptyFamilyDir(const std::string& family, const char* root = HIDDEN_ROOT) {
  std::error_code ec;
  std::filesystem::create_directories(hostPath(std::string(root) + "/" + family), ec);
}

inline void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline std::string utf8Of(const std::vector<uint32_t>& codepoints) {
  std::string out;
  out.reserve(codepoints.size() * 3);
  for (const uint32_t cp : codepoints) appendUtf8(out, cp);
  return out;
}

}  // namespace fontfx
