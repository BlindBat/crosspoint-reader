// Host tests for the image-render pixel writers and the dithering helpers
// they feed from:
//
//  - DirectPixelWriter (lib/Epub/Epub/converters/DirectPixelWriter.h):
//    branch-free framebuffer writer with the orientation transform collapsed
//    into linear coefficients, MSB-first 1-bit packing, and the strip-band
//    clip that doubles as the only bounds guard on the write.
//  - DirectCacheWriter (same header): bounds-checked 2bpp writer for the
//    streaming PixelCache band.
//  - DitherUtils.h applyBayerDither4Level and the BitmapHelpers quantizers /
//    error-diffusion ditherers (Atkinson 2-bit, Atkinson 1-bit,
//    Floyd-Steinberg serpentine).
//
// DirectPixelWriter is compiled against the minimal GfxRenderer stub in
// stubs/GfxRenderer.h; BitmapHelpers.cpp is the real production source.
// Error-diffusion expectations are hand-derived in comments next to each
// assertion.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "GfxRenderer/BitmapHelpers.h"

namespace {

// Tiny fake panel: 16 physical columns (2-byte stride), 8 physical rows.
constexpr int kPhyW = 16;
constexpr int kPhyH = 8;
constexpr int kStride = 2;
constexpr size_t kFrameBytes = static_cast<size_t>(kStride) * kPhyH;

GfxRenderer makeRenderer(uint8_t* target, GfxRenderer::Orientation orientation, GfxRenderer::RenderMode mode,
                         int originY = 0, int rows = kPhyH) {
  GfxRenderer r;
  r.writeTarget = target;
  r.writeOriginY = originY;
  r.writeRows = rows;
  r.renderMode = mode;
  r.orientation = orientation;
  r.panelWidth = kPhyW;
  r.panelHeight = kPhyH;
  r.panelWidthBytes = kStride;
  return r;
}

// Physical Y of logical (x, y), written out independently of the coefficient
// tables in DirectPixelWriter::init() so the band tests have a non-circular
// oracle.
int phyYOf(GfxRenderer::Orientation orientation, int x, int y) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      return (kPhyH - 1) - x;
    case GfxRenderer::LandscapeClockwise:
      return (kPhyH - 1) - y;
    case GfxRenderer::PortraitInverted:
      return x;
    default:  // LandscapeCounterClockwise: identity
      return y;
  }
}

// Logical width of a row for the given orientation on the fake panel.
int logicalWidthOf(GfxRenderer::Orientation orientation) {
  return (orientation == GfxRenderer::Portrait || orientation == GfxRenderer::PortraitInverted) ? kPhyH : kPhyW;
}

// --- DirectPixelWriter: packing and row-stride math ---

TEST(DirectPixelWriter, PortraitTransformPacksMsbFirst) {
  std::vector<uint8_t> fb(kFrameBytes, 0xFF);
  GfxRenderer r = makeRenderer(fb.data(), GfxRenderer::Portrait, GfxRenderer::BW);
  DirectPixelWriter w;
  w.init(r);

  // Portrait: phyX = y, phyY = (phyH-1) - x. Logical (x=2, y=3) ->
  // phy (3, 5) -> byte 5*2 + 3/8 = 10, bit 7-3 -> mask 0x10, cleared (black).
  w.beginRow(3);
  w.writePixel(2, 0);

  for (size_t i = 0; i < fb.size(); i++) {
    EXPECT_EQ(fb[i], i == 10 ? 0xEF : 0xFF) << "byte " << i;
  }
}

TEST(DirectPixelWriter, IdentityOrientationRowStrideMath) {
  std::vector<uint8_t> fb(kFrameBytes, 0xFF);
  GfxRenderer r = makeRenderer(fb.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW);
  DirectPixelWriter w;
  w.init(r);

  // Identity transform: phyX = x, phyY = y.
  w.beginRow(0);
  w.writePixel(0, 0);  // byte 0, mask 0x80
  w.beginRow(1);
  w.writePixel(9, 0);  // byte 1*2 + 9/8 = 3, bit 7-1 -> mask 0x40
  w.beginRow(7);
  w.writePixel(15, 0);  // byte 7*2 + 1 = 15, bit 7-7 -> mask 0x01

  EXPECT_EQ(fb[0], 0x7F);
  EXPECT_EQ(fb[3], 0xBF);
  EXPECT_EQ(fb[15], 0xFE);
  for (size_t i = 0; i < fb.size(); i++) {
    if (i != 0 && i != 3 && i != 15) EXPECT_EQ(fb[i], 0xFF) << "byte " << i;
  }
}

