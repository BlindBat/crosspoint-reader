#pragma once

// Host stand-in for the extension helpers RecentBooksStore::getDataFromBook()
// dispatches on. Simple lowercase suffix checks mirror the production intent
// closely enough for the store-level tests (which do not pin FsHelpers
// behavior — that has its own coverage elsewhere).

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace FsHelpers {

inline bool hasSuffixCi(const std::string_view name, const std::string_view suffix) {
  if (name.size() < suffix.size()) return false;
  const std::string_view tail = name.substr(name.size() - suffix.size());
  return std::equal(tail.begin(), tail.end(), suffix.begin(), [](const char a, const char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  });
}

inline bool hasEpubExtension(const std::string_view name) { return hasSuffixCi(name, ".epub"); }
inline bool hasFb2Extension(const std::string_view name) {
  return hasSuffixCi(name, ".fb2") || hasSuffixCi(name, ".fb2.zip");
}
inline bool hasXtcExtension(const std::string_view name) { return hasSuffixCi(name, ".xtc"); }
inline bool hasTxtExtension(const std::string_view name) { return hasSuffixCi(name, ".txt"); }
inline bool hasMarkdownExtension(const std::string_view name) { return hasSuffixCi(name, ".md"); }

}  // namespace FsHelpers
