#pragma once

#include <cstddef>

// Test controls for the host implementation of lib/Platform/PlatformSeam.h
// (test/support/PlatformHost.cpp). Compile PlatformHost.cpp into any suite whose
// production sources call platform::*.
namespace platform_host {

// Override the values platform::freeHeap()/maxAllocHeap() report; 0 restores
// the defaults (64 MB / 32 MB), which are "plenty" for every heap gate.
void setHeap(size_t freeHeap, size_t maxAlloc);

// Number of platform::yield() calls since the last resetCounters().
size_t yieldCount();
void resetCounters();

}  // namespace platform_host
