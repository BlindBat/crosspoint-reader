#pragma once

class FsFile;
class Print;

class PngToBmpConverter {
 public:
  // Convert PNG file to 1-bit BMP with Atkinson dithering, prescaled to fit target dimensions
  static bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
};
