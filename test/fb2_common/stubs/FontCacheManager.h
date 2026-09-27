#pragma once

// Host stand-in for lib/GfxRenderer/FontCacheManager.h: Section and Fb2Section
// release the rebuildable SD-font caches before laying out a chapter, and the
// suites count that they did.
class FontCacheManager {
 public:
  int releaseSdFontCachesCalls = 0;
  void releaseSdFontCaches() { releaseSdFontCachesCalls++; }
};
