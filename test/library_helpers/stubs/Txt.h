#pragma once

// Host stand-in; records clearCache() calls in BookStubLog.h.

#include <string>
#include <utility>

#include "BookStubLog.h"

class Txt {
 public:
  explicit Txt(std::string filepath, std::string cacheDir)
      : filepath(std::move(filepath)), cacheDir(std::move(cacheDir)) {}
  bool clearCache() const {
    bookStubCalls().push_back({"txt", filepath, cacheDir});
    return true;
  }

 private:
  std::string filepath;
  std::string cacheDir;
};
