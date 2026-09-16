// lib/hal/HalClock: cached RTC polling, UTC offset / 12h formatting and the
// NTP sync gate, over a scripted SDK Rtc.

#include <gtest/gtest.h>

#include <HalClock.h>
#include <HostControls.h>
#include <Rtc.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstring>
#include <ctime>
#include <string>

namespace {

class HalClockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    Rtc::hostReset();
    WiFi.hostStatus = WL_DISCONNECTED;
    hostSntpStatus = SNTP_SYNC_STATUS_RESET;
    host::setMillis(1000);  // non-zero so the poll timestamp is meaningful
    clock = HalClock{};
  }

  void setRtc(uint8_t hour, uint8_t minute) {
    Rtc::hostNow.hour = hour;
    Rtc::hostNow.minute = minute;
  }

  std::string format(uint8_t offset = 48, bool use12Hour = false) {
    char buf[16];
    memset(buf, '#', sizeof(buf));
    if (!clock.formatTime(buf, sizeof(buf), offset, use12Hour)) return "<false>";
    return buf;
  }

  HalClock clock;
};

}  // namespace

TEST_F(HalClockTest, BeginReportsRtcPresence) {
  Rtc::hostPresent = false;
  clock.begin();
  EXPECT_FALSE(clock.isAvailable());
  uint8_t h, m;
  EXPECT_FALSE(clock.getTime(h, m));
  EXPECT_EQ(format(), "<false>");

  Rtc::hostPresent = true;
  clock.begin();
  EXPECT_TRUE(clock.isAvailable());
}

TEST_F(HalClockTest, GetTimeReadsRtcThenServesCacheForTenSeconds) {
  clock.begin();
  setRtc(9, 30);
  uint8_t h = 0, m = 0;
  ASSERT_TRUE(clock.getTime(h, m));
  EXPECT_EQ(h, 9);
  EXPECT_EQ(m, 30);
  EXPECT_EQ(Rtc::hostNowCalls, 1);

  setRtc(9, 31);
  host::setMillis(1000 + 9999);
  ASSERT_TRUE(clock.getTime(h, m));
  EXPECT_EQ(m, 30);  // cached
  EXPECT_EQ(Rtc::hostNowCalls, 1);

  host::setMillis(1000 + 10000);
  ASSERT_TRUE(clock.getTime(h, m));
  EXPECT_EQ(m, 31);
  EXPECT_EQ(Rtc::hostNowCalls, 2);
}

TEST_F(HalClockTest, RtcReadFailureFallsBackToCachedTime) {
  clock.begin();
  setRtc(7, 5);
  uint8_t h = 0, m = 0;
  ASSERT_TRUE(clock.getTime(h, m));
  Rtc::hostNowOk = false;
  host::setMillis(1000 + 20000);
  ASSERT_TRUE(clock.getTime(h, m));
  EXPECT_EQ(h, 7);
  EXPECT_EQ(m, 5);
}

TEST_F(HalClockTest, RtcReadFailureWithoutCacheFails) {
  clock.begin();
  Rtc::hostNowOk = false;
  uint8_t h, m;
  EXPECT_FALSE(clock.getTime(h, m));
  EXPECT_EQ(format(), "<false>");
}

TEST_F(HalClockTest, FormatTimeRejectsSmallBuffers) {
  clock.begin();
  setRtc(1, 2);
  char buf[9];
  EXPECT_FALSE(clock.formatTime(buf, 5, 48, false));
  EXPECT_TRUE(clock.formatTime(buf, 6, 48, false));
  EXPECT_STREQ(buf, "01:02");
  EXPECT_FALSE(clock.formatTime(buf, 8, 48, true));
  EXPECT_TRUE(clock.formatTime(buf, 9, 48, true));
  EXPECT_STREQ(buf, "1:02 AM");
}

TEST_F(HalClockTest, FormatsTwentyFourHourWithZeroPadding) {
  clock.begin();
  setRtc(0, 0);
  EXPECT_EQ(format(), "00:00");
  setRtc(23, 59);
  host::setMillis(1000 + 60000);
  EXPECT_EQ(format(), "23:59");
}

TEST_F(HalClockTest, AppliesQuarterHourUtcOffsets) {
  clock.begin();
  setRtc(12, 0);
  EXPECT_EQ(format(48), "12:00");   // UTC+0
  EXPECT_EQ(format(49), "12:15");   // +0:15
  EXPECT_EQ(format(52), "13:00");   // +1:00
  EXPECT_EQ(format(46), "11:30");   // -0:30
  EXPECT_EQ(format(0), "00:00");    // -12:00
  EXPECT_EQ(format(104), "02:00");  // +14:00 wraps past midnight
}

