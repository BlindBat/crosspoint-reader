#pragma once

// Host stand-in; records clearCache() calls in BookStubLog.h.

#include <string>
#include <utility>

#include "BookStubLog.h"

class Epub {
 public:
  explicit Epub(std::string filepath, std::string cacheDir)
      : filepath(std::move(filepath)), cacheDir(std::move(cacheDir)) {}
  bool clearCache() const {
    bookStubCalls().push_back({"epub", filepath, cacheDir});
    return true;
  }

 private:
  std::string filepath;
  std::string cacheDir;
};
