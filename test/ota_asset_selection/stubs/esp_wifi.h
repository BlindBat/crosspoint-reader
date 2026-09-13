#pragma once

// Host stand-in for the WiFi power-save toggles OtaUpdater flips around the
// download.

typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1, WIFI_PS_MAX_MODEM = 2 } wifi_ps_type_t;

inline int esp_wifi_set_ps(wifi_ps_type_t) { return 0; }