TEST_F(HalClockTest, NegativeOffsetWrapsBackwardsAcrossMidnight) {
  clock.begin();
  setRtc(0, 30);
  EXPECT_EQ(format(44), "23:30");  // -1:00
  EXPECT_EQ(format(0), "12:30");   // -12:00
}

TEST_F(HalClockTest, ClampsCorruptOffsetToPlusFourteen) {
  clock.begin();
  setRtc(0, 0);
  EXPECT_EQ(format(104), "14:00");
  EXPECT_EQ(format(105), "14:00");
  EXPECT_EQ(format(255), "14:00");
}

TEST_F(HalClockTest, TwelveHourFormatUsesTwelveForMidnightAndNoon) {
  clock.begin();
  setRtc(0, 5);
  EXPECT_EQ(format(48, true), "12:05 AM");
  setRtc(12, 5);
  host::setMillis(1000 + 60000);
  EXPECT_EQ(format(48, true), "12:05 PM");
  setRtc(13, 7);
  host::setMillis(1000 + 120000);
  EXPECT_EQ(format(48, true), "1:07 PM");
  setRtc(23, 59);
  host::setMillis(1000 + 180000);
  EXPECT_EQ(format(48, true), "11:59 PM");
}

TEST_F(HalClockTest, TwelveHourFormatAppliesOffsetBeforeSuffix) {
  clock.begin();
  setRtc(11, 50);
  EXPECT_EQ(format(49, true), "12:05 PM");  // +15 min crosses noon
  EXPECT_EQ(format(0, true), "11:50 PM");   // -12h crosses midnight
}

TEST_F(HalClockTest, SyncFromNtpRequiresRtcAndWifi) {
  Rtc::hostPresent = false;
  clock.begin();
  EXPECT_FALSE(clock.syncFromNTP());

  Rtc::hostPresent = true;
  clock.begin();
  WiFi.hostStatus = WL_DISCONNECTED;
  EXPECT_FALSE(clock.syncFromNTP());
  EXPECT_EQ(Rtc::hostSetCalls, 0);
}

TEST_F(HalClockTest, SyncFromNtpTimesOutAfterFiftyPolls) {
  clock.begin();
  WiFi.hostStatus = WL_CONNECTED;
  hostSntpStatus = SNTP_SYNC_STATUS_IN_PROGRESS;
  EXPECT_FALSE(clock.syncFromNTP());
  EXPECT_EQ(host::delayedMs(), 5000u);
  EXPECT_EQ(Rtc::hostSetCalls, 0);
}

TEST_F(HalClockTest, SyncFromNtpSetsRtcFromUtcAndRefreshesCache) {
  clock.begin();
  setRtc(1, 1);
  uint8_t h = 0, m = 0;
  ASSERT_TRUE(clock.getTime(h, m));  // prime the cache with the old time

  WiFi.hostStatus = WL_CONNECTED;
  hostSntpStatus = SNTP_SYNC_STATUS_COMPLETED;
  const time_t before = time(nullptr);
  ASSERT_TRUE(clock.syncFromNTP());
  EXPECT_EQ(Rtc::hostSetCalls, 1);

  struct tm utc;
  gmtime_r(&before, &utc);
  EXPECT_EQ(Rtc::hostLastSet.year, utc.tm_year + 1900);
  EXPECT_EQ(Rtc::hostLastSet.month, utc.tm_mon + 1);
  EXPECT_EQ(Rtc::hostLastSet.weekday, utc.tm_wday);

  // The cache now holds the synced time without another RTC poll.
  const int polls = Rtc::hostNowCalls;
  ASSERT_TRUE(clock.getTime(h, m));
  EXPECT_EQ(h, Rtc::hostLastSet.hour);
  EXPECT_EQ(m, Rtc::hostLastSet.minute);
  EXPECT_EQ(Rtc::hostNowCalls, polls + 1);  // _lastPollMs reset forces one fresh read
}

TEST_F(HalClockTest, SyncFromNtpFailsWhenRtcRejectsTheTime) {
  clock.begin();
  WiFi.hostStatus = WL_CONNECTED;
  hostSntpStatus = SNTP_SYNC_STATUS_COMPLETED;
  Rtc::hostSetOk = false;
  EXPECT_FALSE(clock.syncFromNTP());
  EXPECT_EQ(Rtc::hostSetCalls, 1);
}
