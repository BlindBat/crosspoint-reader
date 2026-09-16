#pragma once

// Minimal Arduino String stand-in for the KOSync client host suite (same
// surface as test/persistable_stores/stubs/Arduino.h): what PersistableStore,
// ObfuscationUtils, base64 and ArduinoJson 7.4.2 with
// ARDUINOJSON_ENABLE_ARDUINO_STRING=1 touch. Heap telemetry goes through
// lib/Platform/PlatformSeam.h, so there is no ESP object here.

#include <cstdint>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}  // NOLINT(google-explicit-constructor)
  String(const std::string& s) : value_(s) {}    // NOLINT(google-explicit-constructor)

  String& operator=(const char* s) {
    value_ = s ? s : "";
    return *this;
  }

  bool concat(const char* s) {
    if (s) value_ += s;
    return true;
  }

  const char* c_str() const { return value_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(value_.size()); }
  bool isEmpty() const { return value_.empty(); }

  const std::string& str() const { return value_; }  // test convenience

  friend bool operator==(const String& a, const char* b) { return a.value_ == (b ? b : ""); }
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
  friend bool operator!=(const String& a, const char* b) { return !(a == b); }

 private:
  std::string value_;
};
