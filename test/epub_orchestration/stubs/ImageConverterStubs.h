#pragma once

// Shared recorder behind the JPEG/PNG -> BMP converter stubs.
//
// The image codecs are out of scope for this suite; what matters is WHICH
// cover Epub picked and whether a failed decode removes its output. Each stub
// drains the input file so the test can assert on the extracted bytes, writes
// a short marker to the output stream, and returns a test-controlled result.

#include <HalStorage.h>
#include <Print.h>

#include <cstdint>
#include <string>

namespace imgconv_host {

struct Recorder {
  bool result = true;         // what the converter returns
  int calls = 0;              // number of conversions attempted
  std::string lastInput;      // bytes read out of the source file
  int lastTargetWidth = 0;    // 0 for the full-size (non-thumbnail) entry points
  int lastTargetHeight = 0;
  bool lastCrop = false;

  void reset() {
    result = true;
    calls = 0;
    lastInput.clear();
    lastTargetWidth = 0;
    lastTargetHeight = 0;
    lastCrop = false;
  }
};

inline Recorder jpeg;
inline Recorder png;

inline void resetAll() {
  jpeg.reset();
  png.reset();
}

inline bool run(Recorder& recorder, HalFile& in, Print& out, const int width, const int height, const bool crop) {
  recorder.calls++;
  recorder.lastTargetWidth = width;
  recorder.lastTargetHeight = height;
  recorder.lastCrop = crop;
  recorder.lastInput.clear();
  char buffer[256];
  for (int n = in.read(buffer, sizeof(buffer)); n > 0; n = in.read(buffer, sizeof(buffer))) {
    recorder.lastInput.append(buffer, static_cast<size_t>(n));
  }
  if (!recorder.result) return false;
  static constexpr uint8_t bmpMarker[] = {'B', 'M', 0, 0};
  out.write(bmpMarker, sizeof(bmpMarker));
  return true;
}

}  // namespace imgconv_host
