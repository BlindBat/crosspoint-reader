#pragma once

// Host stand-in for the SDK IMU driver. HalTiltSensor owns its Imu by value,
// so the scripted behaviour lives in a process-wide HostState the test resets
// and drives (WHO_AM_I success, sample values, I2C failures, call counts).

#include <cstdint>

namespace freeink {

class Imu {
 public:
  struct Sample {
    float ax, ay, az;
    float gx, gy, gz;
  };

  struct HostState {
    bool beginOk = false;
    bool readOk = true;
    bool sleepOk = true;
    bool wakeOk = true;
    Sample sample{};
    int beginCalls = 0;
    int readCalls = 0;
    int sleepCalls = 0;
    int wakeCalls = 0;
  };

  static HostState& host() {
    static HostState state;
    return state;
  }

  bool begin() {
    host().beginCalls++;
    begun_ = host().beginOk;
    return begun_;
  }
  bool present() const { return begun_; }
  bool read(Sample& out) {
    host().readCalls++;
    if (!host().readOk) return false;
    out = host().sample;
    return true;
  }
  bool sleep() {
    host().sleepCalls++;
    return host().sleepOk;
  }
  bool wake() {
    host().wakeCalls++;
    return host().wakeOk;
  }

 private:
  bool begun_ = false;
};

}  // namespace freeink

using Imu = freeink::Imu;
