// lib/hal/HalSystem: reset-reason classification, panic capture hooks and the
// crash report written on the next boot (FR-017).

#include <gtest/gtest.h>

#include <HalSystem.h>
#include <HostControls.h>
#include <Logging.h>

#include <cstring>
#include <string>
#include <vector>

#include "TestSupport.h"

// RTC_NOINIT diagnostics owned by HalSystem.cpp.
extern char panicMessage[256];
extern HalSystem::StackFrame panicStack[32];
extern volatile uint32_t panicCaptureMarker;

// The linker --wrap targets HalSystem's hooks chain into; recorded here.
namespace {
int realAbortCalls = 0;
std::string realAbortMessage;
int realBacktraceCalls = 0;
}  // namespace

extern "C" void __real_panic_abort(const char* message) {
  realAbortCalls++;
  realAbortMessage = message ? message : "<null>";
}
extern "C" void __real_panic_print_backtrace(const void*, int) { realBacktraceCalls++; }

extern "C" void __wrap_panic_abort(const char* message);
extern "C" void __wrap_panic_print_backtrace(const void* frame, int core);

namespace {

constexpr uint32_t kMarker = 0x50414E49u;

class HalSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host::resetAll();
    realAbortCalls = 0;
    realAbortMessage.clear();
    realBacktraceCalls = 0;
    host::setResetReason(ESP_RST_POWERON);
    HalSystem::clearPanic();
  }
  ScopedStorageRoot storage;
};

void simulatePanicReboot(const char* message) {
  __wrap_panic_abort(message);
  host::setResetReason(ESP_RST_PANIC);
}

}  // namespace

/* ---------- reset classification ---------- */

TEST_F(HalSystemTest, PanicAndLockupResetsAreCrashesWithoutMarker) {
  panicCaptureMarker = 0;
  host::setResetReason(ESP_RST_PANIC);
  EXPECT_TRUE(HalSystem::isRebootFromPanic());
  host::setResetReason(ESP_RST_CPU_LOCKUP);
  EXPECT_TRUE(HalSystem::isRebootFromPanic());
}

TEST_F(HalSystemTest, WatchdogResetsAreCrashesOnlyWithMarker) {
  for (auto reason : {ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT}) {
    host::setResetReason(reason);
    panicCaptureMarker = 0;
    EXPECT_FALSE(HalSystem::isRebootFromPanic()) << reason;
    panicCaptureMarker = kMarker;
    EXPECT_TRUE(HalSystem::isRebootFromPanic()) << reason;
    panicCaptureMarker = kMarker ^ 1;
    EXPECT_FALSE(HalSystem::isRebootFromPanic()) << reason;
  }
}

TEST_F(HalSystemTest, OrdinaryResetsAreNeverCrashesEvenWithMarker) {
  panicCaptureMarker = kMarker;
  for (auto reason : {ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_EXT, ESP_RST_UNKNOWN}) {
    host::setResetReason(reason);
    EXPECT_FALSE(HalSystem::isRebootFromPanic()) << reason;
  }
}

/* ---------- panic hooks ---------- */

TEST_F(HalSystemTest, PanicAbortHookCapturesMessageAndMarkerThenChains) {
  __wrap_panic_abort("assert failed: x");
  EXPECT_STREQ(panicMessage, "assert failed: x");
  EXPECT_EQ(panicCaptureMarker, kMarker);
  EXPECT_EQ(realAbortCalls, 1);
  EXPECT_EQ(realAbortMessage, "assert failed: x");
}

TEST_F(HalSystemTest, PanicAbortHookSubstitutesUnknownReasonForNull) {
  __wrap_panic_abort(nullptr);
  EXPECT_STREQ(panicMessage, "(unknown panic reason)");
  EXPECT_EQ(realAbortMessage, "(unknown panic reason)");
}

TEST_F(HalSystemTest, PanicAbortHookTruncatesLongMessage) {
  const std::string big(400, 'm');
  __wrap_panic_abort(big.c_str());
  EXPECT_EQ(strlen(panicMessage), 255u);
  EXPECT_EQ(std::string(panicMessage), big.substr(0, 255));
}

TEST_F(HalSystemTest, BacktraceHookWithNullFrameOnlyChains) {
  __wrap_panic_print_backtrace(nullptr, 0);
  EXPECT_EQ(realBacktraceCalls, 1);
  EXPECT_EQ(panicCaptureMarker, 0u);
}

