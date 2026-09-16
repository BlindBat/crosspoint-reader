#pragma once

// Synthetic fonts for the renderer suite.
//
// Hand-built EpdFontData so every metric the tests assert on is known exactly:
// glyph advances are chosen to be fractional (2.5 px, 4.5 px) so the 12.4
// fixed-point differential-rounding steps are observable, and the bitmaps are
// small solid blocks whose ink can be located byte-for-byte in the framebuffer.
//
// tinyFamily(): 1-bit, ASCII subset + U+2026 + U+FFFD.
// grayFamily(): 2-bit, one glyph carrying all four raw levels.
// cjkFamily():  1-bit, a single CJK glyph, used as a fallback-font target.

#include <EpdFontFamily.h>

#include <cstdint>

namespace testfonts {

// --- 1-bit "tiny" font -------------------------------------------------------
//
// glyph index: 0=' ' 1='A' 2='B' 3='C' 4='D' 5='E' 6='F' 7=U+2026 8=U+FFFD
//
// 'A' 2x2 solid, advance 3.0 px      'D' 2x2, ink only at (0,0)
// 'B' 2x2 solid, advance 2.5 px      'E' 1x1 solid, advance 1.0 px
// 'C' 4x4 solid, advance 4.5 px      'F' 2x2, ink only at (1,1)
inline constexpr uint8_t kTinyBitmap[] = {
    0xF0,  // 'A' 2x2 solid
    0xF0,  // 'B' 2x2 solid
    0xFF, 0xFF,  // 'C' 4x4 solid
    0x80,  // 'D' 2x2, pixel (0,0)
    0x80,  // 'E' 1x1
    0x10,  // 'F' 2x2, pixel (1,1)
    0xE0,  // U+2026 3x1
    0xF0,  // U+FFFD 2x2 solid
};

inline constexpr EpdGlyph kTinyGlyphs[] = {
    // width, height, advanceX (12.4), left, top, dataLength, dataOffset
    {0, 0, 0x20, 0, 0, 0, 0},  // ' '     advance 2.0 px
    {2, 2, 0x30, 0, 2, 1, 0},  // 'A'     advance 3.0 px
    {2, 2, 0x28, 0, 2, 1, 1},  // 'B'     advance 2.5 px
    {4, 4, 0x48, 0, 4, 2, 2},  // 'C'     advance 4.5 px
    {2, 2, 0x30, 0, 2, 1, 4},  // 'D'     advance 3.0 px
    {1, 1, 0x10, 0, 1, 1, 5},  // 'E'     advance 1.0 px
    {2, 2, 0x30, 0, 2, 1, 6},  // 'F'     advance 3.0 px
    {3, 1, 0x30, 0, 1, 1, 7},  // U+2026  advance 3.0 px
    {2, 2, 0x20, 0, 2, 1, 8},  // U+FFFD  advance 2.0 px
};

inline constexpr EpdUnicodeInterval kTinyIntervals[] = {
    {0x0020, 0x0020, 0},
    {0x0041, 0x0046, 1},
    {0x2026, 0x2026, 7},
    {0xFFFD, 0xFFFD, 8},
};

// --- 2-bit font: one glyph whose four pixels carry raw levels 3,2,1,0 --------
// renderCharImpl maps raw -> bmpVal as (3 - raw), so the pixels read
// black, dark gray, light gray, white in row-major order.
inline constexpr uint8_t kGrayBitmap[] = {0xE4};

inline constexpr EpdGlyph kGrayGlyphs[] = {
    {2, 2, 0x30, 0, 2, 1, 0},  // 'A'
};

inline constexpr EpdUnicodeInterval kGrayIntervals[] = {
    {0x0041, 0x0041, 0},
};

// --- 1-bit CJK font: U+4E2D only, 4x4 solid ---------------------------------
inline constexpr uint8_t kCjkBitmap[] = {0xFF, 0xFF};

inline constexpr EpdGlyph kCjkGlyphs[] = {
    {4, 4, 0x40, 0, 4, 2, 0},  // U+4E2D, advance 4.0 px
};

inline constexpr EpdUnicodeInterval kCjkIntervals[] = {
    {0x4E2D, 0x4E2D, 0},
};

inline const EpdFontData& tinyData() {
  static const EpdFontData data = [] {
    EpdFontData d{};
    d.bitmap = kTinyBitmap;
    d.glyph = kTinyGlyphs;
    d.intervals = kTinyIntervals;
    d.intervalCount = sizeof(kTinyIntervals) / sizeof(kTinyIntervals[0]);
    d.advanceY = 10;
    d.ascender = 8;
    d.descender = -2;
    d.is2Bit = false;
    return d;
  }();
  return data;
}

inline const EpdFontData& grayData() {
  static const EpdFontData data = [] {
    EpdFontData d{};
    d.bitmap = kGrayBitmap;
    d.glyph = kGrayGlyphs;
    d.intervals = kGrayIntervals;
    d.intervalCount = sizeof(kGrayIntervals) / sizeof(kGrayIntervals[0]);
    d.advanceY = 10;
    d.ascender = 8;
    d.descender = -2;
    d.is2Bit = true;
    return d;
  }();
  return data;
}

inline const EpdFontData& cjkData() {
  static const EpdFontData data = [] {
    EpdFontData d{};
    d.bitmap = kCjkBitmap;
    d.glyph = kCjkGlyphs;
    d.intervals = kCjkIntervals;
    d.intervalCount = sizeof(kCjkIntervals) / sizeof(kCjkIntervals[0]);
    d.advanceY = 12;
    d.ascender = 9;
    d.descender = -3;
    d.is2Bit = false;
    return d;
  }();
  return data;
}

inline EpdFontFamily tinyFamily() {
  static const EpdFont font(&tinyData());
  return EpdFontFamily(&font);
}

inline EpdFontFamily grayFamily() {
  static const EpdFont font(&grayData());
  return EpdFontFamily(&font);
}

inline EpdFontFamily cjkFamily() {
  static const EpdFont font(&cjkData());
  return EpdFontFamily(&font);
}

}  // namespace testfonts
