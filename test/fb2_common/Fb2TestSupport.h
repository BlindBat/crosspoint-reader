#pragma once

// Small filesystem helpers shared by the FB2 host test suites.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// Independent oracle for "chapter lengths partition the body": the summed byte
// spans ("<section" through "</section>") of the top-level sections of every
// reading body, measured from the source text itself. Reading bodies are the first
// <body> and any later one without a name attribute, the rule both parsers apply.
inline size_t topLevelSectionBytes(const std::string& xml) {
  size_t total = 0;
  size_t start = 0;
  int depth = 0;
  int bodies = 0;
  bool reading = false;
  for (size_t pos = xml.find('<'); pos != std::string::npos; pos = xml.find('<', pos + 1)) {
    if (xml.compare(pos, 5, "<body") == 0) {
      const size_t close = xml.find('>', pos);
      const std::string tag = xml.substr(pos, close - pos);
      bodies++;
      reading = bodies == 1 || tag.find(" name=") == std::string::npos;
    } else if (xml.compare(pos, 7, "</body>") == 0) {
      reading = false;
    } else if (reading && xml.compare(pos, 8, "<section") == 0 &&
               (xml[pos + 8] == '>' || xml[pos + 8] == ' ' || xml[pos + 8] == '/')) {
      if (depth++ == 0) start = pos;
    } else if (reading && xml.compare(pos, 10, "</section>") == 0 && depth > 0) {
      if (--depth == 0) total += pos + 10 - start;
    }
  }
  return total;
}

// Byte-for-byte FB2 book.bin v5 (specs/006-fb2-sd-chapter-lut/contracts/book-bin-v5.md), so
// tests can hand-build valid caches and then corrupt exactly one field. Every field is public
// and written as given: nothing is recomputed, so a test can make any of them lie.
struct V5Record {
  uint32_t titleOffset = 0;
  uint16_t titleLength = 0;
  uint32_t fileOffset = 0;
  uint32_t ownLength = 0;
  uint32_t cumulativeLength = 0;
  uint8_t level = 0;
  uint8_t flags = 0;
};

struct V5BookBin {
  uint8_t version = 5;
  std::string title = "Book";
  std::string author = "Author";
  std::string language = "en";
  std::string coverBinaryId;
  uint16_t chapterCount = 0;
  uint32_t titlesSize = 0;
  std::vector<V5Record> records;
  std::string titles;

  // A consistent file from {title, fileOffset, ownLength, level, derived} chapters.
  struct Chapter {
    std::string title;
    uint32_t fileOffset;
    uint32_t ownLength;
    uint8_t level;
    bool derived;
  };
  static V5BookBin from(const std::vector<Chapter>& chapters) {
    V5BookBin bin;
    uint32_t cumulative = 0;
    for (const auto& c : chapters) {
      V5Record r;
      r.titleOffset = static_cast<uint32_t>(bin.titles.size());
      r.titleLength = static_cast<uint16_t>(c.title.size());
      r.fileOffset = c.fileOffset;
      r.ownLength = c.ownLength;
      cumulative += c.ownLength;
      r.cumulativeLength = cumulative;
      r.level = c.level;
      r.flags = c.derived ? 1 : 0;
      bin.titles += c.title;
      bin.records.push_back(r);
    }
    bin.chapterCount = static_cast<uint16_t>(chapters.size());
    bin.titlesSize = static_cast<uint32_t>(bin.titles.size());
    return bin;
  }

  std::string encode() const {
    std::string out;
    auto pod = [&out](const auto value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    auto str = [&](const std::string& value) {
      pod(static_cast<uint32_t>(value.size()));
      out += value;
    };
    pod(version);
    str(title);
    str(author);
    str(language);
    str(coverBinaryId);
    pod(chapterCount);
    pod(titlesSize);
    for (const auto& r : records) {
      pod(r.titleOffset);
      pod(r.titleLength);
      pod(r.fileOffset);
      pod(r.ownLength);
      pod(r.cumulativeLength);
      pod(r.level);
      pod(r.flags);
    }
    out += titles;
    return out;
  }
};

}  // namespace fb2test
