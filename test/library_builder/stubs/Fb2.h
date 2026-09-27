#pragma once

#include <string>

#include "Epub.h"

class Fb2 {
  std::string path;

 public:
  Fb2(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) { return fakeLoadMetadata(path, title, author); }
};
