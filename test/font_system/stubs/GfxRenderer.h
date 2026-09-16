#pragma once

// Host stub of lib/GfxRenderer/GfxRenderer.h scoped to the registration
// surface SdCardFontManager and SdCardFontSystem drive: the font map, the SD
// font table, and the CJK UI fallback map. Every mutation is observable so the
// tests can pin what the font system registers and tears down.

#include <EpdFontFamily.h>

#include <map>

class SdCardFont;

class GfxRenderer {
 public:
  void insertFont(int fontId, EpdFontFamily font) {
    if (!fontMap_.insert({fontId, font}).second) duplicateInserts++;
  }
  void removeFont(int fontId) {
    fontMap_.erase(fontId);
    sdCardFonts_.erase(fontId);
    removeCalls++;
  }
  const std::map<int, EpdFontFamily>& getFontMap() const { return fontMap_; }
  void registerSdCardFont(int fontId, SdCardFont* font) { sdCardFonts_[fontId] = font; }
  void unregisterSdCardFont(int fontId) { removeFont(fontId); }
  void clearSdCardFonts() { sdCardFonts_.clear(); }
  const std::map<int, SdCardFont*>& getSdCardFonts() const { return sdCardFonts_; }
  bool isSdCardFont(int fontId) const { return sdCardFonts_.count(fontId) > 0; }
  void setFallbackFont(int primaryFontId, int fallbackFontId) { fallbackFontMap_[primaryFontId] = fallbackFontId; }
  void clearFallbackFonts() {
    fallbackFontMap_.clear();
    clearFallbackCalls++;
  }
  const std::map<int, int>& fallbacks() const { return fallbackFontMap_; }

  int duplicateInserts = 0;
  int removeCalls = 0;
  int clearFallbackCalls = 0;

 private:
  std::map<int, EpdFontFamily> fontMap_;
  std::map<int, SdCardFont*> sdCardFonts_;
  std::map<int, int> fallbackFontMap_;
};
