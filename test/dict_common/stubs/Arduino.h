#pragma once

// Host test stub for the Arduino surface the dictionary code still touches.
// The heap guards in DictZip::parse and Dictionary::readDefinition, and the
// clocks, now go through lib/Platform/PlatformSeam.h; HeapLimitScope drives
// platform::maxAllocHeap() so tests can reach the LowMemory refusal paths
// without a fragmented heap.

#include <cstdint>

#include "PlatformHost.h"

namespace dictstub {
// Default is "plenty": guards pass unless a test lowers it. Restore via
// HeapLimitScope rather than by hand so a failing test can't leak the limit.
class HeapLimitScope {
 public:
  explicit HeapLimitScope(uint32_t limit) { platform_host::setHeap(0, limit); }
  ~HeapLimitScope() { platform_host::setHeap(0, 0); }
  HeapLimitScope(const HeapLimitScope&) = delete;
  HeapLimitScope& operator=(const HeapLimitScope&) = delete;
};
}  // namespace dictstub

inline void delay(unsigned long) {}
