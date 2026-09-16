#pragma once

#include <FsHelpers.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Pure helpers behind the web server and WebDAV handler; no Wi-Fi or SD dependencies.
namespace WebPathUtils {

// Folders/files hidden from the web file browser and refused as targets.
// Dot-prefixed names are hidden implicitly (see isProtectedItemName()).
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};

// Collapse a client-supplied path to an absolute "/"-rooted form without a trailing slash.
std::string normalizeWebPath(std::string_view inputPath);

// True for dot-prefixed names and every HIDDEN_ITEMS entry (exact, case-sensitive).
bool isProtectedItemName(std::string_view name);

// Parsed WebSocket upload command "START:<filename>:<size>:<path>".
struct WsStartCommand {
  std::string fileName;
  size_t size = 0;       // saturates at LONG_MAX, matching String::toInt() on the device
  std::string path;      // leading "/" ensured, one trailing "/" stripped
  std::string filePath;  // path joined with fileName
};

enum class WsStartParseResult : uint8_t {
  OK,
  NOT_START,       // missing "START:" prefix
  MISSING_FIELDS,  // fewer than two ':' separators after the prefix
  INVALID_SIZE,    // size token empty or not decimal digits
};

// The command is filled only on OK. The filename is everything up to the first ':' after
// the prefix; the path is everything after the second ':' (so it may itself contain ':').
WsStartParseResult parseWsStart(std::string_view msg, WsStartCommand& out);

// WebDAV request-header helpers (Class 1 server).
// Depth: "0" -> 0; "1", missing or "infinity" -> 1.
inline int davDepth(std::string_view header) { return header == "0" ? 0 : 1; }

// Overwrite: "F"/"f" -> false; anything else (including missing) -> true.
inline bool davOverwrite(std::string_view header) { return !(header == "F" || header == "f"); }

inline const char* mimeTypeForPath(std::string_view path) {
  if (FsHelpers::hasEpubExtension(path)) return "application/epub+zip";
  if (FsHelpers::checkFileExtension(path, ".pdf")) return "application/pdf";
  if (FsHelpers::hasTxtExtension(path)) return "text/plain";
  if (FsHelpers::checkFileExtension(path, ".html") || FsHelpers::checkFileExtension(path, ".htm")) return "text/html";
  if (FsHelpers::checkFileExtension(path, ".css")) return "text/css";
  if (FsHelpers::checkFileExtension(path, ".js")) return "application/javascript";
  if (FsHelpers::checkFileExtension(path, ".json")) return "application/json";
  if (FsHelpers::checkFileExtension(path, ".xml")) return "application/xml";
  if (FsHelpers::hasJpgExtension(path)) return "image/jpeg";
  if (FsHelpers::hasPngExtension(path)) return "image/png";
  if (FsHelpers::hasGifExtension(path)) return "image/gif";
  if (FsHelpers::checkFileExtension(path, ".svg")) return "image/svg+xml";
  if (FsHelpers::checkFileExtension(path, ".zip")) return "application/zip";
  if (FsHelpers::checkFileExtension(path, ".gz")) return "application/gzip";
  return "application/octet-stream";
}

}  // namespace WebPathUtils
