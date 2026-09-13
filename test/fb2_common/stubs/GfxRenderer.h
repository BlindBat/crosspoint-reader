#pragma once

// Fixed-metric GfxRenderer stub for FB2 layout tests: every glyph advances
// 8 px, spaces 4 px, lines are 16 px tall. Deterministic layout lets tests
// compute expected line/page breaks by hand. Draw calls are no-ops.

#include <EpdFontFamily.h>

#include <cstdint>
#include <deque>
#include <string>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

class GfxRenderer {
 public:
  static constexpr int kGlyphAdvance = 8;
  static constexpr int kSpaceWidth = 4;
  static constexpr int kLineHeight = 16;
  static constexpr int kAscender = 12;

  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float = 1.0f) const { return kLineHeight; }
  int getFontAscenderSize(int) const { return kAscender; }
  int getSpaceWidth(int, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return kSpaceWidth; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    int width = 0;
    while (*text++) width += kGlyphAdvance;
    return width;
  }
  int getTextWidth(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    int width = 0;
    while (*text++) width += kGlyphAdvance;
    return width;
  }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return kSpaceWidth;
  }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const {}
  bool isFontCacheScanning() const { return false; }
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {}
  void drawLine(int, int, int, int, bool = true) const {}
  void drawLine(int, int, int, int, int, bool) const {}
};
