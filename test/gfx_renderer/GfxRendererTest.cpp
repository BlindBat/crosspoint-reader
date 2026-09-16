// Drives the real lib/GfxRenderer/GfxRenderer.cpp against the HalDisplay stub
// in stubs/, which exposes a plain 800x480 1-bpp framebuffer plus call
// recorders. Everything asserted here is read back out of that framebuffer.

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "TestFonts.h"

namespace {

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;
constexpr int kPanelWB = kPanelW / 8;
constexpr uint32_t kBufferSize = static_cast<uint32_t>(kPanelWB) * kPanelH;

constexpr int kTiny = 1;
constexpr int kGray = 2;
constexpr int kCjk = 3;

using Orientation = GfxRenderer::Orientation;

constexpr Orientation kAllOrientations[] = {
    GfxRenderer::Portrait,
    GfxRenderer::LandscapeClockwise,
    GfxRenderer::PortraitInverted,
    GfxRenderer::LandscapeCounterClockwise,
};

// Independent re-implementation of the production rotate, so a change to the
// production mapping shows up as a test failure rather than cancelling out.
void rotateRef(const Orientation o, const int x, const int y, int* phyX, int* phyY) {
  switch (o) {
    case GfxRenderer::Portrait:
      *phyX = y;
      *phyY = kPanelH - 1 - x;
      break;
    case GfxRenderer::LandscapeClockwise:
      *phyX = kPanelW - 1 - x;
      *phyY = kPanelH - 1 - y;
      break;
    case GfxRenderer::PortraitInverted:
      *phyX = kPanelW - 1 - y;
      *phyY = x;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      *phyX = x;
      *phyY = y;
      break;
  }
}

// Framebuffer convention: bit clear = black ink, bit set = white.
bool phyInk(const uint8_t* fb, const int x, const int y) {
  return (fb[static_cast<size_t>(y) * kPanelWB + x / 8] & static_cast<uint8_t>(0x80u >> (x % 8))) == 0;
}

class GfxRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer.begin();
    renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer.insertFont(kTiny, testfonts::tinyFamily());
    renderer.insertFont(kGray, testfonts::grayFamily());
    renderer.insertFont(kCjk, testfonts::cjkFamily());
    display.clearScreen(0xFF);
    savedInsets = BoardConfig::ACTIVE.viewableInsets;
  }

  void TearDown() override { BoardConfig::ACTIVE.viewableInsets = savedInsets; }

  uint8_t* fb() const { return display.getFrameBuffer(); }

  bool ink(const int x, const int y) const {
    int px = 0, py = 0;
    rotateRef(renderer.getOrientation(), x, y, &px, &py);
    return phyInk(fb(), px, py);
  }

  size_t inkCount() const {
    size_t n = 0;
    for (uint32_t i = 0; i < kBufferSize; i++) {
      for (int b = 0; b < 8; b++) {
        if ((fb()[i] & (0x80u >> b)) == 0) n++;
      }
    }
    return n;
  }

  // Columns (logical x) containing ink anywhere on the screen.
  std::set<int> inkColumns() const {
    std::set<int> cols;
    for (int y = 0; y < renderer.getScreenHeight(); y++) {
      for (int x = 0; x < renderer.getScreenWidth(); x++) {
        if (ink(x, y)) cols.insert(x);
      }
    }
    return cols;
  }

  HalDisplay display;
  GfxRenderer renderer{display};
  BoardConfig::ViewableInsets savedInsets{};
};

// ---------------------------------------------------------------------------
// Orientation and coordinate transform (FR-107)
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, ScreenDimensionsSwapPerOrientation) {
  renderer.setOrientation(GfxRenderer::Portrait);
  EXPECT_EQ(renderer.getScreenWidth(), kPanelH);
  EXPECT_EQ(renderer.getScreenHeight(), kPanelW);
  renderer.setOrientation(GfxRenderer::PortraitInverted);
  EXPECT_EQ(renderer.getScreenWidth(), kPanelH);
  EXPECT_EQ(renderer.getScreenHeight(), kPanelW);
  renderer.setOrientation(GfxRenderer::LandscapeClockwise);
  EXPECT_EQ(renderer.getScreenWidth(), kPanelW);
  EXPECT_EQ(renderer.getScreenHeight(), kPanelH);
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  EXPECT_EQ(renderer.getScreenWidth(), kPanelW);
  EXPECT_EQ(renderer.getScreenHeight(), kPanelH);
  EXPECT_EQ(renderer.getDisplayWidth(), kPanelW);
  EXPECT_EQ(renderer.getDisplayHeight(), kPanelH);
  EXPECT_EQ(renderer.getDisplayWidthBytes(), kPanelWB);
  EXPECT_EQ(renderer.getBufferSize(), kBufferSize);
}

TEST_F(GfxRendererTest, DrawPixelLandsOnTheRotatedPanelCellInEveryOrientation) {
  for (const Orientation o : kAllOrientations) {
    display.clearScreen(0xFF);
    renderer.setOrientation(o);
    renderer.drawPixel(0, 0, true);
    int px = 0, py = 0;
    rotateRef(o, 0, 0, &px, &py);
    EXPECT_TRUE(phyInk(fb(), px, py)) << "orientation " << static_cast<int>(o);
    EXPECT_EQ(inkCount(), 1u) << "orientation " << static_cast<int>(o);
  }
}

TEST_F(GfxRendererTest, EachOrientationMapsTheFourLogicalCornersToTheFourPanelCorners) {
  for (const Orientation o : kAllOrientations) {
    display.clearScreen(0xFF);
    renderer.setOrientation(o);
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    renderer.drawPixel(0, 0, true);
    renderer.drawPixel(w - 1, 0, true);
    renderer.drawPixel(0, h - 1, true);
    renderer.drawPixel(w - 1, h - 1, true);
    EXPECT_EQ(inkCount(), 4u) << "orientation " << static_cast<int>(o);
    EXPECT_TRUE(phyInk(fb(), 0, 0));
    EXPECT_TRUE(phyInk(fb(), kPanelW - 1, 0));
    EXPECT_TRUE(phyInk(fb(), 0, kPanelH - 1));
    EXPECT_TRUE(phyInk(fb(), kPanelW - 1, kPanelH - 1));
  }
}

TEST_F(GfxRendererTest, DrawPixelOutsideThePanelIsDropped) {
  for (const Orientation o : kAllOrientations) {
    display.clearScreen(0xFF);
    renderer.setOrientation(o);
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    renderer.drawPixel(-1, 0, true);
    renderer.drawPixel(0, -1, true);
    renderer.drawPixel(w, 0, true);
    renderer.drawPixel(0, h, true);
    renderer.drawPixel(-5000, -5000, true);
    EXPECT_EQ(inkCount(), 0u) << "orientation " << static_cast<int>(o);
  }
}

TEST_F(GfxRendererTest, DrawPixelFalseClearsInkBackToWhite) {
  renderer.drawPixel(3, 4, true);
  ASSERT_TRUE(ink(3, 4));
  renderer.drawPixel(3, 4, false);
  EXPECT_FALSE(ink(3, 4));
  EXPECT_EQ(inkCount(), 0u);
}

