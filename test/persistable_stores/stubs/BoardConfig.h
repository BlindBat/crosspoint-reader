#pragma once

// Host stand-in for the SDK BoardConfig. The suite models a plain button
// board (no touch, no home key): SettingsList prunes the touch-only entries,
// which the tests pin explicitly.

namespace BoardConfig {

inline bool hasTouch() { return false; }
inline bool hasHomeKey() { return false; }

}  // namespace BoardConfig
