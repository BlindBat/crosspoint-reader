#pragma once

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>

// The factory bootloaders boot our firmware.bin, but the running ESP-IDF's
// esp_image_verify rejects it with bogus eFuse block-revision errors. Both the
// SD-card and the OTA update path bypass that runtime check by writing the OTA
// app partition raw and updating otadata directly, the same scheme the browser
// flasher uses.
//
// Layout reference: esp_flash_partitions.h. CRC covers ota_seq (4 bytes) only.

namespace ota_boot {

struct __attribute__((packed)) SelectEntry {
  uint32_t ota_seq;
  uint8_t seq_label[20];
  uint32_t ota_state;
  uint32_t crc;
};
static_assert(sizeof(SelectEntry) == 32, "SelectEntry must be 32 bytes");

constexpr uint32_t kOtaImgNew = 0;      // ESP_OTA_IMG_NEW
constexpr uint32_t kOtaImgInvalid = 3;  // ESP_OTA_IMG_INVALID
constexpr uint32_t kOtaImgAborted = 4;  // ESP_OTA_IMG_ABORTED
constexpr size_t kOtaSeqCrcLen = 4;

// CRC32-LE over the 4-byte ota_seq, init UINT32_MAX. Matches IDF and web flasher.
uint32_t computeSeqCrc(uint32_t seq);

// Switch the bootloader's selected app partition to `dest` by writing a fresh
// otadata entry into the inactive otadata slot. Bypasses esp_ota_set_boot_partition's
// esp_image_verify call. The bytes in `dest` must already be a valid app image
// (e.g. patch_firmware_image.py output) — caller is responsible for that.
//
// Returns true on success.
bool switchTo(const esp_partition_t* dest);

}  // namespace ota_boot
