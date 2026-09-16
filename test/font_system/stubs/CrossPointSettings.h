#pragma once

// Host stand-in for src/CrossPointSettings.h scoped to the fields the SD font
// system and FontInstaller read and write. Persistence is counted, not
// performed, so tests can pin the write-throttling guards.

#include <cstdint>
#include <cstring>

class CrossPointSettings {
 public:
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);

  uint8_t fontPointSize = 14;
  char sdFontFamilyName[32] = "";
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  int saveCount = 0;
  int clearSdFontCalls = 0;

  bool saveToFile() {
    saveCount++;
    return true;
  }
  // Mirrors the real implementation's one SD write so saveCount stays a faithful
  // model of the persist traffic. The real version also snaps fontPointSize back
  // into the built-in set; that belongs to CrossPointSettings' own tests.
  void clearSdFontFamily() {
    sdFontFamilyName[0] = '\0';
    clearSdFontCalls++;
    saveToFile();
  }
  void setSdFontFamily(const char* name) {
    std::strncpy(sdFontFamilyName, name, sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  }
  void reset() { *this = CrossPointSettings{}; }

  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }
};

#define SETTINGS CrossPointSettings::getInstance()
