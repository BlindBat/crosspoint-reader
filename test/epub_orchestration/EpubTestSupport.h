#pragma once

// Fixture helpers for the EPUB orchestration suite.
//
// Every EPUB is composed byte by byte here: a stored-method (uncompressed) ZIP
// writer plus small XML generators. Stored entries keep the fixtures readable
// and let a test corrupt any field — a lying size, a bad EOCD offset, a
// truncated central directory — without a compressor in the way.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace epubtest {

// --- ZIP ------------------------------------------------------------------

inline uint32_t crc32Of(const std::string& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char ch : data) {
    crc ^= static_cast<uint8_t>(ch);
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

inline void putU16(std::string& out, const uint16_t value) {
  out.push_back(static_cast<char>(value & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

inline void putU32(std::string& out, const uint32_t value) {
  out.push_back(static_cast<char>(value & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

// Builds a ZIP archive with stored (method 0) entries.
class ZipBuilder {
 public:
  ZipBuilder& add(std::string name, std::string data) {
    Entry entry;
    entry.name = std::move(name);
    entry.data = std::move(data);
    entry.declaredCompressed = static_cast<uint32_t>(entry.data.size());
    entry.declaredUncompressed = entry.declaredCompressed;
    entry.crc = crc32Of(entry.data);
    entries_.push_back(std::move(entry));
    return *this;
  }

  // Central-directory sizes that disagree with the payload: the malformed-input
  // lever for "lying length field".
  ZipBuilder& addWithDeclaredSizes(std::string name, std::string data, const uint32_t declaredCompressed,
                                   const uint32_t declaredUncompressed) {
    add(std::move(name), std::move(data));
    entries_.back().declaredCompressed = declaredCompressed;
    entries_.back().declaredUncompressed = declaredUncompressed;
    return *this;
  }

  // Overrides the compression method recorded for the last added entry.
  ZipBuilder& withMethod(const uint16_t method) {
    entries_.back().method = method;
    return *this;
  }

  // Central-directory local-header offset that does not point at the last
  // added entry: the malformed-input lever for "lying offset field".
  ZipBuilder& withLocalHeaderOffset(const uint32_t offset) {
    entries_.back().offsetOverride = offset;
    entries_.back().hasOffsetOverride = true;
    return *this;
  }

  std::string build() const {
    std::string out;
    std::vector<uint32_t> offsets;
    offsets.reserve(entries_.size());

    for (const auto& entry : entries_) {
      offsets.push_back(static_cast<uint32_t>(out.size()));
      putU32(out, 0x04034b50);
      putU16(out, 20);
      putU16(out, 0);
      putU16(out, entry.method);
      putU16(out, 0);
      putU16(out, 0);
      putU32(out, entry.crc);
      putU32(out, entry.declaredCompressed);
      putU32(out, entry.declaredUncompressed);
      putU16(out, static_cast<uint16_t>(entry.name.size()));
      putU16(out, 0);
      out += entry.name;
      out += entry.data;
    }

    const auto centralDirOffset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < entries_.size(); i++) {
      const auto& entry = entries_[i];
      putU32(out, 0x02014b50);
      putU16(out, 20);
      putU16(out, 20);
      putU16(out, 0);
      putU16(out, entry.method);
      putU16(out, 0);
      putU16(out, 0);
      putU32(out, entry.crc);
      putU32(out, entry.declaredCompressed);
      putU32(out, entry.declaredUncompressed);
      putU16(out, static_cast<uint16_t>(entry.name.size()));
      putU16(out, 0);
      putU16(out, 0);
      putU16(out, 0);
      putU16(out, 0);
      putU32(out, 0);
      putU32(out, entry.hasOffsetOverride ? entry.offsetOverride : offsets[i]);
      out += entry.name;
    }
    const auto centralDirSize = static_cast<uint32_t>(out.size()) - centralDirOffset;

    putU32(out, 0x06054b50);
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, static_cast<uint16_t>(entries_.size()));
    putU16(out, static_cast<uint16_t>(entries_.size()));
    putU32(out, centralDirSize);
    putU32(out, centralDirOffset);
    putU16(out, 0);
    return out;
  }

 private:
  struct Entry {
    std::string name;
    std::string data;
    uint32_t crc = 0;
    uint32_t declaredCompressed = 0;
    uint32_t declaredUncompressed = 0;
    uint32_t offsetOverride = 0;
    bool hasOffsetOverride = false;
    uint16_t method = 0;  // stored
  };
  std::vector<Entry> entries_;
};

// --- filesystem -----------------------------------------------------------

inline bool pathExists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

inline void removeTree(const std::string& path) {
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0) return;
  if (!S_ISDIR(st.st_mode)) {
    ::remove(path.c_str());
    return;
  }
  if (DIR* dir = ::opendir(path.c_str())) {
    while (const dirent* entry = ::readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      removeTree(path + "/" + name);
    }
    ::closedir(dir);
  }
  ::rmdir(path.c_str());
}

inline void writeBytes(const std::string& path, const std::string& bytes) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) return;
  std::fwrite(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);
}

inline std::string readBytes(const std::string& path) {
  std::string out;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return out;
  char buffer[4096];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) out.append(buffer, n);
  std::fclose(file);
  return out;
}

// Self-cleaning scratch directory: the EPUB file and the .crosspoint cache both
// live here, so a test never touches a real SD card or a shared corpus.
class TempDir {
 public:
  TempDir() {
    char pattern[] = "/tmp/cp_epub_orch_XXXXXX";
    const char* made = ::mkdtemp(pattern);
    path_ = made ? made : "";
  }
  ~TempDir() { removeTree(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::string& path() const { return path_; }
  std::string at(const std::string& relative) const { return path_ + "/" + relative; }

 private:
  std::string path_;
};

// --- EPUB XML -------------------------------------------------------------

inline std::string containerXml(const std::string& opfPath, const char* mediaType = "application/oebps-package+xml") {
  return std::string(
             "<?xml version=\"1.0\"?>\n"
             "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
             "  <rootfiles>\n"
             "    <rootfile full-path=\"") +
         opfPath + "\" media-type=\"" + mediaType +
         "\"/>\n"
         "  </rootfiles>\n"
         "</container>\n";
}

struct ManifestItem {
  std::string id;
  std::string href;
  std::string mediaType;
  std::string properties;
};

struct OpfSpec {
  std::string title = "Test Book";
  std::vector<std::string> authors{"Test Author"};
  std::string language = "en";
  std::string metaExtra;  // extra <meta .../> lines inside <metadata>
  std::vector<ManifestItem> manifest;
  std::vector<std::string> spine;  // idrefs, in reading order
  std::string guide;               // full <guide>...</guide> block, or empty
  bool omitSpine = false;
};

inline std::string opfXml(const OpfSpec& spec) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n"
      "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
  out += "    <dc:title>" + spec.title + "</dc:title>\n";
  for (const auto& author : spec.authors) out += "    <dc:creator>" + author + "</dc:creator>\n";
  out += "    <dc:language>" + spec.language + "</dc:language>\n";
  out += spec.metaExtra;
  out += "  </metadata>\n  <manifest>\n";
  for (const auto& item : spec.manifest) {
    out += "    <item id=\"" + item.id + "\" href=\"" + item.href + "\" media-type=\"" + item.mediaType + "\"";
    if (!item.properties.empty()) out += " properties=\"" + item.properties + "\"";
    out += "/>\n";
  }
  out += "  </manifest>\n";
  if (!spec.omitSpine) {
    out += "  <spine toc=\"ncx\">\n";
    for (const auto& idref : spec.spine) out += "    <itemref idref=\"" + idref + "\"/>\n";
    out += "  </spine>\n";
  }
  out += spec.guide;
  out += "</package>\n";
  return out;
}

struct TocLink {
  std::string label;
  std::string href;  // relative to the nav/ncx document
};

inline std::string navXhtml(const std::vector<TocLink>& links) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
      "<body>\n<nav epub:type=\"toc\">\n<ol>\n";
  for (const auto& link : links) {
    out += "<li><a href=\"" + link.href + "\">" + link.label + "</a></li>\n";
  }
  out += "</ol>\n</nav>\n</body>\n</html>\n";
  return out;
}

inline std::string ncxXml(const std::vector<TocLink>& links) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
      "<navMap>\n";
  int order = 1;
  for (const auto& link : links) {
    out += "<navPoint id=\"np" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) + "\">\n";
    out += "<navLabel><text>" + link.label + "</text></navLabel>\n";
    out += "<content src=\"" + link.href + "\"/>\n</navPoint>\n";
    order++;
  }
  out += "</navMap>\n</ncx>\n";
  return out;
}

inline std::string chapterXhtml(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>" + body +
         "</p></body></html>\n";
}

}  // namespace epubtest