TEST(DirectPixelWriter, BwModeSkipsWhiteUnlessRequested) {
  std::vector<uint8_t> fb(kFrameBytes, 0x00);
  GfxRenderer r = makeRenderer(fb.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW);
  DirectPixelWriter w;
  w.init(r);
  w.beginRow(0);

  // Value 3 (white) with writeWhiteInBw=false: no write at all.
  w.writePixel(4, 3);
  for (uint8_t b : fb) EXPECT_EQ(b, 0x00);

  // Same pixel with writeWhiteInBw=true: white is drawn (bit set).
  w.writePixel(4, 3, true);
  EXPECT_EQ(fb[0], 0x08);  // bit 7-4

  // Values 0..2 all draw black in BW mode (bit cleared).
  std::vector<uint8_t> fb2(kFrameBytes, 0xFF);
  GfxRenderer r2 = makeRenderer(fb2.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW);
  w.init(r2);
  w.beginRow(0);
  w.writePixel(0, 0);
  w.writePixel(1, 1);
  w.writePixel(2, 2);
  EXPECT_EQ(fb2[0], 0x1F);  // top three bits cleared
}

TEST(DirectPixelWriter, GrayscalePlaneSelection) {
  // GRAYSCALE_MSB plane holds levels 1 and 2; GRAYSCALE_LSB holds level 1
  // only. Both write by setting the bit (state=false path).
  std::vector<uint8_t> msb(kFrameBytes, 0x00);
  GfxRenderer rMsb = makeRenderer(msb.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::GRAYSCALE_MSB);
  DirectPixelWriter w;
  w.init(rMsb);
  w.beginRow(0);
  for (int v = 0; v <= 3; v++) w.writePixel(v, static_cast<uint8_t>(v));
  EXPECT_EQ(msb[0], 0x60);  // x=1 (0x40) and x=2 (0x20)

  std::vector<uint8_t> lsb(kFrameBytes, 0x00);
  GfxRenderer rLsb = makeRenderer(lsb.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::GRAYSCALE_LSB);
  w.init(rLsb);
  w.beginRow(0);
  for (int v = 0; v <= 3; v++) w.writePixel(v, static_cast<uint8_t>(v));
  EXPECT_EQ(lsb[0], 0x40);  // x=1 only
}

// --- DirectPixelWriter: clipping / bounds ---

TEST(DirectPixelWriter, FullFrameClipDropsOutOfRangeRows) {
  // Guard rows on both sides of the frame catch any write that escapes the
  // clip in either direction.
  std::vector<uint8_t> raw(kFrameBytes + 2 * kStride, 0xAA);
  uint8_t* fb = raw.data() + kStride;
  GfxRenderer r = makeRenderer(fb, GfxRenderer::Portrait, GfxRenderer::BW);
  DirectPixelWriter w;
  w.init(r);

  // Portrait phyY = (phyH-1) - x: logical x=-1 -> phyY = 8 (below frame),
  // x=8 -> phyY = -1 (above frame). Both must be dropped.
  w.beginRow(0);
  w.writePixel(-1, 0);
  w.writePixel(8, 0);

  // Identity orientation: logical y directly out of range in both directions.
  GfxRenderer r2 = makeRenderer(fb, GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW);
  w.init(r2);
  w.beginRow(-1);
  w.writePixel(0, 0);
  w.beginRow(kPhyH);
  w.writePixel(0, 0);

  for (size_t i = 0; i < raw.size(); i++) EXPECT_EQ(raw[i], 0xAA) << "byte " << i;
}

