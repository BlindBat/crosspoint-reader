#pragma once

#include <FsHelpers.h>

#include <string>

#include "components/themes/BaseTheme.h"

// Renderer-free helpers behind UITheme's static cover-path and file-icon lookups.
namespace UIThemeUtils {

// Substitutes the "[HEIGHT]" placeholder in a cover thumbnail path template.
inline std::string getCoverThumbPath(std::string coverBmpPath, const int coverHeight) {
  const size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

// List icon for a browser entry; directories carry a trailing '/'.
inline UIIcon getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasFb2Extension(filename) ||
      FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

}  // namespace UIThemeUtils
