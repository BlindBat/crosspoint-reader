#pragma once

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

struct WiFiClass {
  int hostStatus = WL_DISCONNECTED;
  int status() const { return hostStatus; }
};

inline WiFiClass WiFi;