TEST_F(GfxRendererTest, TapToLogicalInvertsTheOrientationRotation) {
  for (const Orientation o : kAllOrientations) {
    renderer.setOrientation(o);
    for (const int lx : {0, 17, renderer.getScreenWidth() - 1}) {
      for (const int ly : {0, 23, renderer.getScreenHeight() - 1}) {
        int px = 0, py = 0;
        rotateRef(o, lx, ly, &px, &py);
        // Sample the centre of the physical cell so the float->int truncation
        // inside tapToLogical cannot land on a neighbouring pixel.
        const float nx = (static_cast<float>(px) + 0.5f) / static_cast<float>(kPanelW);
        const float ny = (static_cast<float>(py) + 0.5f) / static_cast<float>(kPanelH);
        int outX = -1, outY = -1;
        renderer.tapToLogical(nx, ny, outX, outY);
        EXPECT_EQ(outX, lx) << "orientation " << static_cast<int>(o);
        EXPECT_EQ(outY, ly) << "orientation " << static_cast<int>(o);
      }
    }
  }
}

TEST_F(GfxRendererTest, TapToLogicalClampsOutOfRangeNormalisedInput) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  int x = -1, y = -1;
  renderer.tapToLogical(-3.0f, -3.0f, x, y);
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);
  renderer.tapToLogical(9.0f, 9.0f, x, y);
  EXPECT_EQ(x, kPanelW - 1);
  EXPECT_EQ(y, kPanelH - 1);
}

TEST_F(GfxRendererTest, ViewableInsetsRotateWithTheOrientation) {
  BoardConfig::ACTIVE.viewableInsets = {11, 3, 5, 7};  // top, right, bottom, left
  int t = 0, r = 0, b = 0, l = 0;

  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  EXPECT_EQ(std::make_tuple(t, r, b, l), std::make_tuple(11, 3, 5, 7));

  renderer.setOrientation(GfxRenderer::LandscapeClockwise);
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  EXPECT_EQ(std::make_tuple(t, r, b, l), std::make_tuple(7, 11, 3, 5));

  renderer.setOrientation(GfxRenderer::PortraitInverted);
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  EXPECT_EQ(std::make_tuple(t, r, b, l), std::make_tuple(5, 7, 11, 3));

  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  EXPECT_EQ(std::make_tuple(t, r, b, l), std::make_tuple(3, 5, 7, 11));
}

TEST_F(GfxRendererTest, ClearAndInvertCoverTheWholeFramebuffer) {
  renderer.clearScreen(0x00);
  EXPECT_EQ(inkCount(), static_cast<size_t>(kPanelW) * kPanelH);
  renderer.invertScreen();
  EXPECT_EQ(inkCount(), 0u);
  renderer.drawPixel(0, 0, true);
  renderer.invertScreen();
  EXPECT_EQ(inkCount(), static_cast<size_t>(kPanelW) * kPanelH - 1);
}

// ---------------------------------------------------------------------------
// fillRect head/middle/tail byte masks
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, FillRectWritesHeadMiddleAndTailMasksOnly) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  // x in [5, 21) spans byte 0 (bits 5..7), byte 1 (all), byte 2 (bits 0..4).
  renderer.fillRect(5, 3, 16, 1, true);
  const uint8_t* row = fb() + 3 * kPanelWB;
  EXPECT_EQ(row[0], 0xF8);  // bits 5,6,7 cleared
  EXPECT_EQ(row[1], 0x00);
  EXPECT_EQ(row[2], 0x07);  // bits 0..4 cleared
  EXPECT_EQ(row[3], 0xFF);
  // Neighbouring rows untouched.
  EXPECT_EQ(fb()[2 * kPanelWB], 0xFF);
  EXPECT_EQ(fb()[4 * kPanelWB], 0xFF);
}

TEST_F(GfxRendererTest, FillRectInsideOneByteUsesTheCombinedMask) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.fillRect(2, 0, 3, 1, true);
  EXPECT_EQ(fb()[0], 0xC7);  // bits 2,3,4 cleared
}

TEST_F(GfxRendererTest, FillRectWhiteRestoresOnlyTheRectBits) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.clearScreen(0x00);
  renderer.fillRect(5, 3, 16, 1, false);
  const uint8_t* row = fb() + 3 * kPanelWB;
  EXPECT_EQ(row[0], 0x07);
  EXPECT_EQ(row[1], 0xFF);
  EXPECT_EQ(row[2], 0xF8);
  EXPECT_EQ(row[3], 0x00);
}

TEST_F(GfxRendererTest, FillRectClipsToTheLogicalScreen) {
  for (const Orientation o : kAllOrientations) {
    display.clearScreen(0xFF);
    renderer.setOrientation(o);
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    renderer.fillRect(-10, -10, w + 100, h + 100, true);
    EXPECT_EQ(inkCount(), static_cast<size_t>(kPanelW) * kPanelH) << "orientation " << static_cast<int>(o);
  }
}

TEST_F(GfxRendererTest, FillRectFullyOffscreenOrDegenerateIsANoOp) {
  renderer.fillRect(0, 0, 0, 10, true);
  renderer.fillRect(0, 0, 10, 0, true);
  renderer.fillRect(0, 0, -4, -4, true);
  renderer.fillRect(kPanelW, 0, 10, 10, true);
  renderer.fillRect(0, kPanelH, 10, 10, true);
  renderer.fillRect(-20, 0, 10, 10, true);
  EXPECT_EQ(inkCount(), 0u);
}

TEST_F(GfxRendererTest, FillRectMatchesPerPixelDrawingInEveryOrientation) {
  for (const Orientation o : kAllOrientations) {
    renderer.setOrientation(o);
    display.clearScreen(0xFF);
    renderer.fillRect(7, 11, 19, 13, true);
    const std::vector<uint8_t> fast(fb(), fb() + kBufferSize);

    display.clearScreen(0xFF);
    for (int y = 11; y < 11 + 13; y++) {
      for (int x = 7; x < 7 + 19; x++) renderer.drawPixel(x, y, true);
    }
    const std::vector<uint8_t> slow(fb(), fb() + kBufferSize);
    EXPECT_EQ(fast, slow) << "orientation " << static_cast<int>(o);
  }
}

TEST_F(GfxRendererTest, DitheredFillsMatchTheirPerPixelPatternInEveryOrientation) {
  for (const Orientation o : kAllOrientations) {
    for (const Color c : {Color::LightGray, Color::DarkGray}) {
      renderer.setOrientation(o);
      display.clearScreen(0xFF);
      renderer.fillRectDither(3, 5, 21, 9, c);
      const std::vector<uint8_t> fast(fb(), fb() + kBufferSize);

      display.clearScreen(0xFF);
      for (int y = 5; y < 5 + 9; y++) {
        for (int x = 3; x < 3 + 21; x++) {
          const bool black = (c == Color::LightGray) ? (x % 2 == 0 && y % 2 == 0) : ((x + y) % 2 == 0);
          renderer.drawPixel(x, y, black);
        }
      }
      const std::vector<uint8_t> slow(fb(), fb() + kBufferSize);
      EXPECT_EQ(fast, slow) << "orientation " << static_cast<int>(o) << " color " << static_cast<int>(c);
    }
  }
}

TEST_F(GfxRendererTest, ClearColorFillIsANoOp) {
  renderer.clearScreen(0x00);
  renderer.fillRectDither(0, 0, 40, 40, Color::Clear);
  EXPECT_EQ(inkCount(), static_cast<size_t>(kPanelW) * kPanelH);
}