TEST(DirectPixelWriter, StripBandClipsOffBandRows) {
  // Strip band: physical rows [4, 6) target a 2-row scratch. Guard rows
  // sit directly before and after the scratch, so a broken clip (accepting
  // sy == clipRows, or a negative sy) lands in a guard row and is caught.
  constexpr int kOriginY = 4;
  constexpr int kBandRows = 2;
  std::vector<uint8_t> raw((kBandRows + 2) * kStride, 0xFF);
  uint8_t* scratch = raw.data() + kStride;
  GfxRenderer r = makeRenderer(scratch, GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW, kOriginY, kBandRows);
  DirectPixelWriter w;
  w.init(r);

  for (int y = kOriginY - 1; y <= kOriginY + kBandRows; y++) {
    w.beginRow(y);
    w.writePixel(0, 0);
  }

  // Only band rows 0 and 1 of the scratch were written (bit 0x80 cleared).
  EXPECT_EQ(raw[0], 0xFF);  // guard row before (would catch sy = -1)
  EXPECT_EQ(raw[1], 0xFF);
  EXPECT_EQ(raw[2], 0x7F);  // y=4 -> scratch row 0
  EXPECT_EQ(raw[3], 0xFF);
  EXPECT_EQ(raw[4], 0x7F);  // y=5 -> scratch row 1
  EXPECT_EQ(raw[5], 0xFF);
  EXPECT_EQ(raw[6], 0xFF);  // guard row after (would catch sy = clipRows)
  EXPECT_EQ(raw[7], 0xFF);
}

TEST(DirectPixelWriter, StripBandExactBufferStaysInBounds) {
  // Same clip exercised against an exactly-sized scratch: ASan verifies no
  // write ever leaves the band allocation while every logical row of a
  // full-height image is pushed through.
  constexpr int kOriginY = 3;
  constexpr int kBandRows = 2;
  std::vector<uint8_t> scratch(static_cast<size_t>(kBandRows) * kStride, 0xFF);
  GfxRenderer r =
      makeRenderer(scratch.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW, kOriginY, kBandRows);
  DirectPixelWriter w;
  w.init(r);

  for (int y = 0; y < kPhyH; y++) {
    w.beginRow(y);
    for (int x = 0; x < kPhyW; x++) w.writePixel(x, 0);
  }

  // Every column of both band rows was blacked out.
  for (size_t i = 0; i < scratch.size(); i++) EXPECT_EQ(scratch[i], 0x00) << "byte " << i;
}

TEST(DirectPixelWriter, BandColRangeNarrowsPortraitRow) {
  std::vector<uint8_t> scratch(2 * kStride, 0xFF);
  GfxRenderer r = makeRenderer(scratch.data(), GfxRenderer::Portrait, GfxRenderer::BW, 4, 2);
  DirectPixelWriter w;
  w.init(r);
  w.beginRow(0);

  // Portrait phyY = (phyH-1) - x = 7 - x; band rows {4, 5} -> x in {2, 3}.
  int cs = -1;
  int ce = -1;
  w.bandColRange(0, kPhyH, cs, ce);
  EXPECT_EQ(cs, 2);
  EXPECT_EQ(ce, 4);

  // Same row with xBase=2: columns are offsets from logical x=2.
  w.bandColRange(2, 4, cs, ce);
  EXPECT_EQ(cs, 0);
  EXPECT_EQ(ce, 2);

  // Band entirely outside the logical x range collapses to empty.
  GfxRenderer rFar = makeRenderer(scratch.data(), GfxRenderer::Portrait, GfxRenderer::BW, 0, 2);
  w.init(rFar);
  w.beginRow(0);
  // Band rows {0, 1} -> x in {6, 7}; a window covering only x in [0, 4)
  // contains none of them.
  w.bandColRange(0, 4, cs, ce);
  EXPECT_EQ(cs, ce);
}

TEST(DirectPixelWriter, BandColRangeRowConstantOrientations) {
  std::vector<uint8_t> scratch(2 * kStride, 0xFF);
  // Identity orientation: phyY = y, constant across the row.
  GfxRenderer r = makeRenderer(scratch.data(), GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW, 4, 2);
  DirectPixelWriter w;
  w.init(r);

  int cs = -1;
  int ce = -1;
  w.beginRow(3);  // off band
  w.bandColRange(0, kPhyW, cs, ce);
  EXPECT_EQ(cs, 0);
  EXPECT_EQ(ce, 0);

  w.beginRow(4);  // in band: whole row survives
  w.bandColRange(0, kPhyW, cs, ce);
  EXPECT_EQ(cs, 0);
  EXPECT_EQ(ce, kPhyW);
}

