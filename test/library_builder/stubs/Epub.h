#pragma once

#include <map>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  bool success = true;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

// One extraction, counted, answered from bookMetadata by path. Shared by the
// Epub and Fb2 stubs so the builder's per-format branch is the only difference.
inline bool fakeLoadMetadata(const std::string& path, std::string& title, std::string& author) {
  ++fake::parses;
  const auto& metadata = bookMetadata[path];
  if (!metadata.success) return false;
  title = metadata.title;
  author = metadata.author;
  return true;
}

class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) { return fakeLoadMetadata(path, title, author); }
};
