#pragma once

// Host stand-in: getSettingsList() only asks whether the IMU exists (to
// insert the tilt-page-turn entry). The suite models a board without one.

class HalTiltSensor {
 public:
  bool isAvailable() const { return false; }
};

inline HalTiltSensor halTiltSensor;
