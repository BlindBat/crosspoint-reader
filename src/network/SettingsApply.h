#pragma once

#include <cstdint>

#include "CrossPointSettings.h"
#include "activities/settings/SettingsActivity.h"

// Redundant-write guards for POST /api/settings. settings.json is rewritten in
// full on every save, so a request that carries only values the device already
// holds must not reach the SD card.
namespace settings_apply {

// Stores a toggle's 0/1 value and reports whether the stored value moved.
// A toggle posted at its current value is not an applied setting.
inline bool applyToggle(const SettingInfo& info, const int value) {
  if (!info.valuePtr) {
    return false;
  }
  const uint8_t next = value ? 1 : 0;
  if (SETTINGS.*(info.valuePtr) == next) {
    return false;
  }
  SETTINGS.*(info.valuePtr) = next;
  return true;
}

// settings.json is written only when the request applied at least one setting.
constexpr bool needsSettingsSave(const int applied) { return applied > 0; }

}  // namespace settings_apply
