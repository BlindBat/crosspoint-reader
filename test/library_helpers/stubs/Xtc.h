#pragma once

// Host stand-in; records clearCache() calls in BookStubLog.h.

#include <string>
#include <utility>

#include "BookStubLog.h"

class Xtc {
 public:
  explicit Xtc(std::string filepath, std::string cacheDir)
      : filepath(std::move(filepath)), cacheDir(std::move(cacheDir)) {}
  bool clearCache() const {
    bookStubCalls().push_back({"xtc", filepath, cacheDir});
    return true;
  }

 private:
  std::string filepath;
  std::string cacheDir;
};
