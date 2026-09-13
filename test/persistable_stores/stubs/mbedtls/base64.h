#pragma once

// Host stand-in for mbedtls_base64_decode with the exact contract
// ObfuscationUtils relies on:
//  - dst == NULL (or dlen too small): *olen = required size, return
//    MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL. Input is still validated first, so
//    a corrupt value fails the size query (INVALID_CHARACTER) exactly like
//    the real mbedtls implementation.
//  - invalid characters / lengths: MBEDTLS_ERR_BASE64_INVALID_CHARACTER.
//  - success: decode, set *olen, return 0.

#include <cstddef>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL (-0x002A)
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER (-0x002C)

namespace mbedtls_base64_stub_detail {

inline int decodeValue(const unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace mbedtls_base64_stub_detail

inline int mbedtls_base64_decode(unsigned char* dst, const size_t dlen, size_t* olen, const unsigned char* src,
                                 const size_t slen) {
  using mbedtls_base64_stub_detail::decodeValue;

  if (slen == 0) {
    *olen = 0;
    return 0;
  }
  if (slen % 4 != 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

  size_t pad = 0;
  for (size_t i = 0; i < slen; i++) {
    const unsigned char c = src[i];
    if (c == '=') {
      // Padding only allowed in the final two positions.
      if (i + 2 < slen) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
      pad++;
      continue;
    }
    if (pad > 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;  // data after '='
    if (decodeValue(c) < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
  }
  if (pad > 2) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

  const size_t needed = (slen / 4) * 3 - pad;
  if (dst == nullptr || dlen < needed) {
    *olen = needed;
    return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  }

  size_t out = 0;
  for (size_t i = 0; i < slen; i += 4) {
    int v[4] = {0, 0, 0, 0};
    int chars = 0;
    for (int k = 0; k < 4; k++) {
      if (src[i + k] == '=') break;
      v[k] = decodeValue(src[i + k]);
      chars++;
    }
    const unsigned int triple = (static_cast<unsigned int>(v[0]) << 18) | (static_cast<unsigned int>(v[1]) << 12) |
                                (static_cast<unsigned int>(v[2]) << 6) | static_cast<unsigned int>(v[3]);
    if (chars >= 2 && out < needed) dst[out++] = static_cast<unsigned char>((triple >> 16) & 0xFF);
    if (chars >= 3 && out < needed) dst[out++] = static_cast<unsigned char>((triple >> 8) & 0xFF);
    if (chars >= 4 && out < needed) dst[out++] = static_cast<unsigned char>(triple & 0xFF);
  }
  *olen = out;
  return 0;
}
