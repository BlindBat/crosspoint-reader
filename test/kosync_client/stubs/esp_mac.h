#pragma once

// Deterministic hardware MAC for the host suite: the obfuscation XOR key is
// derived from these six bytes, so obfuscated fixtures stay reproducible.

#include <cstdint>
#include <cstring>

inline int esp_efuse_mac_get_default(uint8_t* mac) {
  static const uint8_t kTestMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
  std::memcpy(mac, kTestMac, sizeof(kTestMac));
  return 0;
}
