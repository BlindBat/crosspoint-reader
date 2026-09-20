#pragma once

// Small filesystem helpers shared by the FB2 host test suites.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef FB2_FIXTURE_DIR
#error "FB2_FIXTURE_DIR must be defined by the build system"
#endif

namespace fb2test {

inline std::string fixturePath(const std::string& name) { return std::string(FB2_FIXTURE_DIR) + "/" + name; }

inline std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

inline bool writeAll(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return out.good();
}

inline bool fileExists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

// RAII temp directory. Created under the system temp dir, removed recursively.
class TempDir {
 public:
  TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/tmp") + "/fb2test.XXXXXX";
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", tmpl.c_str());
    path_ = ::mkdtemp(buffer) ? buffer : "";
  }
  ~TempDir() {
    if (!path_.empty()) {
      const std::string cmd = "rm -rf '" + path_ + "'";
      [[maybe_unused]] const int rc = std::system(cmd.c_str());
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }
  bool valid() const { return !path_.empty(); }

 private:
  std::string path_;
};

// Deterministic FB2 body of `sectionCount` sections, emitted as sibling chains
// `depth` levels deep (so 1100 sections at depth 2 give 550 two-level towers,
// and 64 at depth 64 give one 64-deep chain). Section i carries the word "wordI"
// and a title long enough to exceed std::string's small-buffer optimisation, so
// allocation-counting tests see one heap allocation per stored title.
inline std::string makeSectionTowerFb2(const int sectionCount, const int chainDepth) {
  const int depth = chainDepth < 1 ? 1 : chainDepth;
  std::string out =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Tower</book-title><lang>en</lang></title-info></description>\n<body>\n";
  int emitted = 0;
  while (emitted < sectionCount) {
    const int chain = (sectionCount - emitted) < depth ? (sectionCount - emitted) : depth;
    for (int i = 0; i < chain; i++) {
      const std::string n = std::to_string(emitted + i);
      out += "<section><title><p>Section number " + n + " of the tower</p></title><p>word" + n + "</p>";
    }
    for (int i = 0; i < chain; i++) {
      out += "</section>";
    }
    out += "\n";
    emitted += chain;
  }
  out += "</body>\n</FictionBook>\n";
  return out;
}

// Deterministic FB2 body of `sectionCount` sections that carry NO <title>, so the
// label-derivation rules (contract L1-L8) can be exercised. Emitted as sibling
// chains `chainDepth` levels deep, like makeSectionTowerFb2. Section 0 opens with a
// long multi-byte paragraph (for the character-boundary cap, L6), section 1 is
// title-less AND text-less (for the placeholder fallback, L7), and every other
// section opens with "Body of section N" followed by a second paragraph that must
// never reach the label (L4).
inline std::string makeUntitledSectionsFb2(const int sectionCount, const int chainDepth) {
  const int depth = chainDepth < 1 ? 1 : chainDepth;
  std::string out =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Untitled</book-title><lang>ru</lang></title-info></description>\n"
      "<body>\n";
  int emitted = 0;
  while (emitted < sectionCount) {
    const int chain = (sectionCount - emitted) < depth ? (sectionCount - emitted) : depth;
    for (int i = 0; i < chain; i++) {
      const int index = emitted + i;
      const std::string n = std::to_string(index);
      out += "<section>";
      if (index == 0) {
        // 80 Cyrillic characters (160 bytes in UTF-8): longer than the 64-character
        // label cap, so a byte-wise cut would land mid-character.
        out += "<p>";
        for (int c = 0; c < 40; c++) out += "ЯЁ";
        out += "</p>";
      } else if (index != 1) {
        out += "<p>Body of section " + n + "</p><p>Second paragraph " + n + "</p>";
      }
      // index == 1 stays empty: no title, no text.
    }
    for (int i = 0; i < chain; i++) {
      out += "</section>";
    }
    out += "\n";
    emitted += chain;
  }
  out += "</body>\n</FictionBook>\n";
  return out;
}

// Like makeSectionTowerFb2 but with titles under the 15-character small-buffer
// threshold, so an allocation budget can separate the per-chapter struct cost from
// the heap block a longer title adds.
inline std::string makeShortTitleTowerFb2(const int sectionCount) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Short</book-title><lang>en</lang></title-info></description>\n"
      "<body>\n";
  for (int i = 0; i < sectionCount; i++) {
    const std::string n = std::to_string(i);
    out += "<section><title><p>T" + n + "</p></title><p>w" + n + "</p></section>\n";
  }
  out += "</body>\n</FictionBook>\n";
  return out;
}

}  // namespace fb2test
