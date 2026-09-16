#include "PlatformHost.h"

#include <chrono>

#include "PlatformSeam.h"

namespace {
size_t g_freeHeap = 0;
size_t g_maxAlloc = 0;
size_t g_yields = 0;
constexpr size_t kDefaultFree = 64u * 1024u * 1024u;
constexpr size_t kDefaultMax = 32u * 1024u * 1024u;
}  // namespace

namespace platform_host {
void setHeap(size_t freeHeap, size_t maxAlloc) {
  g_freeHeap = freeHeap;
  g_maxAlloc = maxAlloc;
}
size_t yieldCount() { return g_yields; }
void resetCounters() { g_yields = 0; }
}  // namespace platform_host

namespace platform {
uint32_t millis() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}
void yield() { ++g_yields; }
size_t freeHeap() { return g_freeHeap ? g_freeHeap : kDefaultFree; }
size_t maxAllocHeap() { return g_maxAlloc ? g_maxAlloc : kDefaultMax; }
}  // namespace platform