TEST(DirectPixelWriter, BandColRangeMatchesWritePixelClip) {
  // Property check across every orientation and several band configs: a
  // column is inside [colStart, colEnd) exactly when writePixel()'s own
  // unsigned band test would accept its physical row. phyYOf() is the
  // independent oracle.
  const GfxRenderer::Orientation orientations[] = {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                                   GfxRenderer::PortraitInverted,
                                                   GfxRenderer::LandscapeCounterClockwise};
  const int bands[][2] = {{0, kPhyH}, {4, 2}, {2, 3}, {7, 1}};

  std::vector<uint8_t> scratch(kFrameBytes, 0xFF);
  for (GfxRenderer::Orientation o : orientations) {
    const int logicalW = logicalWidthOf(o);
    const int logicalH = (logicalW == kPhyW) ? kPhyH : kPhyW;
    for (const auto& band : bands) {
      GfxRenderer r = makeRenderer(scratch.data(), o, GfxRenderer::BW, band[0], band[1]);
      DirectPixelWriter w;
      w.init(r);
      for (int y = 0; y < logicalH; y++) {
        w.beginRow(y);
        int cs = -1;
        int ce = -1;
        w.bandColRange(0, logicalW, cs, ce);
        for (int x = 0; x < logicalW; x++) {
          const int sy = phyYOf(o, x, y) - band[0];
          const bool inBand = static_cast<unsigned>(sy) < static_cast<unsigned>(band[1]);
          EXPECT_EQ(cs <= x && x < ce, inBand)
              << "orientation " << o << " band [" << band[0] << ", +" << band[1] << ") x=" << x << " y=" << y;
        }
      }
    }
  }
}

// --- DirectCacheWriter ---

TEST(DirectCacheWriter, PacksTwoBitPixelsMsbFirst) {
  std::vector<uint8_t> buf(4, 0x00);  // 2 rows x 2 bytes (4 pixels each)
  DirectCacheWriter w;
  w.init(buf.data(), 2, 2, 0);

  w.beginRow(10, 10);  // localRow 0
  w.writePixel(0, 3);
  w.writePixel(1, 2);
  w.writePixel(2, 1);
  w.writePixel(3, 0);
  // MSB first: 11 10 01 00 = 0xE4.
  EXPECT_EQ(buf[0], 0xE4);

  // Value is masked to 2 bits: 0xFF writes 3 into bits 7-6 of byte 1.
  w.writePixel(4, 0xFF);
  EXPECT_EQ(buf[1], 0xC0);

  w.beginRow(11, 10);  // localRow 1 -> second row of the band
  w.writePixel(0, 1);
  EXPECT_EQ(buf[2], 0x40);
  EXPECT_EQ(buf[3], 0x00);
}

TEST(DirectCacheWriter, ReadModifyWriteTouchesOnlyAddressedField) {
  std::vector<uint8_t> buf(1, 0xFF);
  DirectCacheWriter w;
  w.init(buf.data(), 1, 1, 0);
  w.beginRow(0, 0);
  w.writePixel(1, 0);  // clear bits 5-4 only
  EXPECT_EQ(buf[0], 0xCF);
}

TEST(DirectCacheWriter, DropsOffBandRowsAndOutOfRangeColumns) {
  // Band: 2 rows x 1 byte, screen origin (8, 10). Guard bytes on both sides
  // of the band catch any write that escapes the checks.
  std::vector<uint8_t> raw(4, 0xAA);
  uint8_t* band = raw.data() + 1;
  DirectCacheWriter w;
  w.init(band, 1, 2, 8);

  w.beginRow(9, 10);  // localRow -1: row dropped
  w.writePixel(8, 3);
  w.beginRow(12, 10);  // localRow 2: row dropped
  w.writePixel(8, 3);

  w.beginRow(10, 10);   // valid row 0
  w.writePixel(7, 3);   // localX -1: column dropped
  w.writePixel(12, 3);  // localX 4 -> byteIdx 1 >= bytesPerRow: dropped

  for (size_t i = 0; i < raw.size(); i++) EXPECT_EQ(raw[i], 0xAA) << "byte " << i;

  // Valid writes still land after the dropped ones.
  w.writePixel(8, 3);
  EXPECT_EQ(raw[1], 0xEA);  // 0xAA with bits 7-6 set to 11
}

TEST(DirectCacheWriter, ExactBufferStaysInBounds) {
  // ASan check: sweep a screen region larger than the band on every side;
  // no write may leave the exactly-sized allocation.
  std::vector<uint8_t> buf(2, 0x00);  // 2 rows x 1 byte
  DirectCacheWriter w;
  w.init(buf.data(), 1, 2, 8);
  for (int y = 5; y < 15; y++) {
    w.beginRow(y, 10);
    for (int x = 0; x < 20; x++) w.writePixel(x, 3);
  }
  EXPECT_EQ(buf[0], 0xFF);
  EXPECT_EQ(buf[1], 0xFF);
}

