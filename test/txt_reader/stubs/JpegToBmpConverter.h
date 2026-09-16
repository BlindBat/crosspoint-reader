#pragma once

// Recording pass-through stub for JpegToBmpConverter (after
// test/fb2_common/stubs). Copies the input verbatim so Txt::generateCoverBmp
// can be asserted end-to-end without a JPEG decoder; records each call.

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

struct JpegToBmpConverterStubState {
  int streamCalls = 0;
  size_t lastInputSize = 0;
  bool failNextConversion = false;

  static JpegToBmpConverterStubState& instance() {
    static JpegToBmpConverterStubState state;
    return state;
  }
  void reset() { *this = JpegToBmpConverterStubState{}; }
};

class JpegToBmpConverter {
 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, HalFile& bmpOut, bool = true) {
    auto& state = JpegToBmpConverterStubState::instance();
    state.streamCalls++;
    state.lastInputSize = jpegFile.size();
    if (state.failNextConversion) {
      state.failNextConversion = false;
      return false;
    }
    uint8_t buffer[256];
    int bytesRead;
    while ((bytesRead = jpegFile.read(buffer, sizeof(buffer))) > 0) {
      if (bmpOut.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) return false;
    }
    return true;
  }
};
