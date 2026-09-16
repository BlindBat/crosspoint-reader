#pragma once

// Shared helpers for the package parser suite.
//
// Every parser under test is a Print sink that takes the total XML size up
// front and treats the write that consumes the last declared byte as the
// final expat chunk. feed() mirrors ZipFile::readFileToStream by pushing the
// document through in fixed chunks and reports the first short write, which
// is how the production caller detects a parse error.

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace parsertest {

// Streams `doc` into `sink` in `chunk`-byte writes. Returns true when every
// write was accepted in full (the parser never reported an error).
template <typename Sink>
bool feed(Sink& sink, const std::string& doc, const size_t chunk = 512) {
  const auto* data = reinterpret_cast<const uint8_t*>(doc.data());
  size_t offset = 0;
  while (offset < doc.size()) {
    const size_t n = doc.size() - offset < chunk ? doc.size() - offset : chunk;
    if (sink.write(data + offset, n) != n) return false;
    offset += n;
  }
  return true;
}

// Byte-at-a-time variant exercising the single-byte Print overload.
template <typename Sink>
bool feedBytewise(Sink& sink, const std::string& doc) {
  for (const char c : doc) {
    if (sink.write(static_cast<uint8_t>(c)) != 1) return false;
  }
  return true;
}

// `count` nested copies of <tag> wrapping `inner`; hostile-nesting fixtures.
inline std::string nest(const char* tag, const size_t count, const std::string& inner) {
  std::string out;
  out.reserve(count * 16 + inner.size());
  for (size_t i = 0; i < count; ++i) {
    out += '<';
    out += tag;
    out += '>';
  }
  out += inner;
  for (size_t i = 0; i < count; ++i) {
    out += "</";
    out += tag;
    out += '>';
  }
  return out;
}

// Per-test scratch directory (ContentOpfParser writes .items.bin under it).
class TempDir {
 public:
  TempDir() {
    char tmpl[] = "/tmp/cp_opf_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    EXPECT_NE(nullptr, dir);
    path_ = dir ? dir : "";
  }
  ~TempDir() {
    if (path_.empty()) return;
    ::remove((path_ + "/.items.bin").c_str());
    ::rmdir(path_.c_str());
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace parsertest
