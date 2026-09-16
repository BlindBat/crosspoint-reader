#pragma once

// Minimal Arduino surface for the compiled production sources: timing,
// RTC/IRAM placement attributes (no-ops on host) and the reset-reason API.

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "HostControls.h"
#include "Print.h"
#include "WString.h"
#include "esp_system.h"

#define RTC_NOINIT_ATTR
#define DRAM_ATTR
#define IRAM_ATTR

unsigned long millis();
void delay(unsigned long ms);
void configTzTime(const char* tz, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