// ---------------------------------------------------------------------------
// Strip (tiled grayscale) target
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, StripTargetReportsTheBandAsTheWriteTarget) {
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  EXPECT_EQ(renderer.getWriteTarget(), renderer.getFrameBuffer());
  EXPECT_EQ(renderer.getWriteOriginY(), 0);
  EXPECT_EQ(renderer.getWriteRows(), kPanelH);

  renderer.beginStripTarget(scratch.data(), 100, 40);
  EXPECT_EQ(renderer.getWriteTarget(), scratch.data());
  EXPECT_EQ(renderer.getWriteOriginY(), 100);
  EXPECT_EQ(renderer.getWriteRows(), 40);

  renderer.endStripTarget();
  EXPECT_EQ(renderer.getWriteTarget(), renderer.getFrameBuffer());
  EXPECT_EQ(renderer.getWriteOriginY(), 0);
  EXPECT_EQ(renderer.getWriteRows(), kPanelH);
}

TEST_F(GfxRendererTest, StripTargetRedirectsPixelsIntoTheBandAndClipsTheRest) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  renderer.beginStripTarget(scratch.data(), 100, 40);

  renderer.drawPixel(8, 120, true);  // inside the band -> scratch row 20
  renderer.drawPixel(8, 50, true);   // above the band -> dropped
  renderer.drawPixel(8, 140, true);  // first row past the band -> dropped
  renderer.drawPixel(8, 99, true);   // last row before the band -> dropped
  renderer.drawPixel(8, 139, true);  // last row of the band -> scratch row 39
  renderer.endStripTarget();

  EXPECT_TRUE(phyInk(scratch.data(), 8, 20));
  EXPECT_TRUE(phyInk(scratch.data(), 8, 39));
  EXPECT_EQ(inkCount(), 0u) << "the shared framebuffer must stay untouched";
  size_t bandInk = 0;
  for (size_t i = 0; i < scratch.size(); i++) {
    for (int b = 0; b < 8; b++) {
      if ((scratch[i] & (0x80u >> b)) == 0) bandInk++;
    }
  }
  EXPECT_EQ(bandInk, 2u);
}

TEST_F(GfxRendererTest, ClearScreenInStripModeOnlyClearsTheBand) {
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  renderer.beginStripTarget(scratch.data(), 100, 40);
  renderer.clearScreen(0x00);
  renderer.endStripTarget();
  EXPECT_EQ(inkCount(), 0u);
  EXPECT_TRUE(std::all_of(scratch.begin(), scratch.end(), [](uint8_t b) { return b == 0x00; }));
}

TEST_F(GfxRendererTest, FillRectInStripModeClipsToTheBand) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  renderer.beginStripTarget(scratch.data(), 100, 40);
  renderer.fillRect(0, 0, kPanelW, kPanelH, true);  // whole screen
  renderer.endStripTarget();
  EXPECT_EQ(inkCount(), 0u);
  EXPECT_TRUE(std::all_of(scratch.begin(), scratch.end(), [](uint8_t b) { return b == 0x00; }));
}

TEST_F(GfxRendererTest, GlyphIntersectsStripIsAlwaysTrueWithoutAStrip) {
  EXPECT_TRUE(renderer.glyphIntersectsStrip(0, -5000, 10, -4990));
  EXPECT_TRUE(renderer.glyphIntersectsStrip(0, 5000, 10, 5010));
}

TEST_F(GfxRendererTest, GlyphIntersectsStripCullsBoxesOutsideTheBand) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  renderer.beginStripTarget(scratch.data(), 100, 40);
  EXPECT_FALSE(renderer.glyphIntersectsStrip(0, 90, 10, 99));
  EXPECT_TRUE(renderer.glyphIntersectsStrip(0, 90, 10, 100));
  EXPECT_TRUE(renderer.glyphIntersectsStrip(0, 139, 10, 160));
  EXPECT_FALSE(renderer.glyphIntersectsStrip(0, 140, 10, 160));
  renderer.endStripTarget();
}

TEST_F(GfxRendererTest, GlyphIntersectsStripIsOrientationAware) {
  renderer.setOrientation(GfxRenderer::Portrait);
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 40, 0xFF);
  renderer.beginStripTarget(scratch.data(), 100, 40);
  // Portrait maps phyY = 479 - logicalX, so the band covers logical x 340..379.
  EXPECT_TRUE(renderer.glyphIntersectsStrip(340, 0, 379, 10));
  EXPECT_FALSE(renderer.glyphIntersectsStrip(380, 0, 400, 10));
  EXPECT_FALSE(renderer.glyphIntersectsStrip(300, 0, 339, 10));
  renderer.endStripTarget();
}

TEST_F(GfxRendererTest, GlyphsOutsideTheBandAreNotRendered) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 16, 0xFF);
  renderer.beginStripTarget(scratch.data(), 0, 16);
  renderer.drawText(kTiny, 0, 0, "A");    // baseline y=8, glyph rows 6..7 -> inside
  renderer.drawText(kTiny, 0, 100, "A");  // rows 106..107 -> outside
  renderer.endStripTarget();
  size_t bandInk = 0;
  for (size_t i = 0; i < scratch.size(); i++) {
    for (int b = 0; b < 8; b++) {
      if ((scratch[i] & (0x80u >> b)) == 0) bandInk++;
    }
  }
  EXPECT_EQ(bandInk, 4u) << "only the in-band 2x2 glyph should have been drawn";
  EXPECT_EQ(inkCount(), 0u);
}

// ---------------------------------------------------------------------------
// Text metrics: measurement and drawing must agree
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, TextMetricsForNullEmptyAndUnknownFonts) {
  EXPECT_EQ(renderer.getTextWidth(kTiny, nullptr), 0);
  EXPECT_EQ(renderer.getTextWidth(kTiny, ""), 0);
  EXPECT_EQ(renderer.getTextWidth(999, "A"), 0);
  EXPECT_EQ(renderer.getTextAdvanceX(999, "A", EpdFontFamily::REGULAR), 0);
  EXPECT_EQ(renderer.getLineHeight(999), 0);
  EXPECT_EQ(renderer.getFontAscenderSize(999), 0);
  EXPECT_EQ(renderer.getTextHeight(999), 0);
  EXPECT_EQ(renderer.getSpaceWidth(999), 0);
  EXPECT_EQ(renderer.getSpaceAdvance(999, 'A', 'B', EpdFontFamily::REGULAR), 0);
  EXPECT_EQ(renderer.getKerning(999, 'A', 'B', EpdFontFamily::REGULAR), 0);
  renderer.drawText(kTiny, 0, 0, nullptr);
  renderer.drawText(kTiny, 0, 0, "");
  renderer.drawText(999, 0, 0, "A");
  EXPECT_EQ(inkCount(), 0u);
}

TEST_F(GfxRendererTest, FontMetricsComeFromTheRegularStyleData) {
  EXPECT_EQ(renderer.getLineHeight(kTiny), 10);
  EXPECT_EQ(renderer.getLineHeight(kTiny, 0.5f), 5);
  EXPECT_EQ(renderer.getFontAscenderSize(kTiny), 8);
  EXPECT_EQ(renderer.getTextHeight(kTiny), 8);
  EXPECT_EQ(renderer.getSpaceWidth(kTiny), 2);                                      // 2.0 px advance
  EXPECT_EQ(renderer.getSpaceAdvance(kTiny, 'A', 'B', EpdFontFamily::REGULAR), 2);  // no kern table
  EXPECT_EQ(renderer.getKerning(kTiny, 'A', 'B', EpdFontFamily::REGULAR), 0);
}

