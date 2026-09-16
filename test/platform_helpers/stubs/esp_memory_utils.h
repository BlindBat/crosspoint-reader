#pragma once

#include <cstdint>

// Pointer-class predicates used by the panic stack capture; the answers come
// from host::setStackSane() / host::setPtrInDram().
bool esp_stack_ptr_is_sane(uint32_t sp);
bool esp_ptr_in_dram(const void* p);
