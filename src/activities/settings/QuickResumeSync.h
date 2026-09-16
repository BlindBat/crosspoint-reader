#pragma once

#include <cstdint>

#include "CrossPointSettings.h"

namespace quick_resume {

// Keeps the "Quick Resume after timeout" toggle in step with the sleep-screen
// mode. Selecting the Quick Resume sleep screen switches the toggle on for
// the user (auto-enabled); leaving that screen again switches it back off only
// when it was auto-enabled and the user has not since chosen it themselves.
//
// preserveTimeoutOn: the toggle was On by the user's own choice (captured on
//   entry and whenever the toggle itself changes).
// timeoutAutoEnabled: the toggle is On only because the sleep screen is Quick
//   Resume.
inline void syncTimeoutForSleepScreen(const uint8_t sleepScreen, uint8_t& quickResumeSleepScreen,
                                      bool& preserveTimeoutOn, bool& timeoutAutoEnabled, const bool sleepScreenChanged,
                                      const bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveTimeoutOn =
        quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    timeoutAutoEnabled = false;
  }

  if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      timeoutAutoEnabled = !preserveTimeoutOn;
    } else if (sleepScreenChanged && !preserveTimeoutOn) {
      timeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && timeoutAutoEnabled && !preserveTimeoutOn) {
    quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    timeoutAutoEnabled = false;
  }
}

}  // namespace quick_resume
