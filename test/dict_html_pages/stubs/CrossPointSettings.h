#pragma once

// Host stub for the settings singleton: only the reader typography fields
// DictHtmlPages forwards to the parser.

#include <cstdint>

class CrossPointSettingsStub {
 public:
  static CrossPointSettingsStub& getInstance() {
    static CrossPointSettingsStub instance;
    return instance;
  }
  int getReaderFontId() const { return 1; }
  float getReaderLineCompression() const { return 1.0f; }
  bool extraParagraphSpacing = false;
  uint8_t paragraphAlignment = 0;
  bool hyphenationEnabled = false;
  bool focusReadingEnabled = false;
};

#define SETTINGS CrossPointSettingsStub::getInstance()
