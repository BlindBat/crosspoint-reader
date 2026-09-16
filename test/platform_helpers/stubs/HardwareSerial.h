#pragma once

#include "Print.h"

// logSerial stand-in: records everything printed so tests can inspect it.
class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return true; }
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* data, size_t length) override;
};

extern HardwareSerial Serial;
