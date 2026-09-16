#pragma once

// Host boards log over the plain serial transport (never the ROM console).
#define FREEINK_LOG_TRANSPORT_SERIAL 0
#define FREEINK_LOG_TRANSPORT_USB_CDC_WRITE 1
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 2
#define FREEINK_LOG_TRANSPORT FREEINK_LOG_TRANSPORT_SERIAL
