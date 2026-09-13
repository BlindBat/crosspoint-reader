#pragma once

// Host stand-in for the Arduino String class with the API surface the
// WebDAV handler and FsHelpers actually use. Backed by std::string, so
// embedded NUL bytes survive inside the object (as they do in the Arduino
// String, which tracks an explicit length) while c_str() consumers stop at
// the first NUL -- exactly the mismatch some path tests pin.

#include <cstddef>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}  // NOLINT(google-explicit-constructor)
  String(const std::string& s) : value_(s) {}    // NOLINT(google-explicit-constructor)
  String(char c) : value_(1, c) {}               // NOLINT(google-explicit-constructor)
  explicit String(int v) : value_(std::to_string(v)) {}
  explicit String(unsigned int v) : value_(std::to_string(v)) {}
  explicit String(long v) : value_(std::to_string(v)) {}
  explicit String(unsigned long v) : value_(std::to_string(v)) {}

  const char* c_str() const { return value_.c_str(); }
  const std::string& str() const { return value_; }
  size_t length() const { return value_.length(); }
  bool isEmpty() const { return value_.empty(); }

  bool startsWith(const String& prefix) const { return value_.rfind(prefix.value_, 0) == 0; }
  bool endsWith(const String& suffix) const {
    return value_.size() >= suffix.value_.size() &&
           value_.compare(value_.size() - suffix.value_.size(), suffix.value_.size(), suffix.value_) == 0;
  }
  bool equals(const String& other) const { return value_ == other.value_; }
  bool equals(const char* other) const { return value_ == (other ? other : ""); }

  char charAt(size_t index) const { return index < value_.size() ? value_[index] : '\0'; }
  char operator[](size_t index) const { return charAt(index); }

  String substring(size_t from) const {
    if (from >= value_.size()) return String();
    return String(value_.substr(from));
  }
  String substring(size_t from, size_t to) const {
    if (from >= to || from >= value_.size()) return String();
    return String(value_.substr(from, to - from));
  }

  int indexOf(char c, size_t from = 0) const {
    const auto pos = value_.find(c, from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int indexOf(const char* s, size_t from = 0) const {
    const auto pos = value_.find(s, from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int lastIndexOf(char c) const {
    const auto pos = value_.rfind(c);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  String& operator+=(const String& other) {
    value_ += other.value_;
    return *this;
  }
  String& operator+=(const char* other) {
    value_ += (other ? other : "");
    return *this;
  }
  String& operator+=(char c) {
    value_ += c;
    return *this;
  }

  friend String operator+(String lhs, const String& rhs) {
    lhs += rhs;
    return lhs;
  }
  friend String operator+(String lhs, const char* rhs) {
    lhs += rhs;
    return lhs;
  }
  friend String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }

  friend bool operator==(const String& lhs, const String& rhs) { return lhs.value_ == rhs.value_; }
  friend bool operator==(const String& lhs, const char* rhs) { return lhs.value_ == (rhs ? rhs : ""); }
  friend bool operator==(const char* lhs, const String& rhs) { return rhs == lhs; }
  friend bool operator!=(const String& lhs, const String& rhs) { return !(lhs == rhs); }
  friend bool operator!=(const String& lhs, const char* rhs) { return !(lhs == rhs); }

 private:
  std::string value_;
};
