#pragma once

// Host test stub for the Arduino/ESP surface the dictionary code touches:
// ESP.getMaxAllocHeap() (the pre-reserve heap guards in DictZip::parse and
// Dictionary::readDefinition) and millis(). The reported heap size is settable
// so tests can drive the LowMemory refusal paths without a fragmented heap.

#include <cstdint>

namespace dictstub {
// Default is "plenty": guards pass unless a test lowers it. Restore via
// HeapLimitScope rather than by hand so a failing test can't leak the limit.
inline uint32_t maxAllocHeap = 256u * 1024u * 1024u;

class HeapLimitScope {
 public:
  explicit HeapLimitScope(uint32_t limit) : saved_(maxAllocHeap) { maxAllocHeap = limit; }
  ~HeapLimitScope() { maxAllocHeap = saved_; }
  HeapLimitScope(const HeapLimitScope&) = delete;
  HeapLimitScope& operator=(const HeapLimitScope&) = delete;

 private:
  uint32_t saved_;
};
}  // namespace dictstub

struct EspStubClass {
  uint32_t getMaxAllocHeap() const { return dictstub::maxAllocHeap; }
};

inline EspStubClass ESP;

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}