TEST_F(GfxRendererTest, TextAdvanceSnapsEachStepIndependentlyOfPosition) {
  // 'B' advances 2.5 px; each step snaps on its own, so four glyphs measure
  // 4 x 3 px rather than 10 px.
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "B", EpdFontFamily::REGULAR), 3);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BB", EpdFontFamily::REGULAR), 6);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBB", EpdFontFamily::REGULAR), 9);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBBB", EpdFontFamily::REGULAR), 12);
}

TEST_F(GfxRendererTest, DrawnGlyphColumnsMatchTheMeasuredAdvanceSteps) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawText(kTiny, 0, 0, "BBBB");
  // Each 'B' is a 2x2 block at its cursor column; cursors are the prefix advances.
  const std::set<int> expected{0, 1, 3, 4, 6, 7, 9, 10};
  EXPECT_EQ(inkColumns(), expected);

  const std::string prefixes[] = {"", "B", "BB", "BBB"};
  int i = 0;
  for (const auto& prefix : prefixes) {
    const int cursor = prefix.empty() ? 0 : renderer.getTextAdvanceX(kTiny, prefix.c_str(), EpdFontFamily::REGULAR);
    EXPECT_TRUE(ink(cursor, 6)) << "glyph " << i;
    i++;
  }
}

TEST_F(GfxRendererTest, TextWidthIsTheInkBoundingBoxNotTheAdvance) {
  // Bounding box stops at the last glyph's right edge; the advance includes
  // the trailing side bearing.
  EXPECT_EQ(renderer.getTextWidth(kTiny, "BBBB"), 11);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBBB", EpdFontFamily::REGULAR), 12);
  EXPECT_EQ(renderer.getTextWidth(kTiny, "A"), 2);
  EXPECT_EQ(renderer.getTextWidth(kTiny, "C"), 4);
}

TEST_F(GfxRendererTest, DrawnTextEndsWhereTheMeasuredAdvanceSays) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  const char* text = "ABCE";
  renderer.drawText(kTiny, 4, 0, text);
  const std::set<int> cols = inkColumns();
  ASSERT_FALSE(cols.empty());
  EXPECT_EQ(*cols.begin(), 4);
  EXPECT_EQ(*cols.rbegin(), 4 + renderer.getTextWidth(kTiny, text) - 1);
  EXPECT_LE(*cols.rbegin(), 4 + renderer.getTextAdvanceX(kTiny, text, EpdFontFamily::REGULAR));
}

TEST_F(GfxRendererTest, SpacesAdvanceTheCursorWithoutDrawingInk) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawText(kTiny, 0, 0, "A A");
  // A@0 (adv 3) -> space@3 (adv 2) -> A@5
  EXPECT_EQ(inkColumns(), (std::set<int>{0, 1, 5, 6}));
}

TEST_F(GfxRendererTest, MissingGlyphsFallBackToTheReplacementGlyph) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  // 'Z' is outside the font's interval table; U+FFFD is a 2x2 block, advance 2.0.
  EXPECT_EQ(renderer.getTextWidth(kTiny, "Z"), 2);
  renderer.drawText(kTiny, 0, 0, "Z");
  EXPECT_EQ(inkCount(), 4u);
}

TEST_F(GfxRendererTest, CombiningMarksDoNotAdvanceTheMeasuredCursor) {
  // U+0301 COMBINING ACUTE: no glyph in the font, zero advance either way.
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "A\xcc\x81", EpdFontFamily::REGULAR),
            renderer.getTextAdvanceX(kTiny, "A", EpdFontFamily::REGULAR));
  renderer.drawText(kTiny, 0, 0, "A\xcc\x81");
  EXPECT_EQ(inkCount(), 4u) << "a mark the font cannot render must not paint anything";
}

TEST_F(GfxRendererTest, DrawCenteredTextCentresOnTheLogicalWidth) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawCenteredText(kTiny, 0, "A");
  const int expected = (renderer.getScreenWidth() - renderer.getTextWidth(kTiny, "A")) / 2;
  const std::set<int> cols = inkColumns();
  ASSERT_FALSE(cols.empty());
  EXPECT_EQ(*cols.begin(), expected);
}

TEST_F(GfxRendererTest, RotatedTextRunsDownTheScreenAndReusesTheSameAdvances) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawTextRotated90CW(kTiny, 20, 100, "BBBB");
  std::set<int> rows;
  for (int y = 0; y < renderer.getScreenHeight(); y++) {
    for (int x = 0; x < renderer.getScreenWidth(); x++) {
      if (ink(x, y)) rows.insert(y);
    }
  }
  // Rotated text advances upward (decreasing y) by the same 3 px steps.
  EXPECT_EQ(rows, (std::set<int>{90, 91, 93, 94, 96, 97, 99, 100}));
}

// ---------------------------------------------------------------------------
// Superscript / subscript placement
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, SupAndSubHalveTheMeasuredAdvance) {
  const auto sup = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP);
  const auto sub = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUB);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBBB", EpdFontFamily::REGULAR), 12);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBBB", sup), 4);
  EXPECT_EQ(renderer.getTextAdvanceX(kTiny, "BBBB", sub), 4);
}

TEST_F(GfxRendererTest, SupRendersTheGlyphAtHalfSizeOnHalvedSteps) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  const auto sup = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP);
  renderer.drawText(kTiny, 0, 0, "BBBB", true, sup);
  // 2x2 glyph -> 1x1; cursor steps of 1 px.
  EXPECT_EQ(inkColumns(), (std::set<int>{0, 1, 2, 3}));
  EXPECT_EQ(inkCount(), 4u);
}

TEST_F(GfxRendererTest, SupPlacesTheScaledGlyphAtTheHalvedBearing) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  const auto sup = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP);
  renderer.drawText(kTiny, 10, 20, "B", true, sup);
  // baseY = (y + ascender) - top/2 = 20 + 8 - 1 = 27.
  EXPECT_TRUE(ink(10, 27));
  EXPECT_EQ(inkCount(), 1u);
}

TEST_F(GfxRendererTest, SubUsesTheSameScalingAsSupAtTheSameCursor) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawText(kTiny, 10, 20, "B", true, static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP));
  const std::vector<uint8_t> supFrame(fb(), fb() + kBufferSize);
  display.clearScreen(0xFF);
  renderer.drawText(kTiny, 10, 20, "B", true, static_cast<EpdFontFamily::Style>(EpdFontFamily::SUB));
  const std::vector<uint8_t> subFrame(fb(), fb() + kBufferSize);
  // The vertical offset is applied by the caller, not by the renderer.
  EXPECT_EQ(supFrame, subFrame);
}

TEST_F(GfxRendererTest, ScaledGlyphsKeepInkThatNearestNeighbourSamplingWouldDrop) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  const auto sup = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP);
  // 'F' has its single ink pixel at (1,1): a (0,0) point sample would miss it.
  renderer.drawText(kTiny, 0, 0, "F", true, sup);
  EXPECT_EQ(inkCount(), 1u);
  display.clearScreen(0xFF);
  // 'D' has its ink at (0,0) and must still draw.
  renderer.drawText(kTiny, 0, 0, "D", true, sup);
  EXPECT_EQ(inkCount(), 1u);
}