// --- DitherUtils: ordered Bayer dithering ---

TEST(BayerDither, ThresholdsAndClamp) {
  // Cell (1, 0): bayer4x4[0][1] = 8 -> dither 0, so the raw quantization
  // thresholds 64/128/192 are exposed directly.
  EXPECT_EQ(applyBayerDither4Level(63, 1, 0), 0);
  EXPECT_EQ(applyBayerDither4Level(64, 1, 0), 1);
  EXPECT_EQ(applyBayerDither4Level(127, 1, 0), 1);
  EXPECT_EQ(applyBayerDither4Level(128, 1, 0), 2);
  EXPECT_EQ(applyBayerDither4Level(191, 1, 0), 2);
  EXPECT_EQ(applyBayerDither4Level(192, 1, 0), 3);

  // Cell (0, 0): bayer4x4[0][0] = 0 -> dither (0-8)*5 = -40.
  EXPECT_EQ(applyBayerDither4Level(103, 0, 0), 0);  // 103-40 = 63
  EXPECT_EQ(applyBayerDither4Level(104, 0, 0), 1);  // 104-40 = 64

  // Cell (2, 3): bayer4x4[3][2] = 13 -> dither +25.
  EXPECT_EQ(applyBayerDither4Level(166, 2, 3), 2);  // 166+25 = 191
  EXPECT_EQ(applyBayerDither4Level(167, 2, 3), 3);  // 167+25 = 192

  // Extreme inputs stay saturated after the dither offset.
  EXPECT_EQ(applyBayerDither4Level(10, 0, 0), 0);   // 10-40 clamps to 0
  EXPECT_EQ(applyBayerDither4Level(250, 2, 3), 3);  // 250+25 clamps to 255
}

TEST(BayerDither, MatrixIsPeriodicEveryFourPixels) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      for (int gray : {40, 90, 160, 220}) {
        EXPECT_EQ(applyBayerDither4Level(static_cast<uint8_t>(gray), x, y),
                  applyBayerDither4Level(static_cast<uint8_t>(gray), x + 4, y + 4));
      }
    }
  }
}

// --- BitmapHelpers: stateless quantizers ---

TEST(Quantizers, QuantizeSimpleThresholds) {
  // X4-tuned 4-level thresholds: 45 / 70 / 140.
  EXPECT_EQ(quantizeSimple(0), 0);
  EXPECT_EQ(quantizeSimple(44), 0);
  EXPECT_EQ(quantizeSimple(45), 1);
  EXPECT_EQ(quantizeSimple(69), 1);
  EXPECT_EQ(quantizeSimple(70), 2);
  EXPECT_EQ(quantizeSimple(139), 2);
  EXPECT_EQ(quantizeSimple(140), 3);
  EXPECT_EQ(quantizeSimple(255), 3);
}

TEST(Quantizers, QuantizeUsesSimplePathAndAdjustPixelIsIdentity) {
  // USE_NOISE_DITHERING and USE_BRIGHTNESS are compiled out, so quantize()
  // must match quantizeSimple() and adjustPixel() must be a pass-through.
  for (int gray : {0, 44, 45, 100, 139, 140, 255}) {
    EXPECT_EQ(quantize(gray, 3, 7), quantizeSimple(gray)) << "gray " << gray;
    EXPECT_EQ(adjustPixel(gray), gray) << "gray " << gray;
  }
}

TEST(Quantizers, Quantize1BitOriginThresholdAndExtremes) {
  // At (0, 0) the hash is 0 -> noise threshold 0 -> adjusted threshold
  // 128 + (0-128)/2 = 64.
  EXPECT_EQ(quantize1bit(63, 0, 0), 0);
  EXPECT_EQ(quantize1bit(64, 0, 0), 1);

  // The adjusted threshold always stays within [64, 192], so 255 maps to
  // white and 0 maps to black at every position.
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      EXPECT_EQ(quantize1bit(255, x, y), 1) << "(" << x << ", " << y << ")";
      EXPECT_EQ(quantize1bit(0, x, y), 0) << "(" << x << ", " << y << ")";
    }
  }
}

