#pragma once

#include <HalStorage.h>
#include <ImageConverterStubs.h>
#include <Print.h>

// Host stub: records the bytes it was handed and returns imgconv_host::jpeg.result.
class JpegToBmpConverter {
 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true) {
    return imgconv_host::run(imgconv_host::jpeg, jpegFile, bmpOut, 0, 0, crop);
  }
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight) {
    return imgconv_host::run(imgconv_host::jpeg, jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
  }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight) {
    return imgconv_host::run(imgconv_host::jpeg, jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
  }
};
