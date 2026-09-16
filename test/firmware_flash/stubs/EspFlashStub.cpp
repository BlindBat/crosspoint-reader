#include "EspFlashStub.h"

#include <esp_ota_ops.h>
#include <esp_rom_crc.h>

#include <cstring>

namespace espstub {

void Partition::configure(const char* label, esp_partition_type_t type, esp_partition_subtype_t subtype,
                          uint32_t address, uint32_t size) {
  info = {};
  info.type = type;
  info.subtype = subtype;
  info.address = address;
  info.size = size;
  info.erase_size = 4096;
  std::strncpy(info.label, label, sizeof(info.label) - 1);
  data.assign(size, 0xFF);
}

void resetDefaults() {
  running.configure("app0", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x640000);
  next.configure("app1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 0x640000);
  otadata.configure("otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xE000, 0x2000);
  runningPtr = &running.info;
  nextPtr = &next.info;
  otadataPtr = &otadata.info;
  ops.clear();
  eraseCalls = 0;
  writeCalls = 0;
  failEraseAt = 0;
  failWriteAt = 0;
  failReads = false;
}

Partition* lookup(const esp_partition_t* p) {
  if (p == &running.info) return &running;
  if (p == &next.info) return &next;
  if (p == &otadata.info) return &otadata;
  return nullptr;
}

}  // namespace espstub

using namespace espstub;

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char*) {
  if (type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) return otadataPtr;
  return nullptr;
}

esp_err_t esp_partition_read(const esp_partition_t* partition, size_t src_offset, void* dst, size_t size) {
  Partition* p = lookup(partition);
  if (!p || failReads || src_offset + size > p->data.size()) return ESP_FAIL;
  std::memcpy(dst, p->data.data() + src_offset, size);
  return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t* partition, size_t dst_offset, const void* src, size_t size) {
  Partition* p = lookup(partition);
  ++writeCalls;
  ops.push_back({Op::WRITE, partition, dst_offset, size});
  if (!p || dst_offset + size > p->data.size()) return ESP_FAIL;
  if (failWriteAt != 0 && writeCalls == failWriteAt) return ESP_FAIL;
  // NOR flash: a write can only clear bits, so writing over an unerased
  // region corrupts the data instead of replacing it.
  const auto* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < size; i++) p->data[dst_offset + i] &= s[i];
  return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t* partition, size_t offset, size_t size) {
  Partition* p = lookup(partition);
  ++eraseCalls;
  ops.push_back({Op::ERASE, partition, offset, size});
  if (!p || offset + size > p->data.size() || offset % 4096 != 0 || size % 4096 != 0) return ESP_FAIL;
  if (failEraseAt != 0 && eraseCalls == failEraseAt) return ESP_FAIL;
  std::memset(p->data.data() + offset, 0xFF, size);
  return ESP_OK;
}

const esp_partition_t* esp_ota_get_running_partition(void) { return runningPtr; }

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return nextPtr; }

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
