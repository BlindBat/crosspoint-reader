#pragma once

// Host stand-in: getSettingsList() only asks whether the IMU exists (to insert
// the tilt-page-turn entry). Compile-time per test binary, like the BoardConfig
// stub next to it.

#ifndef CROSSPOINT_TEST_BOARD_TILT
#define CROSSPOINT_TEST_BOARD_TILT 0
#endif

class HalTiltSensor {
 public:
  bool isAvailable() const { return CROSSPOINT_TEST_BOARD_TILT != 0; }
};

inline HalTiltSensor halTiltSensor;
