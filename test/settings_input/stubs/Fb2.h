#pragma once

// Host stand-in; see Epub.h.

#include <string>

class Fb2 {
 public:
  Fb2(const std::string& path, const std::string&) : path_(path) {}
  bool load(bool = true) { return false; }
  std::string getTitle() const { return ""; }
  std::string getAuthor() const { return ""; }
  std::string getThumbBmpPath() const { return ""; }

 private:
  std::string path_;
};
