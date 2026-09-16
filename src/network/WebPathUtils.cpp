#include "WebPathUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace WebPathUtils {

std::string normalizeWebPath(std::string_view inputPath) {
  if (inputPath.empty() || inputPath == "/") {
    return "/";
  }
  std::string result = FsHelpers::normalisePath(std::string(inputPath));
  if (result.empty()) {
    return "/";
  }
  if (result[0] != '/') {
    result.insert(0, 1, '/');
  }
  if (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

bool isProtectedItemName(std::string_view name) {
  if (!name.empty() && name[0] == '.') {
    return true;
  }
  return std::any_of(std::begin(HIDDEN_ITEMS), std::end(HIDDEN_ITEMS),
                     [name](const char* item) { return name == item; });
}

WsStartParseResult parseWsStart(std::string_view msg, WsStartCommand& out) {
  constexpr std::string_view PREFIX = "START:";
  if (msg.substr(0, PREFIX.size()) != PREFIX) {
    return WsStartParseResult::NOT_START;
  }
  const size_t firstColon = msg.find(':', PREFIX.size());
  if (firstColon == std::string_view::npos) {
    return WsStartParseResult::MISSING_FIELDS;
  }
  const size_t secondColon = msg.find(':', firstColon + 1);
  if (secondColon == std::string_view::npos) {
    return WsStartParseResult::MISSING_FIELDS;
  }

  // Size: optional '+', then decimal digits only. The value is produced by String::toInt(),
  // i.e. atol() over the device's 32-bit long, which saturates at LONG_MAX instead of failing.
  // Saturating explicitly keeps the host and the device on the same answer for every token.
  constexpr size_t MAX_SIZE = 2147483647;  // LONG_MAX with a 32-bit long
  const std::string_view sizeToken = msg.substr(firstColon + 1, secondColon - firstColon - 1);
  const size_t digitStart = (!sizeToken.empty() && sizeToken[0] == '+') ? 1 : 0;
  if (sizeToken.size() <= digitStart) {
    return WsStartParseResult::INVALID_SIZE;
  }
  size_t size = 0;
  for (size_t i = digitStart; i < sizeToken.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(sizeToken[i]);
    if (!isdigit(c)) {
      return WsStartParseResult::INVALID_SIZE;
    }
    const size_t digit = c - '0';
    // Saturate but keep scanning, so a trailing non-digit still fails the whole token.
    size = size > (MAX_SIZE - digit) / 10 ? MAX_SIZE : size * 10 + digit;
  }

  out.fileName = std::string(msg.substr(PREFIX.size(), firstColon - PREFIX.size()));
  out.size = size;
  out.path = std::string(msg.substr(secondColon + 1));
  if (out.path.empty() || out.path[0] != '/') {
    out.path.insert(0, 1, '/');
  }
  if (out.path.size() > 1 && out.path.back() == '/') {
    out.path.pop_back();
  }
  out.filePath = out.path;
  if (out.filePath.back() != '/') {
    out.filePath += '/';
  }
  out.filePath += out.fileName;
  return WsStartParseResult::OK;
}

}  // namespace WebPathUtils
