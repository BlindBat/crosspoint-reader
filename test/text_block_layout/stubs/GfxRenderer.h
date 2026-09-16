#pragma once

// Deterministic fixed-metric GfxRenderer stub for TextBlock/ParsedText layout
// tests. Metrics are per-CODEPOINT (not per-byte) so multi-byte UTF-8 text
// (CJK, soft hyphens, accented letters) measures the same as ASCII and
// expected line breaks stay hand-computable:
//   regular glyph 10 px, bold 12 px, italic 11 px, bold-italic 13 px
//   space / inter-word gap 5 px, kerning 0, line height 16 px.
// Style-dependent widths are deliberate: they let tests prove that mid-line
// bold/italic runs feed the line breaker different measurements.

#include <EpdFontFamily.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}  // namespace BidiUtils

// One recorded drawText call. Declared outside GfxRenderer so its default member
// initializers are complete where the stub declares the static recording buffer.
struct StubDrawCall {
  int x = 0;
  int y = 0;
  char text[64] = {};
  uint8_t style = 0;
  int baseDir = 0;
};

class GfxRenderer {
 public:
  static constexpr int kRegularAdvance = 10;
  static constexpr int kBoldAdvance = 12;
  static constexpr int kItalicAdvance = 11;
  static constexpr int kBoldItalicAdvance = 13;
  static constexpr int kSpaceWidth = 5;
  static constexpr int kLineHeight = 16;
  static constexpr int kAscender = 12;

  static int advanceFor(const EpdFontFamily::Style style) {
    // Only the two font-variant bits affect metrics; decoration bits (underline,
    // sup, ruby-continue, ...) do not, matching how real fonts are selected.
    switch (static_cast<uint8_t>(style) & 0x03) {
      case EpdFontFamily::BOLD:
        return kBoldAdvance;
      case EpdFontFamily::ITALIC:
        return kItalicAdvance;
      case EpdFontFamily::BOLD_ITALIC:
        return kBoldItalicAdvance;
      default:
        return kRegularAdvance;
    }
  }

  static int codepointCount(const char* text) {
    int count = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
      if ((*p & 0xC0) != 0x80) count++;  // count UTF-8 lead bytes only
    }
    return count;
  }

  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float = 1.0f) const { return kLineHeight; }
  int getFontAscenderSize(int) const { return kAscender; }
  int getSpaceWidth(int, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return kSpaceWidth; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return codepointCount(text) * advanceFor(style);
  }
  int getTextWidth(int, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    return codepointCount(text) * advanceFor(style);
  }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return kSpaceWidth;
  }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const {}
  bool isFontCacheScanning() const { return false; }

  // Draw recording. TextBlock::render is a hot path guarded by allocation counts, so the
  // recorder stores into fixed static storage and never touches the heap: a test may open
  // an alloc_counter::CountingScope around render() and still read back what was drawn.
  using DrawCall = StubDrawCall;
  static constexpr size_t kMaxDrawCalls = 128;
  static inline DrawCall drawCalls[kMaxDrawCalls] = {};
  static inline size_t drawCallCount = 0;

  static void resetDrawCalls() { drawCallCount = 0; }

  void drawText(int, const int x, const int y, const char* text, bool = true,
                const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                const BidiUtils::BidiBaseDir baseDir = BidiUtils::BidiBaseDir::AUTO) const {
    if (drawCallCount < kMaxDrawCalls) {
      DrawCall& call = drawCalls[drawCallCount];
      call.x = x;
      call.y = y;
      call.style = static_cast<uint8_t>(style);
      call.baseDir = static_cast<int>(baseDir);
      size_t n = 0;
      while (text != nullptr && text[n] != '\0' && n + 1 < sizeof(call.text)) {
        call.text[n] = text[n];
        ++n;
      }
      call.text[n] = '\0';
    }
    ++drawCallCount;
  }
  void drawLine(int, int, int, int, bool = true) const {}
  void drawLine(int, int, int, int, int, bool) const {}
};
