#pragma once

#include <cstdint>
#include <string>

#include "esp_system.h"

// Test-side knobs behind the Arduino / ESP-IDF stubs (implemented in HostStubs.cpp).
namespace host {

void setMillis(uint32_t ms);
uint32_t millisValue();
// Total milliseconds passed to delay() since the last reset.
uint32_t delayedMs();

void setResetReason(esp_reset_reason_t reason);

// Everything logSerial.print()ed since the last clear.
std::string& serialOutput();
// Everything esp_rom_printf()ed since the last clear (the Sticky log transport).
std::string& romConsoleOutput();

// Knobs for the esp_memory_utils stubs used by the panic backtrace capture.
void setStackSane(bool sane);
void setPtrInDram(bool inDram);

// Resets every knob to its boot default.
void resetAll();

}  // namespace host
