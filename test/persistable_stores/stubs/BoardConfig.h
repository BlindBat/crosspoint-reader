#pragma once

// Host stand-in for the SDK BoardConfig. The capability answers are compile-time
// per test binary: PersistableStoresTest leaves them at the defaults and models a
// plain button board (no touch, no home key), while SettingsBoardVariantsTest
// defines them to 1 to pin the same persistence on a touch board with a home key.

#ifndef CROSSPOINT_TEST_BOARD_TOUCH
#define CROSSPOINT_TEST_BOARD_TOUCH 0
#endif
#ifndef CROSSPOINT_TEST_BOARD_HOME_KEY
#define CROSSPOINT_TEST_BOARD_HOME_KEY 0
#endif

namespace BoardConfig {

inline bool hasTouch() { return CROSSPOINT_TEST_BOARD_TOUCH != 0; }
inline bool hasHomeKey() { return CROSSPOINT_TEST_BOARD_HOME_KEY != 0; }

}  // namespace BoardConfig
