#pragma once

// Minimal Arduino surface for the library-helpers host suite: the String
// class FsHelpers.h names, and millis() for ButtonNavigator's hold timing.
// The suite defines millis() itself against a settable fake clock.

#include <WString.h>

unsigned long millis();
