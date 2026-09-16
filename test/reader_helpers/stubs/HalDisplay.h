#pragma once

// Host stand-in: only the refresh-mode enum ReaderUtils selects from.
class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
};
