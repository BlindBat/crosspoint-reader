#pragma once

// Host stand-in for lib/GfxRenderer/GfxRenderer.h: only the live orientation
// and the logical screen geometry MappedInputManager reads. Orientation
// values mirror the production enum order (mapScreenDirection indexes by it).

class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  // --- test controls -------------------------------------------------------
  Orientation orientation = Portrait;
  int width = 480;
  int height = 800;

  Orientation getOrientation() const { return orientation; }
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void tapToLogical(const float nx, const float ny, int& outX, int& outY) const {
    outX = static_cast<int>(nx * static_cast<float>(width));
    outY = static_cast<int>(ny * static_cast<float>(height));
  }
};
