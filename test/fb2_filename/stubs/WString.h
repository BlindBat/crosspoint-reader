#pragma once

// Minimal Arduino String stand-in: just enough surface for the FsHelpers
// String-overload wrappers to compile and be exercised on the host.

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
