#pragma once

// Host stand-in for the ESP32 MD5Builder used by
// KOReaderCredentialStore::getMd5Password(), which the suite does not
// exercise for hash correctness.

#include <Arduino.h>

#include <string>

class MD5Builder {
 public:
  void begin() { data_.clear(); }
  void add(const char* s) { data_ += (s ? s : ""); }
  void calculate() {}
  String toString() const { return String(std::string("md5:") + data_); }

 private:
  std::string data_;
};
