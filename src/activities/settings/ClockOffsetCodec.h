#pragma once

#include <cstdint>

// Storage encoding of the status-bar clock's UTC offset
// (CrossPointSettings::clockUtcOffsetQ): quarter-hours biased by 48, so
// 0 = UTC-12:00, 48 = UTC+0 and 104 = UTC+14:00. The editable form is a
// (sign, hours, quarter) triple; sign 0 = positive, 1 = negative.
namespace clock_offset {

constexpr uint8_t MAX_POS_HOURS = 14;
constexpr uint8_t MAX_NEG_HOURS = 12;
constexpr uint8_t MINUTE_STEPS = 4;  // 0, 15, 30, 45
constexpr uint8_t MINUTES_PER_QUARTER = 15;
constexpr uint8_t BIAS_QUARTER_HOURS = 48;
constexpr uint8_t MAX_ENCODED = 104;

inline uint8_t maxHoursForSign(const uint8_t sign) { return sign == 1 ? MAX_NEG_HOURS : MAX_POS_HOURS; }

// Returns a value in [0, MAX_ENCODED].
inline uint8_t encode(const uint8_t sign, const uint8_t hours, const uint8_t quarter) {
  int signedQuarter = static_cast<int>(hours) * 4 + static_cast<int>(quarter);
  if (sign == 1) signedQuarter = -signedQuarter;
  int biased = signedQuarter + BIAS_QUARTER_HOURS;
  if (biased < 0) biased = 0;
  if (biased > MAX_ENCODED) biased = MAX_ENCODED;
  return static_cast<uint8_t>(biased);
}

// An out-of-range stored value decodes as UTC+0.
inline void decode(uint8_t biased, uint8_t& sign, uint8_t& hours, uint8_t& quarter) {
  if (biased > MAX_ENCODED) biased = BIAS_QUARTER_HOURS;
  int signedQuarter = static_cast<int>(biased) - BIAS_QUARTER_HOURS;
  if (signedQuarter < 0) {
    sign = 1;
    signedQuarter = -signedQuarter;
  } else {
    sign = 0;
  }
  hours = static_cast<uint8_t>(signedQuarter / 4);
  quarter = static_cast<uint8_t>(signedQuarter % 4);
}

// Hours are capped per sign; at the absolute boundary (-12:00 or +14:00)
// only :00 is valid.
inline void clampForSign(const uint8_t sign, uint8_t& hours, uint8_t& quarter) {
  const uint8_t maxHours = maxHoursForSign(sign);
  if (hours > maxHours) hours = maxHours;
  if (hours == maxHours && quarter != 0) quarter = 0;
}

}  // namespace clock_offset
