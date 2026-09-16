#pragma once

// Host stand-in: MappedInputManager only asks whether a frontlight exists
// (the top-edge swipe becomes the light panel instead of the menu gesture).

class HalFrontlight {
 public:
  static HalFrontlight& getInstance() {
    static HalFrontlight instance;
    return instance;
  }

  bool presentFlag = false;  // test control
  bool present() const { return presentFlag; }
};

#define Frontlight HalFrontlight::getInstance()
