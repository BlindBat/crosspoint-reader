#pragma once

// Recording host stand-in for the esp_ota_* API OtaUpdater::installUpdate()
// drives. Every call is captured on a resettable singleton so tests can
// assert the abort/commit sequencing and the exact bytes "flashed".

#include <cstddef>
#include <cstdint>
#include <vector>

using esp_err_t = int;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define OTA_SIZE_UNKNOWN 0xffffffffU

struct esp_partition_t {
  int id;
};

using esp_ota_handle_t = uint32_t;

struct EspOtaRecorder {
  bool havePartition = true;
  esp_err_t beginResult = ESP_OK;
  esp_err_t writeResult = ESP_OK;
  esp_err_t endResult = ESP_OK;
  esp_err_t setBootResult = ESP_OK;
  int beginCalls = 0;
  int abortCalls = 0;
  int endCalls = 0;
  int setBootCalls = 0;
  std::vector<uint8_t> written;

  void reset() { *this = EspOtaRecorder{}; }

  static EspOtaRecorder& instance() {
    static EspOtaRecorder recorder;
    return recorder;
  }
};

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  static esp_partition_t partition{1};
  return EspOtaRecorder::instance().havePartition ? &partition : nullptr;
}

inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* outHandle) {
  EspOtaRecorder::instance().beginCalls++;
  *outHandle = 42;
  return EspOtaRecorder::instance().beginResult;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void* data, size_t size) {
  auto& recorder = EspOtaRecorder::instance();
  if (recorder.writeResult == ESP_OK) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    recorder.written.insert(recorder.written.end(), bytes, bytes + size);
  }
  return recorder.writeResult;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t) {
  EspOtaRecorder::instance().endCalls++;
  return EspOtaRecorder::instance().endResult;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t) {
  EspOtaRecorder::instance().abortCalls++;
  return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  EspOtaRecorder::instance().setBootCalls++;
  return EspOtaRecorder::instance().setBootResult;
}

inline const char* esp_err_to_name(esp_err_t) { return "ESP_ERR_STUB"; }
