#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Row model of the end-of-book suggestion menu: one row per suggested sibling
// book followed by a trailing Home row.
namespace EndOfBookRows {

// Display name without the file extension, mirroring the file browser rows.
inline std::string displayName(const std::string& filename) {
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

// Fills labels[0..maxRows) with the suggestion display names in order, then the
// Home row when there is room. Returns the number of rows written.
inline size_t buildLabels(const std::vector<std::string>& names, const char* homeLabel, std::string* labels,
                          const size_t maxRows) {
  size_t count = 0;
  for (const auto& name : names) {
    if (count >= maxRows) break;
    labels[count++] = displayName(name);
  }
  if (count < maxRows) {
    labels[count++] = homeLabel;
  }
  return count;
}

// Absolute path of a suggestion: the folder is either "/" or has no trailing slash.
inline std::string joinPath(const std::string& folder, const std::string& name) {
  return folder == "/" ? "/" + name : folder + "/" + name;
}

}  // namespace EndOfBookRows
