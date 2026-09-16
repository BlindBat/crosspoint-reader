#pragma once

#include <cstdint>

// RISC-V exception frame: only the stack pointer is read by HalSystem.
struct RvExcFrame {
  uint32_t mepc;
  uint32_t ra;
  uint32_t sp;
};
