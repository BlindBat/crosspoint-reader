#pragma once

#include <string>

// Host test stub: the production FsHelpers pulls in the Arduino String type and
// URL-decoding logic that BookMetadataCache does not need for these tests. The
// suite's spine hrefs already match the ZIP entry names verbatim, so identity
// transforms are sufficient.
namespace FsHelpers {
inline std::string decodeUriEscapes(const std::string& path) { return path; }
inline std::string normalisePath(const std::string& path) { return path; }
}  // namespace FsHelpers
