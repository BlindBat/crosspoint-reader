#pragma once

// Host stand-in: the Rect QrUtils takes as its bounding box (src/components/themes/BaseTheme.h).
struct Rect {
  int x;
  int y;
  int width;
  int height;

  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
};
