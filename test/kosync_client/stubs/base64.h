#pragma once

// Host stand-in for the Arduino base64 helper used by ObfuscationUtils.
// Standard RFC 4648 encoding with padding, matching what the ESP32 core's
// base64::encode produces (the encoded form must survive a JSON round-trip
// and decode through the mbedtls stub).

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <string>

class base64 {
 public:
  // Overload set of the ESP32 core's base64 helper; the String form is what
  // KOReaderSyncClient reaches through when it encodes "user:password".
  static String encode(const String& text) {
    return encode(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
  }

  static String encode(const uint8_t* data, size_t len) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
      const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                         static_cast<uint32_t>(data[i + 2]);
      out += alphabet[(v >> 18) & 0x3F];
      out += alphabet[(v >> 12) & 0x3F];
      out += alphabet[(v >> 6) & 0x3F];
      out += alphabet[v & 0x3F];
    }
    const size_t rem = len - i;
    if (rem == 1) {
      const uint32_t v = static_cast<uint32_t>(data[i]) << 16;
      out += alphabet[(v >> 18) & 0x3F];
      out += alphabet[(v >> 12) & 0x3F];
      out += "==";
    } else if (rem == 2) {
      const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
      out += alphabet[(v >> 18) & 0x3F];
      out += alphabet[(v >> 12) & 0x3F];
      out += alphabet[(v >> 6) & 0x3F];
      out += '=';
    }
    return String(out);
  }
};