TEST_F(HalSystemTest, BacktraceHookWithUnsafeStackPointerLeavesNoCapture) {
  uint32_t frame[8] = {};
  host::setStackSane(false);
  __wrap_panic_print_backtrace(frame, 0);
  EXPECT_EQ(realBacktraceCalls, 1);
  EXPECT_EQ(panicCaptureMarker, 0u);
  for (const auto& f : panicStack) EXPECT_EQ(f.sp, 0u);

  host::setStackSane(true);
  host::setPtrInDram(false);  // window would leave DRAM
  __wrap_panic_print_backtrace(frame, 0);
  EXPECT_EQ(realBacktraceCalls, 2);
  EXPECT_EQ(panicCaptureMarker, 0u);
}

TEST_F(HalSystemTest, BacktraceHookRejectsAStackPointerThatWouldOverflowTheWindow) {
  // a1/sp sits so close to the top of the address space that sp + 1024 wraps.
  // The range check must reject it before the window is read.
  uint32_t frame[8] = {};
  frame[4] = 0xFFFFFFFFu;  // XtExcFrame::a1
  host::setStackSane(true);
  host::setPtrInDram(true);  // only the overflow guard can refuse this frame
  __wrap_panic_print_backtrace(frame, 0);
  EXPECT_EQ(realBacktraceCalls, 1);
  EXPECT_EQ(panicCaptureMarker, 0u);
  for (const auto& f : panicStack) EXPECT_EQ(f.sp, 0u);
}

/* ---------- report contents ---------- */

TEST_F(HalSystemTest, ShortPanicInfoIsJustTheMessage) {
  __wrap_panic_abort("boom");
  EXPECT_EQ(HalSystem::getPanicInfo(), "boom");
  EXPECT_EQ(HalSystem::getPanicInfo(false), "boom");
}

TEST_F(HalSystemTest, FullPanicInfoContainsVersionReasonMessageLogsAndStack) {
  clearLastLogs();
  LOG_ERR("T", "last words");
  simulatePanicReboot("boom");
  panicStack[0] = {0x3FC80000u, {1, 2, 3, 4, 5, 6, 7, 0xDEADBEEFu}};
  panicStack[1] = {0x3FC80020u, {8, 9, 10, 11, 12, 13, 14, 15}};
  panicStack[2].sp = 0;  // terminator: later frames must not be printed
  panicStack[3] = {0x3FC80040u, {0, 0, 0, 0, 0, 0, 0, 0}};

  const std::string info = HalSystem::getPanicInfo(true);
  EXPECT_NE(info.find("CrossPoint version: host-test"), std::string::npos);
  EXPECT_NE(info.find("\n\nReset reason: PANIC (exception/abort)"), std::string::npos);
  EXPECT_NE(info.find("\n\nPanic reason: boom"), std::string::npos);
  EXPECT_NE(info.find("\n\nLast logs:\n[0] [ERR] [T] last words\n"), std::string::npos);
  EXPECT_NE(info.find("\n\nStack memory:\n0x3FC80000: 0x00000001 0x00000002 0x00000003 0x00000004 0x00000005 "
                      "0x00000006 0x00000007 0xDEADBEEF \n0x3FC80020: 0x00000008"),
            std::string::npos);
  EXPECT_EQ(info.find("0x3FC80040"), std::string::npos);
}

TEST_F(HalSystemTest, FullPanicInfoNamesEveryResetReason) {
  const std::vector<std::pair<esp_reset_reason_t, const char*>> names = {
      {ESP_RST_PANIC, "PANIC (exception/abort)"}, {ESP_RST_CPU_LOCKUP, "CPU_LOCKUP"}, {ESP_RST_INT_WDT, "INT_WDT"},
      {ESP_RST_TASK_WDT, "TASK_WDT"},             {ESP_RST_WDT, "WDT (other)"},       {ESP_RST_BROWNOUT, "BROWNOUT"},
      {ESP_RST_POWERON, "POWERON"},               {ESP_RST_SW, "SW"},                 {ESP_RST_DEEPSLEEP, "DEEPSLEEP"},
      {ESP_RST_EXT, "OTHER"},                     {ESP_RST_UNKNOWN, "OTHER"}};
  for (const auto& [reason, name] : names) {
    host::setResetReason(reason);
    EXPECT_NE(HalSystem::getPanicInfo(true).find(std::string("Reset reason: ") + name + "\n"), std::string::npos)
        << name;
  }
}

