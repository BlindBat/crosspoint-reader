#pragma once

// Host stub for the Arduino core: FontDecompressor only uses the two clocks
// for its stats accounting. Deterministic zero keeps stats assertions stable.
#include <cstdint>

inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
