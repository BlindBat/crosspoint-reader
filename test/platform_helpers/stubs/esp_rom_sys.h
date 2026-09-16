#pragma once

// ROM console printf. Logging.cpp routes through it when the board selects
// FREEINK_LOG_TRANSPORT_ROM_PRINTF; host::romConsoleOutput() records it.
extern "C" void esp_rom_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
