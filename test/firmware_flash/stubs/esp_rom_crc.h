#pragma once

#include <cstdint>

// IDF ROM crc32_le: reflected CRC-32 (poly 0xEDB88320) with the running value
// complemented on entry and exit, so crc32_le(0, "123456789") == 0xCBF43926.
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len);
