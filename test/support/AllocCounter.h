#pragma once

#include <cstddef>

// Heap-allocation counting support for host-side performance-regression tests.
//
// Compiling AllocCounter.cpp into a test binary replaces the global operator new / operator new[]
// (and the matching deletes) with a shim that forwards to std::malloc/std::free and, while
// counting is enabled, increments plain counters. Modeled on the inline pattern proven in
// test/font_cache_manager/FontCacheManagerTest.cpp, extracted so multiple suites can share it.
//
// Constraints and guarantees:
//  - SINGLE-THREADED ONLY. The counters are plain size_t (no atomics, no locks) because all host
//    test suites in this repo run their measured code on one thread, mirroring the single-core
//    ESP32-C3 target. Do not use this shim from multi-threaded tests.
//  - Sanitizer-compatible: the counting path itself performs no heap allocation (it only bumps
//    two integers) and forwards to std::malloc/std::free, which ASan/LSan intercept normally.
//  - Over-aligned allocations (operator new with std::align_val_t) are NOT intercepted; they fall
//    through to the platform default and are not counted. None of the guarded production paths
//    allocate over-aligned types.
namespace alloc_counter {

// Number of counted operator-new calls since the last reset().
size_t allocationCount();

// Total bytes requested by those calls (requested size, not allocator-rounded size).
size_t allocatedBytes();

// Bytes of blocks allocated while counting was enabled since the last reset() and not yet freed.
// Freeing any other block (allocated before the reset, or while disabled) does not change it.
size_t liveBytes();

// High-water mark of liveBytes() since the last reset().
size_t peakBytes();

// Zeroes all counters. Does not change whether counting is enabled.
void reset();

// Turns counting on or off. Counting starts disabled so process/gtest startup is not measured.
void setEnabled(bool enabled);
bool isEnabled();

// RAII scope guard: resets the counters and enables counting on construction, restores the
// previous enabled state on destruction. The counters keep their final values after the scope
// closes (until the next reset), so results may be read either through the guard while it is
// alive or via allocationCount()/allocatedBytes() afterwards.
//
// Keep gtest assertions OUTSIDE the scope: EXPECT/ASSERT machinery may allocate and would
// pollute the measurement.
class CountingScope {
 public:
  CountingScope();
  ~CountingScope();

  CountingScope(const CountingScope&) = delete;
  CountingScope& operator=(const CountingScope&) = delete;

  // Allocations / bytes observed since this scope was opened.
  size_t count() const;
  size_t bytes() const;

 private:
  bool wasEnabled_;
};

}  // namespace alloc_counter
