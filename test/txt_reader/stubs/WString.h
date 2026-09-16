#pragma once

// Minimal Arduino String stand-in so the FsHelpers String overloads compile on
// the host (same shape as test/fb2_filename/stubs/WString.h).

#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}  // NOLINT(google-explicit-constructor)
  const char* c_str() const { return value.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(value.size()); }

 private:
  std::string value;
};