// ---------------------------------------------------------------------------
// Render modes over a 2-bit font
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, BwModeInksEveryNonWhiteLevelOfATwoBitGlyph) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.setRenderMode(GfxRenderer::BW);
  EXPECT_EQ(renderer.getRenderMode(), GfxRenderer::BW);
  renderer.drawText(kGray, 0, 0, "A");
  EXPECT_TRUE(ink(0, 6));   // black
  EXPECT_TRUE(ink(1, 6));   // dark gray
  EXPECT_TRUE(ink(0, 7));   // light gray
  EXPECT_FALSE(ink(1, 7));  // white
  EXPECT_EQ(inkCount(), 3u);
}

TEST_F(GfxRendererTest, GrayscaleMsbMarksBothGrayLevels) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawText(kGray, 0, 0, "A");
  // Grayscale planes flag pixels in reverse: a marked pixel is cleared to white.
  EXPECT_TRUE(ink(0, 6));
  EXPECT_FALSE(ink(1, 6));
  EXPECT_FALSE(ink(0, 7));
  EXPECT_TRUE(ink(1, 7));
}

TEST_F(GfxRendererTest, GrayscaleLsbMarksOnlyTheDarkGrayLevel) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawText(kGray, 0, 0, "A");
  EXPECT_TRUE(ink(0, 6));
  EXPECT_FALSE(ink(1, 6));
  EXPECT_TRUE(ink(0, 7));
  EXPECT_TRUE(ink(1, 7));
}

TEST_F(GfxRendererTest, ScaledGlyphsIgnoreTheRenderModeAndAlwaysPaintInk) {
  // Deviation pinned, not endorsed: renderCharScaled() takes a renderMode but
  // never reads it (GfxRenderer.cpp:380, -Wunused-parameter), so a SUP/SUB
  // glyph paints at pixelState during the grey-plane passes where the
  // full-size path would only flag selected levels.
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  const auto sup = static_cast<EpdFontFamily::Style>(EpdFontFamily::SUP);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);

  // Full size: the LSB plane only clears the dark-gray pixel, so a white
  // framebuffer stays white.
  renderer.drawText(kGray, 0, 0, "A");
  EXPECT_EQ(inkCount(), 0u);

  // Half size: the same mode paints the covered cell black instead.
  display.clearScreen(0xFF);
  renderer.drawText(kGray, 0, 0, "A", true, sup);
  EXPECT_EQ(inkCount(), 1u);
  EXPECT_TRUE(ink(0, 7));
}

// ---------------------------------------------------------------------------
// CJK fallback font routing
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, CjkWithoutAFallbackUsesTheReplacementGlyph) {
  EXPECT_EQ(renderer.getTextWidth(kTiny, "\xe4\xb8\xad"), 2);  // U+4E2D -> U+FFFD (2x2)
}

TEST_F(GfxRendererTest, CjkIsRoutedToTheRegisteredFallbackFont) {
  renderer.setFallbackFont(kTiny, kCjk);
  EXPECT_EQ(renderer.getTextWidth(kTiny, "\xe4\xb8\xad"), 4);  // the fallback's 4x4 glyph
  EXPECT_EQ(renderer.getTextWidth(kTiny, "A"), 2) << "Latin must stay on the primary font";
  renderer.clearFallbackFonts();
  EXPECT_EQ(renderer.getTextWidth(kTiny, "\xe4\xb8\xad"), 2);
}

TEST_F(GfxRendererTest, FallbackDrawingCentresTheFallbackLineBoxInTheRequestedOne) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.setFallbackFont(kTiny, kCjk);
  renderer.drawText(kTiny, 0, 0, "\xe4\xb8\xad");
  // yPos = ascender(9) + (lineHeight(10) - lineHeight(12)) / 2 = 9 - 1 = 8;
  // the 4x4 glyph has top=4 so it covers rows 4..7.
  EXPECT_TRUE(ink(0, 4));
  EXPECT_TRUE(ink(3, 7));
  EXPECT_FALSE(ink(0, 8));
  EXPECT_EQ(inkCount(), 16u);
}

// ---------------------------------------------------------------------------
// Truncation and wrapping
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, TruncatedTextReturnsTheInputWhenItFits) {
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", 10), "AAA");
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", 8), "AAA") << "exactly-fitting text is not truncated";
}

TEST_F(GfxRendererTest, TruncatedTextDropsCharactersUntilItPlusEllipsisFits) {
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", 7), "A\xe2\x80\xa6");
}

TEST_F(GfxRendererTest, TruncatedTextDegradesToTheEllipsisAlone) {
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", 3), "\xe2\x80\xa6");
}

TEST_F(GfxRendererTest, TruncatedTextGuardsNullAndNonPositiveWidths) {
  EXPECT_EQ(renderer.truncatedText(kTiny, nullptr, 10), "");
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", 0), "");
  EXPECT_EQ(renderer.truncatedText(kTiny, "AAA", -5), "");
}

TEST_F(GfxRendererTest, TruncationCutsOnUtf8BoundariesOnly) {
  // Six ellipses, each 3 bytes; truncation must never split one.
  const std::string input = "\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6";
  const std::string out = renderer.truncatedText(kTiny, input.c_str(), 9);
  EXPECT_EQ(out.size() % 3, 0u);
  const auto* p = reinterpret_cast<const unsigned char*>(out.c_str());
  while (*p) EXPECT_EQ(utf8NextCodepoint(&p), 0x2026u);
}

TEST_F(GfxRendererTest, WrappedTextBreaksOnSpaces) {
  const auto lines = renderer.wrappedText(kTiny, "AAA BBB", 8, 2);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "AAA");
  EXPECT_EQ(lines[1], "BBB");
}

TEST_F(GfxRendererTest, WrappedTextGuardsNullAndNonPositiveLimits) {
  EXPECT_TRUE(renderer.wrappedText(kTiny, nullptr, 10, 2).empty());
  EXPECT_TRUE(renderer.wrappedText(kTiny, "AAA", 0, 2).empty());
  EXPECT_TRUE(renderer.wrappedText(kTiny, "AAA", 10, 0).empty());
}

TEST_F(GfxRendererTest, WrappedTextEllipsisesTheLastAvailableLine) {
  const auto lines = renderer.wrappedText(kTiny, "AAA BBB CCC", 8, 2);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "AAA");
  EXPECT_NE(lines[1].find("\xe2\x80\xa6"), std::string::npos);
  EXPECT_LE(renderer.getTextWidth(kTiny, lines[1].c_str()), 8 + renderer.getTextWidth(kTiny, "\xe2\x80\xa6"));
}

TEST_F(GfxRendererTest, WrappedTextTruncatesASingleOverlongWordAndStops) {
  const auto lines = renderer.wrappedText(kTiny, "CCCC", 14, 3);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "CC\xe2\x80\xa6");
}

TEST_F(GfxRendererTest, WrappedTextPushesAnOverlongCarriedWordAsItsOwnLine) {
  // The carried-over word must not stay in the current line, or a later short
  // word would be appended after the ellipsis.
  const auto lines = renderer.wrappedText(kTiny, "E CCCC E", 14, 3);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "E");
  EXPECT_EQ(lines[1], "CC\xe2\x80\xa6");
  EXPECT_EQ(lines[2], "E");
}

TEST_F(GfxRendererTest, WrappedTextNeverExceedsMaxLines) {
  for (int maxLines = 1; maxLines <= 4; maxLines++) {
    const auto lines = renderer.wrappedText(kTiny, "AAA BBB CCC AAA BBB", 8, maxLines);
    EXPECT_LE(static_cast<int>(lines.size()), maxLines);
    EXPECT_FALSE(lines.empty());
  }
}

