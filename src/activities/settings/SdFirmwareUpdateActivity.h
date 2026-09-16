#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Push ConfirmationActivity ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition with
 *     firmware_flash::flashFromSdPath(), drawing a progress bar; on success
 *     ESP.restart().
 *
 * Step 2 resolves the next-update partition through esp_ota_get_next_update_partition()
 * and runs the same firmware_flash::validateImageFile() checks the flasher applies,
 * so a truncated, foreign-chip or wrong-board image is refused before any erase.
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (power-on with a side button held: Down
 * on X4 Pro and X4 Classic, whose Up key is a boot strap, Up elsewhere; Paper Mono
 * has no recovery entry). In recovery mode cancelling re-opens the picker, so there
 * is no way out into a half-initialised UI.
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State {
    PICKING,
    VALIDATING,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : Activity("SdFirmwareUpdate", renderer, mappedInput), recoveryMode(recoveryMode) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::VALIDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }

 private:
  State state = State::PICKING;
  bool recoveryMode = false;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
