#pragma once

// Host stand-in for lib/hal/HalMemory.h: the TLS heap gate in
// KOReaderSyncClient reads the default heap here, so the numbers come from the
// same platform seam the tests already drive with platform_host::setHeap().

#include <PlatformSeam.h>

#include <cstddef>

class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes;
    size_t totalBytes;
    size_t minFreeBytes;
    size_t largestBlockBytes;
  };

  static HeapStats getDefaultHeap() { return {platform::freeHeap(), 0, 0, platform::maxAllocHeap()}; }
  static HeapStats getInternalHeap() { return getDefaultHeap(); }
  static HeapStats getPsramHeap() { return {0, 0, 0, 0}; }
};
