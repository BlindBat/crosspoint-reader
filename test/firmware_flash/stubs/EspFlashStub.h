#pragma once

// Test controls for the ESP-IDF partition/OTA stubs (esp_partition.h,
// esp_ota_ops.h). Three modelled partitions: the running app slot, the next
// OTA slot and otadata. Every erase/write is appended to `ops` in call order
// so tests can assert the flasher's erase-ahead/write cadence.

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace espstub {

struct Partition {
  esp_partition_t info{};
  std::vector<uint8_t> data;  // whole partition, 0xFF when erased

  void configure(const char* label, esp_partition_type_t type, esp_partition_subtype_t subtype, uint32_t address,
                 uint32_t size);
};

struct Op {
  enum Kind { ERASE, WRITE } kind;
  const esp_partition_t* part;
  size_t offset;
  size_t len;
};

// Partitions handed out by the stubs; a null pointer models "not found".
inline Partition running;
inline Partition next;
inline Partition otadata;
inline const esp_partition_t* runningPtr = nullptr;
inline const esp_partition_t* nextPtr = nullptr;
inline const esp_partition_t* otadataPtr = nullptr;

inline std::vector<Op> ops;
inline size_t eraseCalls = 0;
inline size_t writeCalls = 0;
// 1-based call index at which the matching operation returns ESP_FAIL (0 = never).
inline size_t failEraseAt = 0;
inline size_t failWriteAt = 0;
inline bool failReads = false;

// Standard 16 MB layout (partitions.csv): app0 running, app1 next, otadata 8 KiB.
// Clears the op log and failure injectors.
void resetDefaults();

Partition* lookup(const esp_partition_t* p);

}  // namespace espstub