/* ---------- begin() / checkPanic() ---------- */

TEST_F(HalSystemTest, OrdinaryBootClearsStaleDiagnostics) {
  simulatePanicReboot("stale");
  panicStack[0].sp = 0x1000;
  LOG_ERR("T", "old log");
  host::setResetReason(ESP_RST_POWERON);
  HalSystem::begin();
  EXPECT_EQ(panicCaptureMarker, 0u);
  EXPECT_STREQ(panicMessage, "");
  EXPECT_EQ(panicStack[0].sp, 0u);
  EXPECT_EQ(getLastLogs(), "");
  EXPECT_FALSE(HalSystem::isRebootFromPanic());
}

TEST_F(HalSystemTest, PanicBootPreservesDiagnosticsAndLogs) {
  clearLastLogs();
  LOG_ERR("T", "before crash");
  simulatePanicReboot("boom");
  HalSystem::begin();
  EXPECT_EQ(panicCaptureMarker, kMarker);
  EXPECT_STREQ(panicMessage, "boom");
  EXPECT_EQ(getLastLogs(), "[0] [ERR] [T] before crash\n");
}

TEST_F(HalSystemTest, PanicBootWithCorruptLogRingDropsTheLogsOnly) {
  LOG_ERR("T", "garbage");
  simulatePanicReboot("boom");
  extern size_t logHead;
  logHead = 99;
  HalSystem::begin();
  EXPECT_EQ(getLastLogs(), "");
  EXPECT_STREQ(panicMessage, "boom");
  EXPECT_EQ(panicCaptureMarker, kMarker);
}

TEST_F(HalSystemTest, CheckPanicWritesCrashReportAndClearsMarker) {
  clearLastLogs();
  LOG_INF("T", "hi");
  simulatePanicReboot("boom");
  HalSystem::begin();
  const std::string expected = HalSystem::getPanicInfo(true);
  HalSystem::checkPanic();
  ASSERT_TRUE(storage.exists("/crash_report.txt"));
  const auto bytes = storage.get("/crash_report.txt");
  const std::string report(bytes.begin(), bytes.end());
  EXPECT_EQ(report, expected);
  EXPECT_NE(report.find("Panic reason: boom"), std::string::npos);
  EXPECT_NE(report.find("[INF] [T] hi"), std::string::npos);
  EXPECT_EQ(panicCaptureMarker, 0u);
  EXPECT_STREQ(panicMessage, "boom");  // kept for the crash screen
}

TEST_F(HalSystemTest, CheckPanicDoesNothingOnOrdinaryBoot) {
  HalSystem::checkPanic();
  EXPECT_FALSE(storage.exists("/crash_report.txt"));
  EXPECT_EQ(Storage.writeOpens, 0);
}

TEST_F(HalSystemTest, CheckPanicKeepsMarkerWhenReportCannotBeOpened) {
  simulatePanicReboot("boom");
  Storage.failNextOpen = true;
  HalSystem::checkPanic();
  EXPECT_FALSE(storage.exists("/crash_report.txt"));
  EXPECT_EQ(panicCaptureMarker, kMarker);
}

TEST_F(HalSystemTest, CheckPanicKeepsMarkerAfterShortWrite) {
  simulatePanicReboot("boom");
  Storage.writeCap = 10;
  HalSystem::checkPanic();
  EXPECT_EQ(panicCaptureMarker, kMarker);
  EXPECT_TRUE(HalSystem::isRebootFromPanic());  // a later watchdog reboot would retry the dump
}

TEST_F(HalSystemTest, ClearPanicResetsEverything) {
  simulatePanicReboot("boom");
  panicStack[5].sp = 7;
  LOG_ERR("T", "x");
  HalSystem::clearPanic();
  EXPECT_EQ(panicCaptureMarker, 0u);
  EXPECT_STREQ(panicMessage, "");
  EXPECT_EQ(panicStack[5].sp, 0u);
  EXPECT_EQ(getLastLogs(), "");
  EXPECT_EQ(HalSystem::getPanicInfo(), "");
}
