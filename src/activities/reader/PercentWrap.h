#pragma once

namespace ReaderPercent {

// Steps a 0..100 percent value by delta on a 100-value ring: 0% and 100% share the
// wrap point, but 100 stays the landing value when reached without crossing it
// (90 + 10 = 100, while 100 + 1 = 1 and 0 - 1 = 99).
inline int wrapPercent(const int percent, const int delta) {
  const int raw = percent + delta;
  if (raw > 0 && raw % 100 == 0) {
    return 100;
  }
  return ((raw % 100) + 100) % 100;
}

}  // namespace ReaderPercent
