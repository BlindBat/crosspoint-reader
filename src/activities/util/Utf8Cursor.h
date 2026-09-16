#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Byte-offset cursor movement and editing over a UTF-8 std::string. Offsets
// always land on code point boundaries; continuation bytes (10xxxxxx) are
// skipped in either direction.
namespace utf8_cursor {

inline size_t prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

inline size_t next(const std::string& s, size_t pos) {
  if (pos >= s.length()) return s.length();
  pos++;
  while (pos < s.length() && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

// Inserts `out` at the cursor and advances past it. Refused when the result
// would exceed maxLength bytes (0 = unlimited). A cursor past the end is
// pulled back to the end first.
inline bool insert(std::string& text, size_t& cursorPos, const char* out, const size_t maxLength) {
  if (!out || !*out) return false;
  const size_t n = strlen(out);
  if (maxLength != 0 && text.length() + n > maxLength) return false;
  if (cursorPos > text.length()) cursorPos = text.length();
  text.insert(cursorPos, out, n);
  cursorPos += n;
  return true;
}

// Removes the code point before the cursor. False when there is nothing to remove.
inline bool backspace(std::string& text, size_t& cursorPos) {
  if (text.empty() || cursorPos == 0) return false;
  const size_t start = prev(text, cursorPos);
  text.erase(start, cursorPos - start);
  cursorPos = start;
  return true;
}

}  // namespace utf8_cursor
