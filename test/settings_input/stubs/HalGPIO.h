#pragma once

// Host stand-in for lib/hal/HalGPIO.h with settable button and touch state.
// Every query MappedInputManager makes reads a public field; tests assign the
// fields directly and call reset() between scenarios. update() only counts.

#include <Arduino.h>

#include <cstdint>

class HalGPIO {
 public:
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BUTTON_COUNT = 7;

  // --- test controls -------------------------------------------------------
  bool down[BUTTON_COUNT] = {};          // isPressed()
  bool pressedEdge[BUTTON_COUNT] = {};   // wasPressed()
  bool releasedEdge[BUTTON_COUNT] = {};  // wasReleased()
  unsigned long heldMs = 0;
  unsigned long powerHeldMs = 0;
  bool touch = false;
  bool homeKey = false;
  bool homeKeyTapped = false;
  bool homeKeyLong = false;
  bool tapPending = false;
  float tapNx = 0.0f;
  float tapNy = 0.0f;
  bool tapCandidate = false;
  unsigned long tapCandidateHeldMs = 0;
  bool touchLongPress = false;
  bool touchHeld = false;
  bool touchReleasedEdge = false;
  unsigned long touchHeldMs = 0;
  bool swipePending = false;
  float swipeStartNx = 0.0f;
  float swipeStartNy = 0.0f;
  float swipeEndNx = 0.0f;
  float swipeEndNy = 0.0f;
  int suppressCount = 0;
  int updateCount = 0;

  void reset() { *this = HalGPIO(); }

  // --- surface used by the compiled production code -------------------------
  void update() { updateCount++; }
  bool isPressed(const uint8_t i) const { return i < BUTTON_COUNT && down[i]; }
  bool wasPressed(const uint8_t i) const { return i < BUTTON_COUNT && pressedEdge[i]; }
  bool wasReleased(const uint8_t i) const { return i < BUTTON_COUNT && releasedEdge[i]; }
  bool wasAnyPressed() const {
    for (const bool b : pressedEdge) {
      if (b) return true;
    }
    return false;
  }
  bool wasAnyReleased() const {
    for (const bool b : releasedEdge) {
      if (b) return true;
    }
    return false;
  }
  unsigned long getHeldTime() const { return heldMs; }
  unsigned long getPowerButtonHeldTime() const { return powerHeldMs; }
  bool hasTouch() const { return touch; }
  bool hasHomeKey() const { return homeKey; }
  bool wasHomeKeyTapped() const { return homeKeyTapped; }
  bool wasHomeKeyLongPressed() const { return homeKeyLong; }
  bool wasTouchTap(float& nx, float& ny) const {
    if (!tapPending) return false;
    nx = tapNx;
    ny = tapNy;
    return true;
  }
  bool wasTouchDown(float&, float&) const { return false; }
  bool wasTouchReleased() const { return touchReleasedEdge; }
  bool isTouchTapCandidate(float& nx, float& ny, unsigned long& held) const {
    if (!tapCandidate) return false;
    nx = tapNx;
    ny = tapNy;
    held = tapCandidateHeldMs;
    return true;
  }
  bool isTouchHeldAt(float& nx, float& ny) const {
    if (!touchHeld) return false;
    nx = tapNx;
    ny = tapNy;
    return true;
  }
  bool wasTouchLongPress(float& nx, float& ny) const {
    if (!touchLongPress) return false;
    nx = tapNx;
    ny = tapNy;
    return true;
  }
  void suppressTouchContact() { suppressCount++; }
  unsigned long lastTouchHeldMs() const { return touchHeldMs; }
  bool wasSwipe(float& sx, float& sy, float& ex, float& ey) const {
    if (!swipePending) return false;
    sx = swipeStartNx;
    sy = swipeStartNy;
    ex = swipeEndNx;
    ey = swipeEndNy;
    return true;
  }
};
