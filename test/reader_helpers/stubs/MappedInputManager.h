#pragma once

// Scripted MappedInputManager stand-in: tests set the per-frame input snapshot
// directly. Button/SwipeDir enums mirror src/MappedInputManager.h. The real
// wasLongPressed() fires once and suppresses the matching release; here the
// test script models that by setting either longPressed or released, never both.

#include <array>

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };

  static constexpr size_t BUTTON_COUNT = 15;

  std::array<bool, BUTTON_COUNT> pressed{};
  std::array<bool, BUTTON_COUNT> released{};
  std::array<bool, BUTTON_COUNT> longPressed{};
  unsigned long heldTime = 0;
  bool navSwapped = false;
  bool touch = false;
  bool tapped = false;
  int tapX = 0;
  int tapY = 0;
  SwipeDir swipe = SwipeDir::None;
  bool menuGesture = false;
  bool readerMenuSwipeUp = false;
  bool backGesture = false;
  mutable unsigned long lastLongPressThreshold = 0;

  void press(const Button b) { pressed[static_cast<size_t>(b)] = true; }
  void release(const Button b) { released[static_cast<size_t>(b)] = true; }
  void longPress(const Button b, const unsigned long heldMs) {
    longPressed[static_cast<size_t>(b)] = true;
    heldTime = heldMs;
  }

  bool wasPressed(const Button b) const { return pressed[static_cast<size_t>(b)]; }
  bool wasReleased(const Button b) const { return released[static_cast<size_t>(b)]; }
  bool wasLongPressed(const Button b, const unsigned long thresholdMs) const {
    lastLongPressThreshold = thresholdMs;
    return longPressed[static_cast<size_t>(b)] && heldTime >= thresholdMs;
  }
  bool isNavDirectionSwapped() const { return navSwapped; }
  bool hasTouch() const { return touch; }
  bool wasScreenTapped(int& x, int& y) const {
    if (!tapped) return false;
    x = tapX;
    y = tapY;
    return true;
  }
  SwipeDir wasSwipe() const { return swipe; }
  bool wasMenuGesture() const { return menuGesture; }
  bool wasReaderMenuSwipeUp() const { return readerMenuSwipeUp; }
  bool wasBackGesture() const { return backGesture; }
  unsigned long getHeldTime() const { return heldTime; }
};
