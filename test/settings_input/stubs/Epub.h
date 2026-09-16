#pragma once

// Host stand-in: RecentBooksStore::getDataFromBook() constructs an Epub for
// metadata lookup. The persistence tests never depend on real EPUB parsing.

#include <string>

class Epub {
 public:
  Epub(const std::string& path, const std::string&) : path_(path) {}
  bool load(bool = true, bool = false) { return false; }
  std::string getTitle() const { return ""; }
  std::string getAuthor() const { return ""; }
  std::string getThumbBmpPath() const { return ""; }

 private:
  std::string path_;
};
