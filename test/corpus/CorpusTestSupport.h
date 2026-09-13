#pragma once

// Helpers shared by the corpus-driven parser tests. Each suite instantiates a
// value-parameterized test over listCorpusFiles(<dir>), so dropping a new file
// into test/corpus/<kind>/ automatically runs it through the universal
// invariants of every consuming suite.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace corpus {

// Sorted list of regular-file names in `directory`. Sorted so gtest test
// enumeration is deterministic across platforms.
inline std::vector<std::string> listCorpusFiles(const std::string& directory) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file()) {
      names.push_back(entry.path().filename().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

// Whole file as raw bytes (may legitimately contain NUL and invalid UTF-8).
inline std::string readCorpusFile(const std::string& directory, const std::string& name) {
  std::ifstream input(std::filesystem::path(directory) / name, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// gtest parameter-name generator: corpus filename with every character outside
// [A-Za-z0-9_] replaced by '_' (gtest only accepts alphanumerics/underscore).
struct NameFromFilename {
  template <typename ParamType>
  std::string operator()(const ::testing::TestParamInfo<ParamType>& info) const {
    std::string name = info.param;
    for (char& c : name) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      if (!ok) c = '_';
    }
    return name;
  }
};

}  // namespace corpus
