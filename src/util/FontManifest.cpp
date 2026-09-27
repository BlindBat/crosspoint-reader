#include "FontManifest.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace {

// A manifest string field, or "" when it is absent or longer than maxLength: the
// manifest comes from the network and the parsed model lives in the device heap.
const char* boundedManifestField(JsonObjectConst obj, const char* key, const size_t maxLength) {
  const char* value = obj[key] | static_cast<const char*>(nullptr);
  if (value == nullptr) return "";
  if (strnlen(value, maxLength + 1) > maxLength) {
    LOG_ERR("FONT", "Manifest field '%s' exceeds %zu bytes; ignored", key, maxLength);
    return "";
  }
  return value;
}

size_t internedBytes(const char* text) { return *text == '\0' ? 0 : std::strlen(text) + 1; }

}  // namespace

bool FontManifestArena::intern(const char* text, FontManifestStrRef& outRef) {
  if (text == nullptr || *text == '\0') {
    outRef = 0;
    return true;
  }
  const size_t length = std::strlen(text) + 1;
  if (used + length > capacity) {
    LOG_ERR("FONT", "Manifest string arena overflow at %u/%u bytes", used, capacity);
    return false;
  }
  outRef = used;
  std::memcpy(strings.get() + used, text, length);
  used = static_cast<uint32_t>(used + length);
  return true;
}

void FontManifestArena::clear() {
  // Swap rather than clear: clear() keeps the capacity, and this runs to hand
  // the heap back while the error screen is up. Reverse allocation order.
  std::vector<FontManifestFamily>().swap(families);
  std::vector<FontManifestStrRef>().swap(scriptGroupLabels);
  files.reset();
  fileCount = 0;
  strings.reset();
  used = 0;
  capacity = 0;
}

FontManifestError parseFontManifest(JsonDocument& doc, std::string& baseUrl, FontManifestArena& out) {
  out.clear();
  const int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    return FontManifestError::UNSUPPORTED_VERSION;
  }

  baseUrl = doc["baseUrl"] | "";

  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  JsonArray familiesArr = doc["families"].as<JsonArray>();
  const size_t groupCount = std::min(groupsArr.size(), FONT_MANIFEST_MAX_SCRIPT_GROUPS);
  const size_t familyCount = std::min(familiesArr.size(), FONT_MANIFEST_MAX_FAMILIES);
  if (groupsArr.size() > FONT_MANIFEST_MAX_SCRIPT_GROUPS) {
    LOG_ERR("FONT", "Manifest declares more than %zu script groups; extra groups ignored",
            FONT_MANIFEST_MAX_SCRIPT_GROUPS);
  }
  if (familiesArr.size() > FONT_MANIFEST_MAX_FAMILIES) {
    LOG_ERR("FONT", "Manifest lists %zu families; keeping the first %zu", familiesArr.size(),
            FONT_MANIFEST_MAX_FAMILIES);
  }

  // Size the arena and the file table in one pass, under the same caps the
  // build below applies, so neither reallocates while the catalog is built: a
  // mid-build growth would both fragment the heap and invalidate refs already
  // handed out.
  size_t arenaBytes = 1;  // leading terminator makes offset 0 the empty string
  size_t fileTotal = 0;
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    arenaBytes += internedBytes(groupsArr[groupIndex]["label"] | "");
  }
  size_t familyIndex = 0;
  for (JsonObject fObj : familiesArr) {
    if (familyIndex++ >= familyCount) break;
    arenaBytes += internedBytes(boundedManifestField(fObj, "name", FONT_MANIFEST_MAX_NAME_BYTES));
    arenaBytes += internedBytes(boundedManifestField(fObj, "description", FONT_MANIFEST_MAX_DESCRIPTION_BYTES));
    size_t fileIndex = 0;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      if (fileIndex++ >= FONT_MANIFEST_MAX_FILES_PER_FAMILY) break;
      arenaBytes += internedBytes(boundedManifestField(fileObj, "name", FONT_MANIFEST_MAX_NAME_BYTES));
      fileTotal++;
    }
  }
  out.strings = makeUniqueNoThrow<char[]>(arenaBytes);
  if (!out.strings) {
    LOG_ERR("FONT", "OOM: %zu byte string arena", arenaBytes);
    return FontManifestError::OUT_OF_MEMORY;
  }
  out.strings[0] = '\0';
  out.used = 1;
  out.capacity = static_cast<uint32_t>(arenaBytes);
  if (fileTotal > 0) {
    out.files = makeUniqueNoThrow<FontManifestFile[]>(fileTotal);
    if (!out.files) {
      LOG_ERR("FONT", "OOM: %zu manifest file entries", fileTotal);
      out.clear();
      return FontManifestError::OUT_OF_MEMORY;
    }
  }

  out.scriptGroupLabels.reserve(groupCount);
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
    const char* tag = groupObj["tag"] | "";
    const char* label = groupObj["label"] | "";
    if (*tag == '\0' || *label == '\0') {
      LOG_ERR("FONT", "Malformed script group at index %zu", groupIndex);
      out.clear();
      return FontManifestError::MALFORMED;
    }
    FontManifestStrRef labelRef = 0;
    if (!out.intern(label, labelRef)) {
      out.clear();
      return FontManifestError::MALFORMED;
    }
    out.scriptGroupLabels.push_back(labelRef);
  }

  out.families.reserve(familyCount);
  for (JsonObject fObj : familiesArr) {
    if (out.families.size() >= familyCount) break;
    FontManifestFamily family;
    if (!out.intern(boundedManifestField(fObj, "name", FONT_MANIFEST_MAX_NAME_BYTES), family.name) ||
        !out.intern(boundedManifestField(fObj, "description", FONT_MANIFEST_MAX_DESCRIPTION_BYTES),
                    family.description)) {
      out.clear();
      return FontManifestError::MALFORMED;
    }

    for (JsonVariant script : fObj["scripts"].as<JsonArray>()) {
      const char* familyTag = script.as<const char*>();
      if (!familyTag) continue;
      for (size_t groupIndex = 0; groupIndex < out.scriptGroupLabels.size(); groupIndex++) {
        JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
        const char* groupTag = groupObj["tag"] | "";
        if (std::strcmp(familyTag, groupTag) == 0) {
          family.scriptMask |= uint32_t{1} << groupIndex;
          break;
        }
      }
    }

    family.fileStart = out.fileCount;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      if (family.fileCount >= FONT_MANIFEST_MAX_FILES_PER_FAMILY) {
        LOG_ERR("FONT", "Family '%s' lists more than %zu files; the rest are ignored", out.str(family.name),
                FONT_MANIFEST_MAX_FILES_PER_FAMILY);
        break;
      }
      FontManifestFile file;
      if (!out.intern(boundedManifestField(fileObj, "name", FONT_MANIFEST_MAX_NAME_BYTES), file.name)) {
        out.clear();
        return FontManifestError::MALFORMED;
      }
      file.size = fileObj["size"] | 0u;
      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", out.str(file.name));
        out.clear();
        return FontManifestError::MALFORMED;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();
      family.totalSize += file.size;
      out.files[out.fileCount++] = file;
      family.fileCount++;
    }

    out.families.push_back(family);
  }

  return FontManifestError::OK;
}
