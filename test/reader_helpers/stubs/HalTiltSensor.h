#pragma once

// Host stand-in: tilt events are one-shot, consumed by the was*() query like the IMU driver.
class HalTiltSensor {
 public:
  bool forwardEvent = false;
  bool backEvent = false;
  bool wasTiltedForward() {
    const bool v = forwardEvent;
    forwardEvent = false;
    return v;
  }
  bool wasTiltedBack() {
    const bool v = backEvent;
    backEvent = false;
    return v;
  }
};

inline HalTiltSensor halTiltSensor;
