#pragma once

#include <cstdint>

// Xtensa exception frame: HalSystem reads a1 (the stack pointer).
struct XtExcFrame {
  uint32_t exit;
  uint32_t pc;
  uint32_t ps;
  uint32_t a0;
  uint32_t a1;
};
