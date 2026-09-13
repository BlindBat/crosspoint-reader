#include "AllocCounter.h"

#include <cstdlib>
#include <new>

namespace alloc_counter {
namespace {

// Plain (non-atomic) state: this shim is for single-threaded host tests only. See AllocCounter.h.
bool g_enabled = false;
size_t g_count = 0;
size_t g_bytes = 0;

// The one funnel every replaced operator new goes through. Must never allocate on its own so the
// shim stays safe under AddressSanitizer and cannot recurse.
void* countedAllocate(const size_t size) {
  if (g_enabled) {
    ++g_count;
    g_bytes += size;
  }
  return std::malloc(size);
}

}  // namespace

size_t allocationCount() { return g_count; }

size_t allocatedBytes() { return g_bytes; }

void reset() {
  g_count = 0;
  g_bytes = 0;
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
// All matching deletes are replaced too so every counted allocation is freed with std::free.

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

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete[](void* allocation) noexcept { std::free(allocation); }

void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }

void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

void operator delete(void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }

void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }
