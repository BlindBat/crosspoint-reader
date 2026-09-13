#pragma once

// Host stub: vTaskDelay() is a cooperative-scheduler yield on device; the
// host suites run single-threaded, so it is a no-op.
inline void vTaskDelay(int /*ticks*/) {}
