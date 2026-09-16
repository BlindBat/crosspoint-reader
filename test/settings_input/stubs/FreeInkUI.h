#pragma once

// Host stand-in for the SDK umbrella header: KeyboardLayoutSet only needs the
// layout id enum. Values mirror components/keyboard/keyboard.h.

#include <cstdint>

namespace freeink::ui {

enum class KeyboardLayoutId : uint8_t {
  QwertyEn,
  AzertyFr,
  QwertzDe,
  SpanishEs,
  CyrillicRu,
  CyrillicUk,
  CyrillicBe,
  CyrillicKk,
  HebrewIl,
};

}  // namespace freeink::ui