// --- BitmapHelpers: Atkinson error diffusion (2-bit path) ---

TEST(AtkinsonDitherer, KnownThreeByThreeInput) {
  // 3x3 image of constant gray 135, width 3. X4-tuned quantizer:
  //   <30 -> (0, qv 15), <50 -> (1, qv 30), <140 -> (2, qv 80),
  //   else (3, qv 210); error = (adjusted - qv) >> 3 (arithmetic shift,
  //   i.e. floor division by 8), 1/8 to each of 6 neighbors.
  // Buffers are width+4 = 7 wide; pixel x reads errorRow0[x+2].
  //
  // Row 0 (all error buffers zero):
  //  x=0: adj 135 -> q2, err (135-80)>>3 = 6.
  //       er0[3]=6 er0[4]=6 | er1[1]=6 er1[2]=6 er1[3]=6 | er2[2]=6
  //  x=1: adj 135+6=141 -> q3, err (141-210)>>3 = floor(-69/8) = -9.
  //       er0[4]=-3 er0[5]=-9 | er1[2]=-3 er1[3]=-3 er1[4]=-9 | er2[3]=-9
  //  x=2: adj 135-3=132 -> q2, err 52>>3 = 6.
  //       er1[3]=3 er1[4]=-3 er1[5]=6 | er2[4]=6
  //  out [2, 3, 2]; after nextRow: er0 = [0,6,-3,3,-3,6,0],
  //  er1 = [0,0,6,-9,6,0,0].
  //
  // Row 1:
  //  x=0: adj 135-3=132 -> q2, err 6.
  //       er0[3]=9 er0[4]=3 | er1[1]=6 er1[2]=12 er1[3]=-3 | er2[2]=6
  //  x=1: adj 135+9=144 -> q3, err floor(-66/8) = -9.
  //       er0[4]=-6 er0[5]=-3 | er1[2]=3 er1[3]=-12 er1[4]=-3 | er2[3]=-9
  //  x=2: adj 135-6=129 -> q2, err 49>>3 = 6.
  //       er1[3]=-6 er1[4]=3 er1[5]=6 | er2[4]=6
  //  out [2, 3, 2]; after nextRow: er0 = [0,6,3,-6,3,6,0],
  //  er1 = [0,0,6,-9,6,0,0].
  //
  // Row 2:
  //  x=0: adj 135+3=138 -> q2 (still < 140), err 58>>3 = 7. er0[3]=1 er0[4]=10
  //  x=1: adj 135+1=136 -> q2, err 56>>3 = 7. er0[4]=17
  //  x=2: adj 135+17=152 -> q3.
  //  out [2, 2, 3].
  const int expected[3][3] = {{2, 3, 2}, {2, 3, 2}, {2, 2, 3}};

  AtkinsonDitherer d(3);
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) {
      EXPECT_EQ(d.processPixel(135, x), expected[y][x]) << "(" << x << ", " << y << ")";
    }
    d.nextRow();
  }
}

TEST(AtkinsonDitherer, FreshPixelThresholds) {
  // With no accumulated error the first pixel exposes the raw thresholds
  // 30 / 50 / 140.
  const int inputs[] = {29, 30, 49, 50, 139, 140};
  const int expected[] = {0, 1, 1, 2, 2, 3};
  for (size_t i = 0; i < 6; i++) {
    AtkinsonDitherer d(4);
    EXPECT_EQ(d.processPixel(inputs[i], 0), expected[i]) << "gray " << inputs[i];
  }
}

TEST(AtkinsonDitherer, ClampsAdjustedValueBeforeQuantizing) {
  // Input far below 0 clamps to 0 -> q0 (qv 15), err (0-15)>>3 = -2, so the
  // next pixel sees 50-2 = 48 which sits in the [30, 50) band -> q1.
  AtkinsonDitherer d(4);
  EXPECT_EQ(d.processPixel(-1000, 0), 0);
  EXPECT_EQ(d.processPixel(50, 1), 1);

  // Input far above 255 clamps to 255 -> q3 (qv 210), err 45>>3 = 5, so the
  // next pixel sees 139+5 = 144 -> q3 instead of q2.
  AtkinsonDitherer d2(4);
  EXPECT_EQ(d2.processPixel(9000, 0), 3);
  EXPECT_EQ(d2.processPixel(139, 1), 3);
}

