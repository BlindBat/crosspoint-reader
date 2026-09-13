#pragma once

// Host stub of lib/GfxRenderer/GfxRenderer.h scoped to exactly what
// DirectPixelWriter::init() reads: the active write target (framebuffer or
// strip-band scratch), render mode, orientation, and panel geometry. The
// real header drags in fonts, the display HAL, and the full renderer state;
// the writer under test only consumes these eight getters.
//
// Enum values (names and order) must match the real GfxRenderer so the
// orientation/render-mode switches in DirectPixelWriter.h compile against
// the same constants they see in firmware.

#include <cstdint>

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

  // Test-controlled state, set directly by the fixtures.
  uint8_t* writeTarget = nullptr;
  int writeOriginY = 0;
  int writeRows = 0;
  RenderMode renderMode = BW;
  Orientation orientation = Portrait;
  uint16_t panelWidth = 0;
  uint16_t panelHeight = 0;
  uint16_t panelWidthBytes = 0;

  uint8_t* getWriteTarget() const { return writeTarget; }
  int getWriteOriginY() const { return writeOriginY; }
  int getWriteRows() const { return writeRows; }
  RenderMode getRenderMode() const { return renderMode; }
  Orientation getOrientation() const { return orientation; }
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  uint16_t getDisplayWidthBytes() const { return panelWidthBytes; }
};
