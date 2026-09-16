#pragma once

// Scriptable MappedInputManager for the ButtonNavigator host tests. Tests set
// the per-frame edge/hold state directly; the Button enum mirrors production.

#include <Arduino.h>

#include <cstdint>
#include <set>

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

  // --- test controls -------------------------------------------------------
  std::set<Button> pressedEdge;
  std::set<Button> releasedEdge;
  std::set<Button> held;
  unsigned long heldTimeMs = 0;

  void clearFrame() {
    pressedEdge.clear();
    releasedEdge.clear();
    held.clear();
    heldTimeMs = 0;
  }

  // --- API surface used by ButtonNavigator ---------------------------------
  bool wasPressed(const Button button) const { return pressedEdge.count(button) != 0; }
  bool wasReleased(const Button button) const { return releasedEdge.count(button) != 0; }
  bool isPressed(const Button button) const { return held.count(button) != 0; }
  unsigned long getHeldTime() const { return heldTimeMs; }
};
