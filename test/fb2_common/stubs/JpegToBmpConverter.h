#pragma once

// Recording pass-through stub for JpegToBmpConverter. Instead of decoding
// JPEG data it copies the input file verbatim to the output, so a test can
// assert the exact bytes the Fb2CoverExtractor base64-decoded end-to-end.
// Each call records which entry point ran and the requested target size.

#include <HalStorage.h>

#include <string>

struct JpegToBmpConverterStubState {
  int streamCalls = 0;
  int withSizeCalls = 0;
  int oneBitCalls = 0;
  int lastTargetMaxWidth = -1;
  int lastTargetMaxHeight = -1;
  size_t lastInputSize = 0;
  bool failNextConversion = false;

  static JpegToBmpConverterStubState& instance() {
    static JpegToBmpConverterStubState state;
    return state;
  }
  void reset() { *this = JpegToBmpConverterStubState{}; }
};

class JpegToBmpConverter {
  static bool copyStream(HalFile& in, HalFile& out) {
    auto& state = JpegToBmpConverterStubState::instance();
    state.lastInputSize = in.size();
    if (state.failNextConversion) {
      state.failNextConversion = false;
      return false;
    }
    uint8_t buffer[256];
    size_t bytesRead;
    while ((bytesRead = in.read(buffer, sizeof(buffer))) > 0) {
      if (out.write(buffer, bytesRead) != bytesRead) return false;
    }
    return true;
  }

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, HalFile& bmpOut, bool = true) {
    JpegToBmpConverterStubState::instance().streamCalls++;
    return copyStream(jpegFile, bmpOut);
  }
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, HalFile& bmpOut, int targetMaxWidth, int targetMaxHeight) {
    auto& state = JpegToBmpConverterStubState::instance();
    state.withSizeCalls++;
    state.lastTargetMaxWidth = targetMaxWidth;
    state.lastTargetMaxHeight = targetMaxHeight;
    return copyStream(jpegFile, bmpOut);
  }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, HalFile& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight) {
    auto& state = JpegToBmpConverterStubState::instance();
    state.oneBitCalls++;
    state.lastTargetMaxWidth = targetMaxWidth;
    state.lastTargetMaxHeight = targetMaxHeight;
    return copyStream(jpegFile, bmpOut);
  }
};
