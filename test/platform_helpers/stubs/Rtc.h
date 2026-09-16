#pragma once

#include <cstdint>

// SDK RTC stand-in with a scriptable clock (see the host* members, defined
// below the class so DateTime's default member initializers are complete).
class Rtc {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

  bool begin() {
    begun_ = hostPresent;
    return begun_;
  }
  bool present() const { return begun_; }
  bool now(DateTime& out) {
    hostNowCalls++;
    if (!hostNowOk) return false;
    out = hostNow;
    return true;
  }
  bool set(const DateTime& dt) {
    hostSetCalls++;
    hostLastSet = dt;
    if (hostSetOk) hostNow = dt;
    return hostSetOk;
  }

  static bool hostPresent;
  static bool hostNowOk;
  static bool hostSetOk;
  static DateTime hostNow;
  static DateTime hostLastSet;
  static int hostNowCalls;
  static int hostSetCalls;

  static void hostReset();

 private:
  bool begun_ = false;
};

inline bool Rtc::hostPresent = true;
inline bool Rtc::hostNowOk = true;
inline bool Rtc::hostSetOk = true;
inline Rtc::DateTime Rtc::hostNow{};
inline Rtc::DateTime Rtc::hostLastSet{};
inline int Rtc::hostNowCalls = 0;
inline int Rtc::hostSetCalls = 0;

inline void Rtc::hostReset() {
  hostPresent = true;
  hostNowOk = true;
  hostSetOk = true;
  hostNow = DateTime{};
  hostLastSet = DateTime{};
  hostNowCalls = 0;
  hostSetCalls = 0;
}
