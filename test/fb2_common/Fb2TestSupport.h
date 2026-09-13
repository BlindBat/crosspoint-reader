#pragma once

// Small filesystem helpers shared by the FB2 host test suites.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef FB2_FIXTURE_DIR
#error "FB2_FIXTURE_DIR must be defined by the build system"
#endif

namespace fb2test {

inline std::string fixturePath(const std::string& name) { return std::string(FB2_FIXTURE_DIR) + "/" + name; }

inline std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

inline bool writeAll(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return out.good();
}

inline bool fileExists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

// RAII temp directory. Created under the system temp dir, removed recursively.
class TempDir {
 public:
  TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/tmp") + "/fb2test.XXXXXX";
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", tmpl.c_str());
    path_ = ::mkdtemp(buffer) ? buffer : "";
  }
  ~TempDir() {
    if (!path_.empty()) {
      const std::string cmd = "rm -rf '" + path_ + "'";
      [[maybe_unused]] const int rc = std::system(cmd.c_str());
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }
  bool valid() const { return !path_.empty(); }

 private:
  std::string path_;
};

}  // namespace fb2test
