#pragma once

// Heap-allocation guard for the malformed-input tests.
//
// The firmware runs with ~380KB RAM, so a reader that trusts an attacker's
// declared size and calls malloc() with it is a real out-of-memory bug on
// device even though it may look harmless on a 64-bit host. This guard
// interposes the C allocator (which is where lib/ZipFile allocates, and also
// where operator new bottoms out) so a test can measure the LARGEST single
// allocation a call attempts.
//
// Key detail: the requested size is recorded BEFORE the real allocator runs,
// so a huge request is observed whether or not the host actually satisfies it.
// A test may also set failAbove to make the guard return nullptr for oversized
// requests, reproducing the device's OOM without the host reserving gigabytes.
//
// MUST be included in exactly one translation unit per test executable (it
// defines the global allocator functions).

#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace allocguard {
inline bool tracking = false;
inline size_t maxSingle = 0;         // largest single request seen while tracking
inline size_t totalBytes = 0;        // sum of all requests while tracking
inline size_t count = 0;             // number of allocation calls while tracking
inline size_t failAbove = SIZE_MAX;  // return nullptr for requests larger than this

inline void reset() {
  maxSingle = 0;
  totalBytes = 0;
  count = 0;
}

// RAII: track every allocation within the scope, restoring prior state on exit.
struct TrackScope {
  size_t savedFailAbove;
  explicit TrackScope(size_t failAboveBytes = SIZE_MAX) : savedFailAbove(failAbove) {
    reset();
    failAbove = failAboveBytes;
    tracking = true;
  }
  ~TrackScope() {
    tracking = false;
    failAbove = savedFailAbove;
  }
};
}  // namespace allocguard

using MallocFn = void* (*)(size_t);
using FreeFn = void (*)(void*);
using CallocFn = void* (*)(size_t, size_t);
using ReallocFn = void* (*)(void*, size_t);

static MallocFn ag_real_malloc = nullptr;
static FreeFn ag_real_free = nullptr;
static CallocFn ag_real_calloc = nullptr;
static ReallocFn ag_real_realloc = nullptr;

// Bootstrap arena: dlsym() itself may allocate before the real symbols are
// resolved. Serve those few early requests from a fixed buffer.
static char ag_bootstrap[1 << 16];
static size_t ag_bootstrap_off = 0;
static bool ag_from_bootstrap(const void* p) {
  return p >= static_cast<void*>(ag_bootstrap) && p < static_cast<void*>(ag_bootstrap + sizeof(ag_bootstrap));
}

static void ag_ensure_syms() {
  if (!ag_real_malloc) {
    ag_real_malloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
    ag_real_free = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
    ag_real_calloc = reinterpret_cast<CallocFn>(dlsym(RTLD_NEXT, "calloc"));
    ag_real_realloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
  }
}

static void* ag_bootstrap_alloc(size_t n) {
  const size_t aligned = (n + 15) & ~size_t{15};
  if (ag_bootstrap_off + aligned > sizeof(ag_bootstrap)) return nullptr;
  void* p = ag_bootstrap + ag_bootstrap_off;
  ag_bootstrap_off += aligned;
  return p;
}

extern "C" void* malloc(size_t n) {
  if (!ag_real_malloc) {
    ag_ensure_syms();
    if (!ag_real_malloc) return ag_bootstrap_alloc(n);
  }
  if (allocguard::tracking) {
    if (n > allocguard::maxSingle) allocguard::maxSingle = n;
    allocguard::totalBytes += n;
    allocguard::count++;
    if (n > allocguard::failAbove) return nullptr;  // simulate device OOM
  }
  return ag_real_malloc(n);
}

extern "C" void free(void* p) {
  if (ag_from_bootstrap(p)) return;
  ag_ensure_syms();
  if (ag_real_free) ag_real_free(p);
}

extern "C" void* calloc(size_t nmemb, size_t sz) {
  if (!ag_real_calloc) {
    ag_ensure_syms();
    if (!ag_real_calloc) {
      const size_t n = nmemb * sz;
      void* p = ag_bootstrap_alloc(n);
      if (p) std::memset(p, 0, n);
      return p;
    }
  }
  if (allocguard::tracking) {
    const size_t n = nmemb * sz;
    if (n > allocguard::maxSingle) allocguard::maxSingle = n;
    allocguard::totalBytes += n;
    allocguard::count++;
    if (n > allocguard::failAbove) return nullptr;
  }
  return ag_real_calloc(nmemb, sz);
}

extern "C" void* realloc(void* p, size_t n) {
  ag_ensure_syms();
  if (allocguard::tracking) {
    if (n > allocguard::maxSingle) allocguard::maxSingle = n;
    allocguard::totalBytes += n;
    allocguard::count++;
    if (n > allocguard::failAbove) return nullptr;
  }
  if (ag_from_bootstrap(p)) {
    // Migrate a bootstrap block to the real heap.
    void* q = ag_real_malloc ? ag_real_malloc(n) : nullptr;
    return q;
  }
  return ag_real_realloc(p, n);
}

// Replaceable global operator new/delete. Unlike a plain malloc interposer,
// these override the allocator that libc++ itself uses (std::string,
// std::vector, ...), which is where BookMetadataCache's length-prefixed-string
// reads allocate. They record into the same counters and honor failAbove; they
// call the real allocator directly so a new that bottoms out in malloc is not
// double-counted.
namespace {
inline void* ag_new(size_t n) {
  ag_ensure_syms();
  if (allocguard::tracking) {
    if (n > allocguard::maxSingle) allocguard::maxSingle = n;
    allocguard::totalBytes += n;
    allocguard::count++;
    if (n > allocguard::failAbove) throw std::bad_alloc();
  }
  void* p = ag_real_malloc ? ag_real_malloc(n ? n : 1) : ag_bootstrap_alloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
inline void* ag_new_nothrow(size_t n) noexcept {
  if (allocguard::tracking) {
    if (n > allocguard::maxSingle) allocguard::maxSingle = n;
    allocguard::totalBytes += n;
    allocguard::count++;
    if (n > allocguard::failAbove) return nullptr;
  }
  ag_ensure_syms();
  return ag_real_malloc ? ag_real_malloc(n ? n : 1) : ag_bootstrap_alloc(n ? n : 1);
}
inline void ag_delete(void* p) noexcept {
  if (ag_from_bootstrap(p)) return;
  ag_ensure_syms();
  if (ag_real_free) ag_real_free(p);
}
}  // namespace

void* operator new(std::size_t n) { return ag_new(n); }
void* operator new[](std::size_t n) { return ag_new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return ag_new_nothrow(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return ag_new_nothrow(n); }
void operator delete(void* p) noexcept { ag_delete(p); }
void operator delete[](void* p) noexcept { ag_delete(p); }
void operator delete(void* p, std::size_t) noexcept { ag_delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ag_delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ag_delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ag_delete(p); }
