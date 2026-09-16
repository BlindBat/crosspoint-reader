#pragma once

// Host MD5Builder with the ESP32 core's call shape (begin / add / calculate /
// toString) and a real RFC 1321 digest, so document-id and auth-key hashes can
// be asserted against known vectors. The per-round constants are derived with
// std::sin exactly as the RFC defines them, which keeps the table typo-free.

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

class MD5Builder {
 public:
  void begin() {
    a0_ = 0x67452301u;
    b0_ = 0xefcdab89u;
    c0_ = 0x98badcfeu;
    d0_ = 0x10325476u;
    totalLen_ = 0;
    bufLen_ = 0;
    std::memset(digest_, 0, sizeof(digest_));
  }

  void add(const uint8_t* data, size_t len) {
    totalLen_ += len;
    while (len > 0) {
      const size_t take = std::min(len, sizeof(buf_) - bufLen_);
      std::memcpy(buf_ + bufLen_, data, take);
      bufLen_ += take;
      data += take;
      len -= take;
      if (bufLen_ == sizeof(buf_)) {
        processBlock(buf_);
        bufLen_ = 0;
      }
    }
  }

  void add(const char* s) { add(reinterpret_cast<const uint8_t*>(s), s ? std::strlen(s) : 0); }

  void calculate() {
    const uint64_t bitLen = totalLen_ * 8u;
    const uint8_t pad = 0x80;
    add(&pad, 1);
    const uint8_t zero = 0;
    while (bufLen_ != 56) add(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; i++) lenBytes[i] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFF);
    add(lenBytes, 8);
    const uint32_t words[4] = {a0_, b0_, c0_, d0_};
    for (int w = 0; w < 4; w++) {
      for (int i = 0; i < 4; i++) digest_[w * 4 + i] = static_cast<uint8_t>((words[w] >> (8 * i)) & 0xFF);
    }
  }

  String toString() const {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (const uint8_t b : digest_) {
      out += hex[b >> 4];
      out += hex[b & 0x0F];
    }
    return String(out);
  }

 private:
  static uint32_t rotl(const uint32_t x, const uint32_t c) { return (x << c) | (x >> (32 - c)); }

  void processBlock(const uint8_t* block) {
    static const uint32_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    static uint32_t K[64];
    static bool kReady = false;
    if (!kReady) {
      for (int i = 0; i < 64; i++) {
        K[i] = static_cast<uint32_t>(std::floor(std::fabs(std::sin(static_cast<double>(i + 1))) * 4294967296.0));
      }
      kReady = true;
    }
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
      M[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }
    uint32_t A = a0_, B = b0_, C = c0_, D = d0_;
    for (uint32_t i = 0; i < 64; i++) {
      uint32_t F;
      uint32_t g;
      if (i < 16) {
        F = (B & C) | (~B & D);
        g = i;
      } else if (i < 32) {
        F = (D & B) | (~D & C);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        F = B ^ C ^ D;
        g = (3 * i + 5) % 16;
      } else {
        F = C ^ (B | ~D);
        g = (7 * i) % 16;
      }
      F = F + A + K[i] + M[g];
      A = D;
      D = C;
      C = B;
      B = B + rotl(F, S[i]);
    }
    a0_ += A;
    b0_ += B;
    c0_ += C;
    d0_ += D;
  }

  uint32_t a0_ = 0, b0_ = 0, c0_ = 0, d0_ = 0;
  uint64_t totalLen_ = 0;
  uint8_t buf_[64] = {};
  size_t bufLen_ = 0;
  uint8_t digest_[16] = {};
};