// ---------------------------------------------------------------------------
// BW snapshot chunking
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, StoreAndRestoreRoundTripsEveryByteAcrossChunkBoundaries) {
  uint8_t* buffer = renderer.getFrameBuffer();
  for (uint32_t i = 0; i < kBufferSize; i++) buffer[i] = static_cast<uint8_t>(i * 7u + 3u);
  const std::vector<uint8_t> snapshot(buffer, buffer + kBufferSize);

  ASSERT_TRUE(renderer.storeBwBuffer());
  std::memset(buffer, 0x5A, kBufferSize);
  renderer.restoreBwBuffer(true);

  EXPECT_EQ(std::vector<uint8_t>(buffer, buffer + kBufferSize), snapshot);
  // The 8,000-byte chunk seams in particular.
  for (uint32_t offset = 8000; offset < kBufferSize; offset += 8000) {
    EXPECT_EQ(buffer[offset - 1], snapshot[offset - 1]) << "offset " << offset - 1;
    EXPECT_EQ(buffer[offset], snapshot[offset]) << "offset " << offset;
  }
  EXPECT_EQ(buffer[kBufferSize - 1], snapshot[kBufferSize - 1]);
}

TEST_F(GfxRendererTest, RestoreResyncsThePanelBaselineOnlyWhenAsked) {
  ASSERT_TRUE(renderer.storeBwBuffer());
  renderer.restoreBwBuffer(true);
  EXPECT_EQ(display.cleanupCalls, 1);
  EXPECT_EQ(display.lastCleanupBuffer, renderer.getFrameBuffer());

  ASSERT_TRUE(renderer.storeBwBuffer());
  renderer.restoreBwBuffer(false);
  EXPECT_EQ(display.cleanupCalls, 1);
}

TEST_F(GfxRendererTest, RestoringWithoutAStoredSnapshotIsANoOp) {
  uint8_t* buffer = renderer.getFrameBuffer();
  std::memset(buffer, 0x3C, kBufferSize);
  renderer.restoreBwBuffer(true);
  EXPECT_EQ(buffer[0], 0x3C);
  EXPECT_EQ(display.cleanupCalls, 0);
}

TEST_F(GfxRendererTest, StoringTwiceKeepsTheSecondSnapshot) {
  uint8_t* buffer = renderer.getFrameBuffer();
  std::memset(buffer, 0x11, kBufferSize);
  ASSERT_TRUE(renderer.storeBwBuffer());
  std::memset(buffer, 0x22, kBufferSize);
  ASSERT_TRUE(renderer.storeBwBuffer());
  std::memset(buffer, 0x33, kBufferSize);
  renderer.restoreBwBuffer(false);
  EXPECT_EQ(buffer[0], 0x22);
  EXPECT_EQ(buffer[kBufferSize - 1], 0x22);
}

TEST_F(GfxRendererTest, DiscardDropsTheSnapshotWithoutTouchingTheFramebuffer) {
  uint8_t* buffer = renderer.getFrameBuffer();
  std::memset(buffer, 0x11, kBufferSize);
  ASSERT_TRUE(renderer.storeBwBuffer());
  std::memset(buffer, 0x44, kBufferSize);
  renderer.discardStoredBwBuffer();
  renderer.restoreBwBuffer(true);
  EXPECT_EQ(buffer[0], 0x44);
  EXPECT_EQ(display.cleanupCalls, 0);
}

TEST_F(GfxRendererTest, CleanupWithFrameBufferHandsThePanelTheLiveFrame) {
  renderer.cleanupGrayscaleWithFrameBuffer();
  EXPECT_EQ(display.cleanupCalls, 1);
  EXPECT_EQ(display.lastCleanupBuffer, renderer.getFrameBuffer());
}

// ---------------------------------------------------------------------------
// Framebuffer regions
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, FramebufferRegionRoundTripsThroughAnAlignedRect) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.fillRect(8, 0, 16, 2, true);
  uint8_t saved[8] = {};
  const size_t written = renderer.readFramebufferRegion(8, 0, 16, 2, saved, sizeof(saved));
  EXPECT_EQ(written, 4u);  // 2 bytes per row x 2 rows

  renderer.clearScreen(0xFF);
  EXPECT_EQ(inkCount(), 0u);
  renderer.writeFramebufferRegion(8, 0, 16, 2, saved);
  EXPECT_EQ(inkCount(), 32u);
}

TEST_F(GfxRendererTest, FramebufferRegionSnapsTheXExtentOutwardToWholeBytes) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  uint8_t dst[16] = {};
  // x=5..10 straddles bytes 0 and 1, so the aligned rect is x=0..15 (2 bytes).
  EXPECT_EQ(renderer.readFramebufferRegion(5, 0, 6, 1, dst, sizeof(dst)), 2u);
}

TEST_F(GfxRendererTest, FramebufferRegionRejectsBadArgumentsAndSmallBuffers) {
  uint8_t dst[4] = {};
  EXPECT_EQ(renderer.readFramebufferRegion(0, 0, 16, 2, nullptr, sizeof(dst)), 0u);
  EXPECT_EQ(renderer.readFramebufferRegion(0, 0, 0, 2, dst, sizeof(dst)), 0u);
  EXPECT_EQ(renderer.readFramebufferRegion(0, 0, 16, 0, dst, sizeof(dst)), 0u);
  EXPECT_EQ(renderer.readFramebufferRegion(0, 0, 32, 2, dst, sizeof(dst)), 0u) << "capacity too small";
  EXPECT_EQ(renderer.readFramebufferRegion(-100, -100, 10, 10, dst, sizeof(dst)), 0u) << "fully offscreen";
  renderer.writeFramebufferRegion(0, 0, 16, 2, nullptr);
  renderer.writeFramebufferRegion(0, 0, 0, 2, dst);
  EXPECT_EQ(inkCount(), 0u);
}

TEST_F(GfxRendererTest, RegionByteSizeMatchesWhatCopyRegionNeeds) {
  for (const Orientation o : kAllOrientations) {
    renderer.setOrientation(o);
    const size_t needed = renderer.getRegionByteSize(3, 5, 20, 9);
    ASSERT_GT(needed, 0u) << "orientation " << static_cast<int>(o);
    std::vector<uint8_t> buf(needed);
    EXPECT_TRUE(renderer.copyRegionToBuffer(3, 5, 20, 9, buf.data(), buf.size()));
    EXPECT_FALSE(renderer.copyRegionToBuffer(3, 5, 20, 9, buf.data(), needed - 1));
    EXPECT_TRUE(renderer.copyBufferToRegion(3, 5, 20, 9, buf.data(), buf.size()));
    EXPECT_FALSE(renderer.copyBufferToRegion(3, 5, 20, 9, buf.data(), needed - 1));
  }
}

TEST_F(GfxRendererTest, RegionCopyRestoresThePixelsItSaved) {
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.fillRect(0, 0, 40, 40, true);
  const size_t needed = renderer.getRegionByteSize(0, 0, 40, 40);
  std::vector<uint8_t> buf(needed);
  ASSERT_TRUE(renderer.copyRegionToBuffer(0, 0, 40, 40, buf.data(), buf.size()));
  const size_t before = inkCount();
  renderer.clearScreen(0xFF);
  ASSERT_TRUE(renderer.copyBufferToRegion(0, 0, 40, 40, buf.data(), buf.size()));
  EXPECT_EQ(inkCount(), before);
}

