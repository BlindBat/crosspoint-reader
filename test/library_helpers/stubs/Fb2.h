#pragma once

// Host stand-in; records clearCache() calls in BookStubLog.h.

#include <string>
#include <utility>

#include "BookStubLog.h"

class Fb2 {
 public:
  explicit Fb2(std::string filepath, std::string cacheDir)
      : filepath(std::move(filepath)), cacheDir(std::move(cacheDir)) {}
  bool clearCache() const {
    bookStubCalls().push_back({"fb2", filepath, cacheDir});
    return true;
  }

 private:
  std::string filepath;
  std::string cacheDir;
};
