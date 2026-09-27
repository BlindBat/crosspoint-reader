#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// JSON schema version of the fonts.json manifest. The canonical version for
// the build tooling lives in lib/EpdFont/scripts/cpfont_version.py. This
// firmware-side copy must be bumped manually when the firmware is updated to
// support a new manifest schema.
#define FONTS_MANIFEST_VERSION 1

// Byte offset into FontManifestArena::strings; 0 is the empty string.
using FontManifestStrRef = uint32_t;

// One downloadable .cpfont entry of a manifest family.
struct FontManifestFile {
  FontManifestStrRef name = 0;
  uint32_t size = 0;
  uint32_t crc32 = 0;
};

struct FontManifestFamily {
  FontManifestStrRef name = 0;
  FontManifestStrRef description = 0;
  // Range into FontManifestArena::files, which holds every family's files back to back.
  uint32_t fileStart = 0;
  uint32_t fileCount = 0;
  uint32_t totalSize = 0;
  uint32_t scriptMask = 0;  // bit i set when the family lists scriptGroups[i]'s tag
  bool installed = false;   // filled in by the caller against the SD registry
  bool hasUpdate = false;   // filled in by the caller from on-disk file sizes
};

// The parsed manifest. Every string lives in one arena sized before anything is
// interned, so the catalog costs three allocations (arena, file table, family
// vector) instead of one per string, and nothing reallocates while it is built.
struct FontManifestArena {
  std::unique_ptr<char[]> strings;  // null-terminated, packed back to back
  uint32_t used = 0;
  uint32_t capacity = 0;
  std::vector<FontManifestFamily> families;
  std::unique_ptr<FontManifestFile[]> files;
  uint32_t fileCount = 0;
  std::vector<FontManifestStrRef> scriptGroupLabels;

  // cppcheck-suppress arithOperationsOnVoidPointer // unique_ptr<char[]>::get() is char*, not void*
  const char* str(FontManifestStrRef ref) const { return strings ? strings.get() + ref : ""; }
  // False if the string does not fit the arena reserved for the manifest.
  bool intern(const char* text, FontManifestStrRef& outRef);
  // Hands every allocation back, in reverse order of acquisition.
  void clear();
};

enum class FontManifestError : uint8_t {
  OK,
  UNSUPPORTED_VERSION,
  MALFORMED,
  OUT_OF_MEMORY,
};

// Script groups beyond this are ignored: scriptMask is a 32-bit membership mask.
constexpr size_t FONT_MANIFEST_MAX_SCRIPT_GROUPS = 32;

// The manifest is fetched over the network, so every repeated element needs a
// ceiling: the device holds the parsed model in its 380 KB heap.
constexpr size_t FONT_MANIFEST_MAX_FAMILIES = 128;
constexpr size_t FONT_MANIFEST_MAX_FILES_PER_FAMILY = 32;
constexpr size_t FONT_MANIFEST_MAX_NAME_BYTES = 64;
constexpr size_t FONT_MANIFEST_MAX_DESCRIPTION_BYTES = 256;

// Reads a deserialized fonts.json into `out`, which is cleared first. On OK the
// arena holds the manifest; on any error it is left cleared.
FontManifestError parseFontManifest(JsonDocument& doc, std::string& baseUrl, FontManifestArena& out);