TEST_F(GfxRendererTest, RegionHelpersRejectDegenerateAndOffscreenRects) {
  uint8_t buf[4] = {};
  EXPECT_EQ(renderer.getRegionByteSize(0, 0, 0, 10), 0u);
  EXPECT_EQ(renderer.getRegionByteSize(0, 0, 10, 0), 0u);
  EXPECT_FALSE(renderer.copyRegionToBuffer(0, 0, 0, 10, buf, sizeof(buf)));
  EXPECT_FALSE(renderer.copyBufferToRegion(0, 0, 10, 0, buf, sizeof(buf)));
  EXPECT_FALSE(renderer.copyRegionToBuffer(0, 0, 10, 10, nullptr, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// Refresh modes, promotion and the fading fix
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, DisplayBufferPassesTheRequestedModeAndFadingFlag) {
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  EXPECT_EQ(display.displayCalls, 1);
  EXPECT_EQ(display.lastMode, HalDisplay::FULL_REFRESH);
  EXPECT_FALSE(display.lastTurnOffScreen);

  renderer.setFadingFix(true);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  EXPECT_EQ(display.lastMode, HalDisplay::HALF_REFRESH);
  EXPECT_TRUE(display.lastTurnOffScreen);
}

TEST_F(GfxRendererTest, PromotedRefreshAppliesToTheNextPaintOnly) {
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastMode, HalDisplay::FULL_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.lastMode, HalDisplay::FAST_REFRESH);
}

TEST_F(GfxRendererTest, PromotedRefreshAlsoCoversTheAsyncPath) {
  display.asyncSupported = true;
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.asyncCalls, 1);
  EXPECT_EQ(display.lastMode, HalDisplay::HALF_REFRESH);
  renderer.waitRefreshComplete();
  EXPECT_EQ(display.waitCalls, 1);
}

TEST_F(GfxRendererTest, FadingFixForcesTheBlockingRefreshPath) {
  display.asyncSupported = true;
  EXPECT_TRUE(renderer.supportsAsyncRefresh());
  renderer.setFadingFix(true);
  EXPECT_FALSE(renderer.supportsAsyncRefresh());
  renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.asyncCalls, 0);
  EXPECT_EQ(display.displayCalls, 1);
  EXPECT_TRUE(display.lastTurnOffScreen);
}

TEST_F(GfxRendererTest, AsyncSupportFollowsThePanelCapability) {
  display.asyncSupported = false;
  EXPECT_FALSE(renderer.supportsAsyncRefresh());
  display.asyncSupported = true;
  EXPECT_TRUE(renderer.supportsAsyncRefresh());
}

TEST_F(GfxRendererTest, GrayscaleDelegationPassesTheFadingFlagAndTheFramebuffer) {
  renderer.setFadingFix(true);
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  EXPECT_EQ(display.grayscaleBaseCalls, 1);
  EXPECT_TRUE(display.lastTurnOffScreen);
  renderer.copyGrayscaleLsbBuffers();
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  EXPECT_EQ(display.lsbCopies, 1);
  EXPECT_EQ(display.msbCopies, 1);
  EXPECT_EQ(display.grayBufferCalls, 1);
  EXPECT_TRUE(display.lastTurnOffScreen);
}

TEST_F(GfxRendererTest, PreconditionGrayscaleRotatesTheRectAndClampsIt) {
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.preconditionGrayscale(0, 0, 10, 20);
  EXPECT_EQ(display.preconditionCalls, 1);
  // Portrait: (0,0)->(0,479) and (9,19)->(19,470); bbox x 0..19, y 470..479.
  EXPECT_EQ(display.lastPreconditionX, 0);
  EXPECT_EQ(display.lastPreconditionY, 470);
  EXPECT_EQ(display.lastPreconditionW, 20);
  EXPECT_EQ(display.lastPreconditionH, 10);

  renderer.preconditionGrayscale(0, 0, 0, 5);
  EXPECT_EQ(display.preconditionCalls, 1) << "empty rects must not reach the panel";
  renderer.preconditionGrayscale();
  EXPECT_EQ(display.preconditionCalls, 2);
}

TEST_F(GfxRendererTest, StripGrayscaleCapabilitiesAndWritesAreDelegated) {
  EXPECT_FALSE(renderer.supportsStripGrayscale());
  EXPECT_FALSE(renderer.combinesGrayscaleBase());
  display.stripSupported = true;
  display.combinesBase = true;
  EXPECT_TRUE(renderer.supportsStripGrayscale());
  EXPECT_TRUE(renderer.combinesGrayscaleBase());

  std::vector<uint8_t> band(static_cast<size_t>(kPanelWB) * 8, 0xAB);
  renderer.writeGrayscalePlaneStrip(true, band.data(), 40, 8);
  ASSERT_EQ(display.stripWrites.size(), 1u);
  EXPECT_TRUE(display.stripWrites[0].lsbPlane);
  EXPECT_EQ(display.stripWrites[0].yStart, 40);
  EXPECT_EQ(display.stripWrites[0].numRows, 8);
  EXPECT_EQ(display.stripWrites[0].firstByte, 0xAB);
}

// ---------------------------------------------------------------------------
// Image polarity (Night Mode) and image blits
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, ImagePolarityIsCounterInvertedOnlyWhenTheOutputIsInverted) {
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.fillRect(0, 0, 16, 2, true);
  const size_t before = inkCount();
  renderer.preserveImagePolarity(0, 0, 16, 2);
  EXPECT_EQ(inkCount(), before) << "no inversion configured, so nothing to counter";

  display.setInverted(true);
  renderer.preserveImagePolarity(0, 0, 16, 2);
  EXPECT_EQ(inkCount(), before - 32) << "the 32 inked pixels flip to white";
}

TEST_F(GfxRendererTest, ImagePolarityIgnoresEmptyRectsAndStripMode) {
  display.setInverted(true);
  renderer.fillRect(0, 0, 16, 2, true);
  const size_t before = inkCount();
  renderer.preserveImagePolarity(0, 0, 0, 2);
  renderer.preserveImagePolarity(0, 0, 16, -1);
  EXPECT_EQ(inkCount(), before);

  std::vector<uint8_t> scratch(static_cast<size_t>(kPanelWB) * 8, 0xFF);
  renderer.beginStripTarget(scratch.data(), 0, 8);
  renderer.preserveImagePolarity(0, 0, 16, 2);
  renderer.endStripTarget();
  EXPECT_EQ(inkCount(), before);
}

TEST_F(GfxRendererTest, DrawImageRotatesTheOriginCornerForThePanel) {
  const uint8_t bitmap[8] = {};
  renderer.setOrientation(GfxRenderer::Portrait);
  renderer.drawImage(bitmap, 10, 20, 8, 8);
  EXPECT_EQ(display.drawImageCalls, 1);
  EXPECT_EQ(display.lastImageX, 20);
  EXPECT_EQ(display.lastImageY, kPanelH - 1 - 10 - 8);

  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawImage(bitmap, 10, 20, 8, 8);
  EXPECT_EQ(display.lastImageX, 10);
  EXPECT_EQ(display.lastImageY, 20);
}

