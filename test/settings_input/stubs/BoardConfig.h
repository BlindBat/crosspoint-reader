#pragma once

// Host stand-in for the SDK BoardConfig. The suite compiles the touch-capable
// variant of MappedInputManager (FREEINK_CAP_TOUCH=1), so the board identity
// the power-click policy keys on is a test control; the compile-time board
// shape stays a plain button board.

namespace BoardConfig {

// Test control: MappedInputManager::wasPowerConfirmClick() takes the delayed
// frontlight-double-click path on the X4 Pro and the plain release edge
// elsewhere.
inline bool& x4ProFlag() {
  static bool flag = false;
  return flag;
}

inline bool hasTouch() { return false; }
inline bool hasHomeKey() { return false; }
inline bool isX4Pro() { return x4ProFlag(); }

}  // namespace BoardConfig
