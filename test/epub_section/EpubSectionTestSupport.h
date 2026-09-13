#pragma once

// Shared fixtures/helpers for the EPUB Section pagination-engine suite.

#include <Epub.h>  // stubs/Epub.h (test-controlled)
#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace sectest {

// ---- Section .bin format mirror (Section.cpp private constants) ------------
// Mirrors SECTION_FILE_VERSION in Section.cpp. Bump in lockstep when the
// production format version changes.
constexpr uint8_t kSectionFileVersion = 45;
constexpr uint8_t kIncompleteVersion = 0;
// Derived exactly as Section.cpp derives it: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t kPartialVersion = 0xFE - (kSectionFileVersion - 28);

// Header layout (write order in Section::writeSectionFileHeader):
constexpr size_t kOffVersion = 0;              // uint8_t
constexpr size_t kOffFontId = 1;               // int (4)
constexpr size_t kOffLineCompression = 5;      // float (4)
constexpr size_t kOffExtraSpacing = 9;         // bool (1)
constexpr size_t kOffAlignment = 10;           // uint8_t
constexpr size_t kOffViewportWidth = 11;       // uint16_t
constexpr size_t kOffViewportHeight = 13;      // uint16_t
constexpr size_t kOffHyphenation = 15;         // bool (1)
constexpr size_t kOffEmbeddedStyle = 16;       // bool (1)
constexpr size_t kOffImageRendering = 17;      // uint8_t
constexpr size_t kOffFocusReading = 18;        // bool (1)
constexpr size_t kOffPageCount = 19;           // uint16_t
constexpr size_t kOffLutOffset = 21;           // uint32_t
constexpr size_t kOffAnchorMapOffset = 25;     // uint32_t
constexpr size_t kOffParagraphLutOffset = 29;  // uint32_t
constexpr size_t kOffLiLutOffset = 33;         // uint32_t
constexpr size_t kOffVisibleLutOffset = 37;    // uint32_t
constexpr uint32_t kHeaderSize = 41;

// ---- Filesystem helpers ----------------------------------------------------

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

template <typename T>
T readAt(const std::string& bytes, size_t offset) {
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

template <typename T>
void pokeAt(std::string& bytes, size_t offset, const T& value) {
  std::memcpy(&bytes[offset], &value, sizeof(T));
}

// RAII temp directory (same pattern as test/fb2_common/Fb2TestSupport.h).
class TempDir {
 public:
  TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/tmp") + "/sectest.XXXXXX";
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

// ---- Render spec -----------------------------------------------------------
// With the fixed-metric GfxRenderer fake (8 px/char, 4 px space, 16 px lines),
// a 400x64 viewport gives 4 lines of ~6 words per page -- small deterministic
// pages so even modest fixtures span many pages.
inline ReaderRenderSpec makeSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 0;
  spec.lineCompression = 1.0f;
  spec.extraParagraphSpacing = false;
  spec.paragraphAlignment = 0;
  spec.viewportWidth = 400;
  spec.viewportHeight = 64;
  spec.hyphenationEnabled = false;
  spec.embeddedStyle = false;
  spec.imageRendering = 0;
  spec.focusReadingEnabled = false;
  return spec;
}

// ---- Chapter fixtures ------------------------------------------------------

constexpr const char* kSpineHref = "OEBPS/text/ch0.xhtml";

// Deterministic chapter: `paragraphs` paragraphs of `wordsPerParagraph` words.
// Paragraph i carries id="pp<i>"; word j of paragraph i renders as "p<i>x<j>".
inline std::string buildChapterHtml(const int paragraphs, const int wordsPerParagraph) {
  std::ostringstream html;
  html << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html><head><title>t</title></head><body>\n";
  for (int i = 0; i < paragraphs; i++) {
    html << "<p id=\"pp" << i << "\">";
    for (int j = 0; j < wordsPerParagraph; j++) {
      if (j > 0) html << ' ';
      html << 'p' << i << 'x' << j;
    }
    html << "</p>\n";
  }
  html << "</body></html>\n";
  return html.str();
}

// Epub stub wired to serve `html` as spine item 0, caching under `tmp`.
inline std::shared_ptr<Epub> makeEpub(const TempDir& tmp, const std::string& html) {
  auto epub = std::make_shared<Epub>();
  epub->cachePath = tmp.path() + "/epub_cache";
  epub->spine.push_back({kSpineHref});
  epub->items[kSpineHref] = html;
  return epub;
}

inline std::string sectionBinPath(const Epub& epub, const int spineIndex = 0) {
  return epub.getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin";
}

inline std::string sectionTmpPath(const Epub& epub, const int spineIndex = 0) {
  return sectionBinPath(epub, spineIndex) + ".part";
}

inline std::string htmlCachePath(const Epub& epub, const int spineIndex = 0) {
  return epub.getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
}

// ---- Page helpers ----------------------------------------------------------

inline std::vector<std::string> pageWords(const Page& page) {
  std::vector<std::string> words;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<const PageLine&>(*element).getBlock();
    if (!block) continue;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      words.emplace_back(block->wordText(i));
    }
  }
  return words;
}

// All words of pages [0, pageCount) as one stream, via Section::loadPage.
inline std::vector<std::string> allWords(Section& section, const int pageCount) {
  std::vector<std::string> words;
  for (int p = 0; p < pageCount; p++) {
    const auto page = section.loadPage(p);
    if (!page) return {};
    const auto pw = pageWords(*page);
    words.insert(words.end(), pw.begin(), pw.end());
  }
  return words;
}

}  // namespace sectest