TEST(AtkinsonDitherer, ResetClearsAccumulatedError) {
  AtkinsonDitherer d(4);
  ASSERT_EQ(d.processPixel(255, 0), 3);  // err (255-210)>>3 = 5 spread ahead
  d.reset();
  // Without reset x=1 would see 139+5 = 144 -> q3; after reset it is q2.
  EXPECT_EQ(d.processPixel(139, 1), 2);
}

// --- BitmapHelpers: Atkinson error diffusion (1-bit path) ---

TEST(Atkinson1BitDitherer, KnownThreeByTwoInput) {
  // 3x2 image of constant gray 120, width 3. Threshold 128, quantized
  // values 0/255; error = (adjusted - qv) >> 3. adjustPixel() is identity
  // (USE_BRIGHTNESS is off).
  //
  // Row 0:
  //  x=0: adj 120 -> 0, err 120>>3 = 15.
  //       er0[3]=15 er0[4]=15 | er1[1]=15 er1[2]=15 er1[3]=15 | er2[2]=15
  //  x=1: adj 120+15=135 -> 1, err (135-255)>>3 = -15.
  //       er0[4]=0 | er1[2]=0 er1[3]=0 er1[4]=-15 | er2[3]=-15
  //  x=2: adj 120+0=120 -> 0, err 15.
  //       er1[3]=15 er1[4]=0 er1[5]=15 | er2[4]=15
  //  out [0, 1, 0]; after nextRow: er0 = [0,15,0,15,0,15,0],
  //  er1 = [0,0,15,-15,15,0,0].
  //
  // Row 1:
  //  x=0: adj 120+0=120 -> 0, err 15. er0[3]=15+15=30 er0[4]=15
  //  x=1: adj 120+30=150 -> 1, err (150-255)>>3 = floor(-105/8) = -14.
  //       er0[4]=1
  //  x=2: adj 120+1=121 -> 0.
  //  out [0, 1, 0].
  const int expected[2][3] = {{0, 1, 0}, {0, 1, 0}};

  Atkinson1BitDitherer d(3);
  for (int y = 0; y < 2; y++) {
    for (int x = 0; x < 3; x++) {
      EXPECT_EQ(d.processPixel(120, x), expected[y][x]) << "(" << x << ", " << y << ")";
    }
    d.nextRow();
  }
}

TEST(Atkinson1BitDitherer, ThresholdAndReset) {
  {
    Atkinson1BitDitherer d(4);
    EXPECT_EQ(d.processPixel(127, 0), 0);
  }
  {
    Atkinson1BitDitherer d(4);
    EXPECT_EQ(d.processPixel(128, 0), 1);
  }
  // 120 -> black with err 15 spread ahead; after reset() the next pixel at
  // 127 stays black instead of being pushed to 127+15 = 142 -> white.
  Atkinson1BitDitherer d(4);
  ASSERT_EQ(d.processPixel(120, 0), 0);
  d.reset();
  EXPECT_EQ(d.processPixel(127, 1), 0);
}

// --- BitmapHelpers: Floyd-Steinberg serpentine ---

TEST(FloydSteinbergDitherer, SerpentineTwoByTwoKnownInput) {
  // 2x2 image of constant gray 135, driven the way the converters do:
  // x ascending on every row, direction handled inside processPixel().
  //
  // Row 0 (forward): x=0 adj 135 -> q2 (qv 80), err 55; right neighbor gets
  // (55*7)>>4 = 24, so x=1 adj 159 -> q3. Next-row buffer picks up
  // [10, 17, 3] from x=0 and [-10, -16, -4] from x=1 (err -51), leaving
  // errorCurRow = [10, 7, -13, -4] after nextRow().
  //
  // Row 1 (reversed distribution): x=0 reads err[1] = 7 -> adj 142 -> q3;
  // x=1 reads err[2] = -13 -> adj 122 -> q2.
  FloydSteinbergDitherer d(2);
  EXPECT_FALSE(d.isReverseRow());
  EXPECT_EQ(d.processPixel(135, 0), 2);
  EXPECT_EQ(d.processPixel(135, 1), 3);
  d.nextRow();
  EXPECT_TRUE(d.isReverseRow());
  EXPECT_EQ(d.processPixel(135, 0), 3);
  EXPECT_EQ(d.processPixel(135, 1), 2);
  d.nextRow();
  EXPECT_FALSE(d.isReverseRow());
}

}  // namespace
