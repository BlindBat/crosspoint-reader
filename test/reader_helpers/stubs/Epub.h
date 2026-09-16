#pragma once

// Host stand-in: EpubReaderUtils::saveProgress only asks the book for its cache directory.

#include <string>

class Epub {
 public:
  explicit Epub(std::string cachePath) : cachePath_(std::move(cachePath)) {}
  const std::string& getCachePath() const { return cachePath_; }

 private:
  std::string cachePath_;
};
