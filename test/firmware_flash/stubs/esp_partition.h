#pragma once

// Host model of the ESP-IDF partition API. Layout mirrors esp_partition.h
// (esp32c3 IDF 5.x) closely enough for the production sources; the behaviour
// (erase sets 0xFF, write can only clear bits, every call is logged) lives in
// EspFlashStub.cpp and is driven through EspFlashStub.h.

#include <esp_err.h>

#include <cstddef>
#include <cstdint>

typedef enum {
  ESP_PARTITION_TYPE_APP = 0x00,
  ESP_PARTITION_TYPE_DATA = 0x01,
  ESP_PARTITION_TYPE_ANY = 0xff,
} esp_partition_type_t;

typedef enum {
  ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
  ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,
  ESP_PARTITION_SUBTYPE_APP_OTA_0 = ESP_PARTITION_SUBTYPE_APP_OTA_MIN + 0,
  ESP_PARTITION_SUBTYPE_APP_OTA_1 = ESP_PARTITION_SUBTYPE_APP_OTA_MIN + 1,
  ESP_PARTITION_SUBTYPE_APP_OTA_15 = ESP_PARTITION_SUBTYPE_APP_OTA_MIN + 15,
  ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
  ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
  ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef struct {
  void* flash_chip;
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t address;
  uint32_t size;
  uint32_t erase_size;
  char label[17];
  bool encrypted;
  bool readonly;
} esp_partition_t;

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char* label);
esp_err_t esp_partition_read(const esp_partition_t* partition, size_t src_offset, void* dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t* partition, size_t dst_offset, const void* src, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t* partition, size_t offset, size_t size);
