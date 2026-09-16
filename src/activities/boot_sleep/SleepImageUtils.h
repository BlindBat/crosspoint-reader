#pragma once

#include <HalStorage.h>

#include <cstdint>

// Pure helpers behind SleepActivity: header validation that runs before any
// decode buffer is allocated, and the placement math for full-screen images.
namespace sleepimage {

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;  // fraction of the source width trimmed in total (half per side)
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

// Byte-wise field readers; a byte past EOF reads as 0 (readBE32 returns 0 for the whole field).
uint16_t readLE16(HalFile& file);
uint32_t readLE32(HalFile& file);
uint32_t readBE32(HalFile& file);

// Signature + IHDR check for a directory-scanned PNG: legal depth/colour type,
// <=2048x3072, <=2048*1536 px, not interlaced, compression and filter 0.
bool isValidPngHeader(HalFile& file);

// Centres a bitmapWidth x bitmapHeight image on a pageWidth x pageHeight page.
// Oversized images are scaled to fit; with crop set, the longer axis is cropped
// so the image fills the page instead.
BitmapPlacement calculateBitmapPlacement(int bitmapWidth, int bitmapHeight, int pageWidth, int pageHeight, bool crop);

// Validates a 32-bit BGRA overlay BMP (BI_RGB or BI_BITFIELDS, 1..2048 x 1..3072)
// and leaves the file positioned at the pixel data.
bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, bool logErrors);

}  // namespace sleepimage