TEST_F(GfxRendererTest, DrawIconPlotsInkPixelsThroughTheOrientationTransform) {
  // 8x8, 1bpp, bit == 0 means ink: only the first row carries ink.
  const uint8_t icon[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.drawIcon(icon, 0, 0, 8);
  EXPECT_EQ(inkCount(), 8u);
  // row 0 maps to logical x = size - 1 - row = 7, col -> y.
  for (int col = 0; col < 8; col++) EXPECT_TRUE(ink(7, col)) << "col " << col;
}

// ---------------------------------------------------------------------------
// Framebuffer loan
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, FrameBufferLoanReleasesAndRestoresTheBuffer) {
  ASSERT_TRUE(renderer.hasFrameBuffer());
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    EXPECT_FALSE(renderer.hasFrameBuffer());
    EXPECT_TRUE(display.isLent());
  }
  EXPECT_TRUE(renderer.hasFrameBuffer());
  EXPECT_FALSE(display.isLent());
  EXPECT_EQ(ESP.restartCount, 0);
}

TEST_F(GfxRendererTest, NestedFrameBufferLoansStayInert) {
  GfxRenderer::FrameBufferLoan outer(renderer);
  ASSERT_FALSE(renderer.hasFrameBuffer());
  {
    GfxRenderer::FrameBufferLoan inner(renderer);
    EXPECT_FALSE(renderer.hasFrameBuffer());
  }
  EXPECT_FALSE(renderer.hasFrameBuffer()) << "the inner loan must not return the outer loan's storage";
  outer.end();
  EXPECT_TRUE(renderer.hasFrameBuffer());
}

// ---------------------------------------------------------------------------
// Font registration
// ---------------------------------------------------------------------------

TEST_F(GfxRendererTest, DuplicateFontIdsAreIgnoredAndRemovalDropsTheEntry) {
  EXPECT_EQ(renderer.getFontMap().size(), 3u);
  renderer.insertFont(kTiny, testfonts::cjkFamily());
  EXPECT_EQ(renderer.getFontMap().size(), 3u);
  EXPECT_EQ(renderer.getTextWidth(kTiny, "A"), 2) << "the first registration must win";
  renderer.removeFont(kTiny);
  EXPECT_EQ(renderer.getFontMap().size(), 2u);
  EXPECT_EQ(renderer.getTextWidth(kTiny, "A"), 0);
}

TEST_F(GfxRendererTest, SdCardFontScalesDefaultToUnityAndAreClearable) {
  EXPECT_FALSE(renderer.isSdCardFont(kTiny));
  EXPECT_EQ(renderer.getSdCardFontScale(kTiny), 256);
  renderer.registerSdCardFontScale(kTiny, 512);
  EXPECT_EQ(renderer.getSdCardFontScale(kTiny), 512);
  renderer.clearSdCardFontScales();
  EXPECT_EQ(renderer.getSdCardFontScale(kTiny), 256);
}

// ---------------------------------------------------------------------------
// Hostile glyph metrics: the decode must stay inside the glyph's own bitmap
// (FR-116). An SD-card font's glyph record comes from an untrusted file, and
// nothing in the format ties width/height to dataLength.
// ---------------------------------------------------------------------------

namespace {

// One-glyph font ('A') whose declared dataLength is deliberately smaller than
// its width x height needs. `bufferBytes` sizes the actual allocation
// independently: give it padding to make the over-read deterministic, or make
// it exactly `declaredLength` so an over-read is a true heap overflow ASan can
// trap.
class LyingGlyphFont {
 public:
  LyingGlyphFont(uint8_t width, uint8_t height, uint16_t declaredLength, size_t bufferBytes, bool is2Bit, uint8_t fill)
      : bitmap_(bufferBytes, fill) {
    // top == ascender, so glyph row 0 always lands on the drawText y whatever
    // height the record claims.
    glyph_ = {width, height, 0x40, 0, kAscender, declaredLength, 0};
    interval_ = {0x0041, 0x0041, 0};
    data_.bitmap = bitmap_.data();
    data_.glyph = &glyph_;
    data_.intervals = &interval_;
    data_.intervalCount = 1;
    data_.advanceY = 20;
    data_.ascender = kAscender;
    data_.descender = -4;
    data_.is2Bit = is2Bit;
  }

  static constexpr int16_t kAscender = 16;
  LyingGlyphFont(const LyingGlyphFont&) = delete;
  LyingGlyphFont& operator=(const LyingGlyphFont&) = delete;

  EpdFontFamily family() const { return EpdFontFamily(&font_); }

 private:
  std::vector<uint8_t> bitmap_;
  EpdGlyph glyph_{};
  EpdUnicodeInterval interval_{};
  EpdFontData data_{};
  EpdFont font_{&data_};
};

constexpr int kLiar = 4;
// drawText's y is the top of the line; with top == ascender the glyph's row 0
// lands exactly there.
constexpr int kTopY = 40;

}  // namespace

TEST_F(GfxRendererTest, GlyphClaimingMoreRowsThanItsDataPaintsOnlyBackedRows) {
  // 1-bit, 8x8 = 8 bytes of ink claimed, 1 byte declared. The allocation holds
  // 8 solid bytes, so an unclamped decode would paint all 8 rows.
  LyingGlyphFont liar(8, 8, 1, 8, false, 0xFF);
  renderer.insertFont(kLiar, liar.family());
  renderer.drawText(kLiar, 0, kTopY, "A");

  EXPECT_EQ(inkCount(), 8u) << "only the single row the declared dataLength backs may be painted";
  for (int x = 0; x < 8; x++) {
    EXPECT_TRUE(ink(x, kTopY)) << "x=" << x;
  }
  for (int y = kTopY + 1; y < kTopY + 8; y++) {
    for (int x = 0; x < 8; x++) {
      EXPECT_FALSE(ink(x, y)) << "row " << y << " is not backed by bitmap data";
    }
  }
}

TEST_F(GfxRendererTest, TwoBitGlyphClaimingMoreRowsThanItsDataPaintsOnlyBackedRows) {
  // 2-bit, 8x8 needs 16 bytes; 4 declared == 16 pixels == 2 rows.
  LyingGlyphFont liar(8, 8, 4, 16, true, 0xFF);
  renderer.insertFont(kLiar, liar.family());
  renderer.drawText(kLiar, 0, kTopY, "A");

  EXPECT_EQ(inkCount(), 16u) << "2 bpp: 4 declared bytes back exactly two 8-pixel rows";
}

TEST_F(GfxRendererTest, GlyphDecodeStaysInsideItsOwnBitmapAllocation) {
  // The allocation is exactly the declared length, so reading even one byte
  // past it is a heap overflow (--asan traps; a plain run still sees the row
  // clamp).
  LyingGlyphFont liar(16, 200, 2, 2, false, 0xFF);
  renderer.insertFont(kLiar, liar.family());
  renderer.drawText(kLiar, 0, kTopY, "A");

  EXPECT_EQ(inkCount(), 16u) << "2 bytes at 1 bpp back exactly one 16-pixel row";
}

TEST_F(GfxRendererTest, RotatedGlyphDecodeIsClampedToo) {
  LyingGlyphFont liar(8, 8, 1, 8, false, 0xFF);
  renderer.insertFont(kLiar, liar.family());
  renderer.drawTextRotated90CW(kLiar, 100, 100, "A");
  EXPECT_EQ(inkCount(), 8u) << "the rotated decode shares the clamp";
}

}  // namespace
