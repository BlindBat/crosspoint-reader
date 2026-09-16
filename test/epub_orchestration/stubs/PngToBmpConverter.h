#pragma once

#include <HalStorage.h>
#include <ImageConverterStubs.h>
#include <Print.h>

// Host stub: records the bytes it was handed and returns imgconv_host::png.result.
class PngToBmpConverter {
 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true) {
    return imgconv_host::run(imgconv_host::png, pngFile, bmpOut, 0, 0, crop);
  }
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight) {
    return imgconv_host::run(imgconv_host::png, pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
  }
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight) {
    return imgconv_host::run(imgconv_host::png, pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
  }
};
