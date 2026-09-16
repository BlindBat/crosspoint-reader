#include "FontManifest.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

// Reads a manifest string field, refusing anything longer than maxLength: the
// manifest comes from the network and the parsed model lives in the device heap.
std::string boundedManifestField(JsonObjectConst obj, const char* key, const size_t maxLength) {
  const char* value = obj[key] | static_cast<const char*>(nullptr);
  if (value == nullptr) {
    return {};
  }
  if (strnlen(value, maxLength + 1) > maxLength) {
    LOG_ERR("FONT", "Manifest field '%s' exceeds %zu bytes; ignored", key, maxLength);
    return {};
  }
  return std::string(value);
}

}  // namespace

FontManifestError parseFontManifest(JsonDocument& doc, std::string& baseUrl,
                                    std::vector<std::string>& scriptGroupLabels,
                                    std::vector<FontManifestFamily>& families) {
  const int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    return FontManifestError::UNSUPPORTED_VERSION;
  }

  baseUrl = doc["baseUrl"] | "";
  families.clear();
  scriptGroupLabels.clear();

  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  const size_t groupCount = std::min(groupsArr.size(), FONT_MANIFEST_MAX_SCRIPT_GROUPS);
  scriptGroupLabels.reserve(groupCount);
  if (groupsArr.size() > FONT_MANIFEST_MAX_SCRIPT_GROUPS) {
    LOG_ERR("FONT", "Manifest declares more than %zu script groups; extra groups ignored",
            FONT_MANIFEST_MAX_SCRIPT_GROUPS);
  }
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
    const char* tag = groupObj["tag"] | "";
    const char* label = groupObj["label"] | "";
    if (*tag == '\0' || *label == '\0') {
      LOG_ERR("FONT", "Malformed script group at index %zu", groupIndex);
      return FontManifestError::MALFORMED;
    }
    scriptGroupLabels.push_back(label);
  }

  JsonArray familiesArr = doc["families"].as<JsonArray>();
  const size_t familyCount = std::min(familiesArr.size(), FONT_MANIFEST_MAX_FAMILIES);
  if (familiesArr.size() > FONT_MANIFEST_MAX_FAMILIES) {
    LOG_ERR("FONT", "Manifest lists %zu families; keeping the first %zu", familiesArr.size(),
            FONT_MANIFEST_MAX_FAMILIES);
  }
  families.reserve(familyCount);

  for (JsonObject fObj : familiesArr) {
    if (families.size() >= FONT_MANIFEST_MAX_FAMILIES) break;
    FontManifestFamily family;
    family.name = boundedManifestField(fObj, "name", FONT_MANIFEST_MAX_NAME_BYTES);
    family.description = boundedManifestField(fObj, "description", FONT_MANIFEST_MAX_DESCRIPTION_BYTES);

    for (JsonVariant s : fObj["styles"].as<JsonArray>()) {
      if (family.styles.size() >= FONT_MANIFEST_MAX_STYLES_PER_FAMILY) break;
      const char* style = s.as<const char*>();
      if (style == nullptr || strnlen(style, FONT_MANIFEST_MAX_NAME_BYTES + 1) > FONT_MANIFEST_MAX_NAME_BYTES) {
        continue;
      }
      family.styles.emplace_back(style);
    }

    for (JsonVariant script : fObj["scripts"].as<JsonArray>()) {
      const char* familyTag = script.as<const char*>();
      if (!familyTag) continue;
      for (size_t groupIndex = 0; groupIndex < scriptGroupLabels.size(); groupIndex++) {
        JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
        const char* groupTag = groupObj["tag"] | "";
        if (std::strcmp(familyTag, groupTag) == 0) {
          family.scriptMask |= uint32_t{1} << groupIndex;
          break;
        }
      }
    }

    family.totalSize = 0;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      if (family.files.size() >= FONT_MANIFEST_MAX_FILES_PER_FAMILY) {
        LOG_ERR("FONT", "Family '%s' lists more than %zu files; the rest are ignored", family.name.c_str(),
                FONT_MANIFEST_MAX_FILES_PER_FAMILY);
        break;
      }
      FontManifestFile file;
      file.name = boundedManifestField(fileObj, "name", FONT_MANIFEST_MAX_NAME_BYTES);
      file.size = fileObj["size"] | 0;

      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", file.name.c_str());
        return FontManifestError::MALFORMED;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    families.push_back(std::move(family));
  }

  return FontManifestError::OK;
}
