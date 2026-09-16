#pragma once

// Arduino stand-in for the settings/input host suite: the String surface the
// PersistableStore family and ArduinoJson need (see persistable_stores), plus
// a test-controlled millis() for MappedInputManager and HalTiltSensor, and
// <math.h> because the device Arduino.h brings fabsf() into scope.

#include <math.h>

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

struct EspHostStub {
  uint32_t getFreeHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;

// Host clock behind millis(); tests set it directly.
namespace arduino_host {
inline unsigned long& clock() {
  static unsigned long ms = 0;
  return ms;
}
}  // namespace arduino_host

inline unsigned long millis() { return arduino_host::clock(); }
inline void delay(unsigned long) {}
