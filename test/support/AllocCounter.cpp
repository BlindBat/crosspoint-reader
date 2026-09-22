#include "AllocCounter.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

namespace alloc_counter {
namespace {

// Plain (non-atomic) state: this shim is for single-threaded host tests only. See AllocCounter.h.
bool g_enabled = false;
size_t g_count = 0;
size_t g_bytes = 0;
size_t g_live = 0;
size_t g_peak = 0;
// Bumped by reset(). A block records the epoch it was counted in (0 = not counted), so freeing a
// block counted before the last reset, or never counted, leaves liveBytes() alone.
size_t g_epoch = 1;

// Every block carries {size, epoch} in a header, whether or not counting is enabled: a block
// allocated before counting starts is still freed through the same operator delete.
struct Header {
  size_t size;
  size_t epoch;
};
constexpr size_t HEADER = sizeof(Header) > alignof(std::max_align_t) ? sizeof(Header) : alignof(std::max_align_t);

// The one funnel every replaced operator new goes through. Must never allocate on its own so the
// shim stays safe under AddressSanitizer and cannot recurse.
void* countedAllocate(const size_t size) {
  if (size > static_cast<size_t>(-1) - HEADER) return nullptr;  // the header must not wrap the size
  auto* block = static_cast<unsigned char*>(std::malloc(size + HEADER));
  if (!block) return nullptr;
  const Header header{size, g_enabled ? g_epoch : 0};
  std::memcpy(block, &header, sizeof(header));
  if (g_enabled) {
    ++g_count;
    g_bytes += size;
    g_live += size;
    if (g_live > g_peak) g_peak = g_live;
  }
  return block + HEADER;
}

void countedFree(void* allocation) {
  if (!allocation) return;
  auto* block = static_cast<unsigned char*>(allocation) - HEADER;
  Header header;
  std::memcpy(&header, block, sizeof(header));
  if (header.epoch == g_epoch) g_live -= header.size;
  std::free(block);
}

}  // namespace

size_t allocationCount() { return g_count; }

size_t allocatedBytes() { return g_bytes; }

size_t liveBytes() { return g_live; }

size_t peakBytes() { return g_peak; }

void reset() {
  g_count = 0;
  g_bytes = 0;
  g_live = 0;
  g_peak = 0;
  ++g_epoch;
}

void setEnabled(const bool enabled) { g_enabled = enabled; }

bool isEnabled() { return g_enabled; }

CountingScope::CountingScope() : wasEnabled_(g_enabled) {
  reset();
  g_enabled = true;
}

CountingScope::~CountingScope() { g_enabled = wasEnabled_; }

size_t CountingScope::count() const { return g_count; }

size_t CountingScope::bytes() const { return g_bytes; }

}  // namespace alloc_counter

// Global replacements. Throwing forms must not return nullptr; nothrow forms must not throw.
// All matching deletes are replaced too so every allocation is freed through countedFree.

void* operator new(const std::size_t size) {
  if (void* allocation = alloc_counter::countedAllocate(size)) return allocation;
  throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
  if (void* allocation = alloc_counter::countedAllocate(size)) return allocation;
  throw std::bad_alloc();
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
  return alloc_counter::countedAllocate(size);
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  return alloc_counter::countedAllocate(size);
}

void operator delete(void* allocation) noexcept { alloc_counter::countedFree(allocation); }

void operator delete[](void* allocation) noexcept { alloc_counter::countedFree(allocation); }

void operator delete(void* allocation, std::size_t) noexcept { alloc_counter::countedFree(allocation); }

void operator delete[](void* allocation, std::size_t) noexcept { alloc_counter::countedFree(allocation); }

void operator delete(void* allocation, const std::nothrow_t&) noexcept { alloc_counter::countedFree(allocation); }

void operator delete[](void* allocation, const std::nothrow_t&) noexcept { alloc_counter::countedFree(allocation); }
