#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// JSON schema version of the fonts.json manifest. The canonical version for
// the build tooling lives in lib/EpdFont/scripts/cpfont_version.py. This
// firmware-side copy must be bumped manually when the firmware is updated to
// support a new manifest schema.
#define FONTS_MANIFEST_VERSION 1

// One downloadable .cpfont entry of a manifest family.
struct FontManifestFile {
  std::string name;
  size_t size = 0;
  uint32_t crc32 = 0;
};

struct FontManifestFamily {
  std::string name;
  std::string description;
  std::vector<std::string> styles;
  std::vector<FontManifestFile> files;
  size_t totalSize = 0;
  bool installed = false;   // filled in by the caller against the SD registry
  bool hasUpdate = false;   // filled in by the caller from on-disk file sizes
  uint32_t scriptMask = 0;  // bit i set when the family lists scriptGroups[i]'s tag
};

enum class FontManifestError : uint8_t {
  OK,
  UNSUPPORTED_VERSION,
  MALFORMED,
};

// Script groups beyond this are ignored: scriptMask is a 32-bit membership mask.
constexpr size_t FONT_MANIFEST_MAX_SCRIPT_GROUPS = 32;

// Reads a deserialized fonts.json into the caller's containers. On OK the
// containers hold the manifest; on any error their contents are unspecified.
FontManifestError parseFontManifest(JsonDocument& doc, std::string& baseUrl,
                                    std::vector<std::string>& scriptGroupLabels,
                                    std::vector<FontManifestFamily>& families);
