#pragma once

// POSIX-backed HalStorage stub for the PersistableStore host suite.
//
// The production stores address absolute SD paths like
// "/.crosspoint/settings.json"; the stub remaps every path under a
// test-controlled root directory so the suite can run anywhere. It also
// counts writeFile() calls so tests can pin the redundant-write guards
// (AGENTS.md "SD Persistence Throttling").

#include <Arduino.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

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
  bool failNextWrite = false;

  void resetCounters() {
    writeCount = 0;
    writtenPaths.clear();
    failNextWrite = false;
  }

  std::string mapPath(const char* path) const { return root + path; }

  // --- API surface used by the compiled production code --------------------
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

  bool remove(const char* path) { return ::remove(mapPath(path).c_str()) == 0; }
};

#define Storage HalStorage::getInstance()
