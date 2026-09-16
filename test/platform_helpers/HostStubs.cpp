// Definitions behind the Arduino / ESP-IDF stub headers in stubs/.

#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_memory_utils.h>
#include <esp_rom_sys.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {
uint32_t gMillis = 0;
uint32_t gDelayed = 0;
esp_reset_reason_t gResetReason = ESP_RST_POWERON;
std::string gSerial;
std::string gRomConsole;
bool gStackSane = false;
bool gPtrInDram = false;
}  // namespace

HardwareSerial Serial;

size_t HardwareSerial::write(uint8_t b) {
  gSerial.push_back(static_cast<char>(b));
  return 1;
}

size_t HardwareSerial::write(const uint8_t* data, size_t length) {
  gSerial.append(reinterpret_cast<const char*>(data), length);
  return length;
}

unsigned long millis() { return gMillis; }
void delay(unsigned long ms) { gDelayed += static_cast<uint32_t>(ms); }
void configTzTime(const char*, const char*, const char*, const char*) {}

esp_reset_reason_t esp_reset_reason() { return gResetReason; }

extern "C" void esp_rom_printf(const char* format, ...) {
  char buf[512];
  va_list args;
  va_start(args, format);
  const int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (len <= 0) return;
  const size_t written = static_cast<size_t>(len) < sizeof(buf) ? static_cast<size_t>(len) : sizeof(buf) - 1;
  gRomConsole.append(buf, written);
}

bool esp_stack_ptr_is_sane(uint32_t) { return gStackSane; }
bool esp_ptr_in_dram(const void*) { return gPtrInDram; }

namespace host {

void setMillis(uint32_t ms) { gMillis = ms; }
uint32_t millisValue() { return gMillis; }
uint32_t delayedMs() { return gDelayed; }
void setResetReason(esp_reset_reason_t reason) { gResetReason = reason; }
std::string& serialOutput() { return gSerial; }
std::string& romConsoleOutput() { return gRomConsole; }
void setStackSane(bool sane) { gStackSane = sane; }
void setPtrInDram(bool inDram) { gPtrInDram = inDram; }

void resetAll() {
  gMillis = 0;
  gDelayed = 0;
  gResetReason = ESP_RST_POWERON;
  gSerial.clear();
  gRomConsole.clear();
  gStackSane = false;
  gPtrInDram = false;
}

}  // namespace host
