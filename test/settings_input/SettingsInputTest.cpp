// Host tests for the settings/input layer (spec FR-125..FR-141):
//  - MappedInputManager compiled against a settable HalGPIO/GfxRenderer stub:
//    remap table, fixed side/power buttons, page/nav buttons, the live axis
//    swap, screen-direction rotation, long-press one-shot + release
//    suppression, gesture meaning, hint labels, and — built with
//    FREEINK_CAP_TOUCH=1 — the power-click-as-Confirm policy.
//  - KeyboardLayoutSet: enabled mask defaulting, Latin floor, cycling.
//  - clock_offset codec, quick_resume sync and utf8_cursor helpers (pure).
//  - CrossPointSettings derived getters (the JSON round trip lives in
//    persistable_stores and is not repeated here).
//  - I18n lookup: bit-15 English fallback, bounds.
//  - HalTiltSensor gesture state machine over a scripted IMU.
//
// CrossPointSettings and I18n are singletons; every fixture re-establishes
// the fields it depends on rather than assuming defaults.

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Imu.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include "CrossPointSettings.h"
#include "I18nStrings.h"
#include "MappedInputManager.h"
#include "activities/settings/ClockOffsetCodec.h"
#include "activities/settings/QuickResumeSync.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "activities/util/Utf8Cursor.h"
#include "fontIds.h"

namespace {

using namespace i18n_strings;  // NOLINT: generated tables
using Button = MappedInputManager::Button;
using LayoutId = freeink::ui::KeyboardLayoutId;

// Per-process storage root: ctest runs tests in parallel in one directory.
std::string processStoreRoot() { return "settings_input_root_" + std::to_string(::getpid()); }

void removeTree(const std::string& path) {
  const std::string cmd = "rm -rf '" + path + "'";
  [[maybe_unused]] const int rc = std::system(cmd.c_str());
}

class StoreRootCleanup : public ::testing::Environment {
 public:
  void TearDown() override { removeTree(processStoreRoot()); }
};

const ::testing::Environment* const storeRootCleanup = ::testing::AddGlobalTestEnvironment(new StoreRootCleanup);

// Reset every CrossPointSettings field this suite reads to its documented default.
void resetSettings() {
  auto& s = SETTINGS;
  s.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  s.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  s.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  s.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  s.sideButtonLayout = CrossPointSettings::PREV_NEXT;
  s.frontButtonFollowOrientation = 0;
  s.shortPwrBtn = CrossPointSettings::IGNORE;
  s.keyboardLayouts = 0;
  s.sleepTimeoutMinutes = 10;
  s.refreshFrequency = CrossPointSettings::REFRESH_15;
  s.fontFamily = CrossPointSettings::NOTOSERIF;
  s.fontPointSize = CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  s.sdFontFamilyName[0] = '\0';
  s.sdFontIdResolver = nullptr;
  s.sdFontResolverCtx = nullptr;
  s.lineSpacing = CrossPointSettings::NORMAL;
  s.extraParagraphSpacing = 1;
  s.paragraphAlignment = CrossPointSettings::JUSTIFIED;
  s.hyphenationEnabled = 0;
  s.embeddedStyle = 1;
  s.imageRendering = CrossPointSettings::IMAGES_DISPLAY;
  s.focusReadingEnabled = 0;
  s.sleepScreen = CrossPointSettings::DARK;
  s.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_NEVER;
  I18N.setLanguage(Language::EN);
}

// ---------------------------------------------------------------------------
// clock_offset codec
// ---------------------------------------------------------------------------

TEST(ClockOffsetCodec, UtcZeroEncodesToBias) {
  EXPECT_EQ(clock_offset::encode(0, 0, 0), 48);
  EXPECT_EQ(clock_offset::encode(1, 0, 0), 48);  // -0:00 is still UTC+0
}

TEST(ClockOffsetCodec, RoundTripsEveryStoredValue) {
  for (int biased = 0; biased <= clock_offset::MAX_ENCODED; ++biased) {
    uint8_t sign = 9, hours = 99, quarter = 99;
    clock_offset::decode(static_cast<uint8_t>(biased), sign, hours, quarter);
    EXPECT_LE(sign, 1);
    EXPECT_LT(quarter, clock_offset::MINUTE_STEPS);
    EXPECT_EQ(clock_offset::encode(sign, hours, quarter), biased) << "biased=" << biased;
  }
}

TEST(ClockOffsetCodec, DecodesKnownZones) {
  uint8_t sign, hours, quarter;
  clock_offset::decode(71, sign, hours, quarter);  // Nepal +5:45
  EXPECT_EQ(sign, 0);
  EXPECT_EQ(hours, 5);
  EXPECT_EQ(quarter, 3);
  clock_offset::decode(99, sign, hours, quarter);  // Chatham +12:45
  EXPECT_EQ(sign, 0);
  EXPECT_EQ(hours, 12);
  EXPECT_EQ(quarter, 3);
  clock_offset::decode(0, sign, hours, quarter);  // UTC-12:00
  EXPECT_EQ(sign, 1);
  EXPECT_EQ(hours, 12);
  EXPECT_EQ(quarter, 0);
  clock_offset::decode(104, sign, hours, quarter);  // UTC+14:00
  EXPECT_EQ(sign, 0);
  EXPECT_EQ(hours, 14);
  EXPECT_EQ(quarter, 0);
  clock_offset::decode(30, sign, hours, quarter);  // UTC-4:30
  EXPECT_EQ(sign, 1);
  EXPECT_EQ(hours, 4);
  EXPECT_EQ(quarter, 2);
}

TEST(ClockOffsetCodec, EncodeClampsToStorageRange) {
  EXPECT_EQ(clock_offset::encode(1, 14, 0), 0);    // -14:00 is below -12:00
  EXPECT_EQ(clock_offset::encode(1, 12, 3), 0);    // -12:45
  EXPECT_EQ(clock_offset::encode(0, 14, 3), 104);  // +14:45
  EXPECT_EQ(clock_offset::encode(0, 63, 3), 104);  // hostile hours
}

TEST(ClockOffsetCodec, DecodeOutOfRangeFallsBackToUtcZero) {
  for (const uint8_t bad : {uint8_t{105}, uint8_t{128}, uint8_t{255}}) {
    uint8_t sign = 1, hours = 1, quarter = 1;
    clock_offset::decode(bad, sign, hours, quarter);
    EXPECT_EQ(sign, 0) << int(bad);
    EXPECT_EQ(hours, 0) << int(bad);
    EXPECT_EQ(quarter, 0) << int(bad);
  }
}

// clockUtcOffsetQ is one persisted byte from a hand-editable JSON file, so the
// codec has to be total over the whole domain, not just the valid range.
TEST(ClockOffsetCodec, DecodeIsTotalOverEveryByte) {
  for (int raw = 0; raw <= 255; ++raw) {
    uint8_t sign = 9, hours = 99, quarter = 99;
    clock_offset::decode(static_cast<uint8_t>(raw), sign, hours, quarter);
    ASSERT_LE(sign, 1) << raw;
    ASSERT_LT(quarter, clock_offset::MINUTE_STEPS) << raw;
    ASSERT_LE(hours, clock_offset::MAX_POS_HOURS) << raw;
    const uint8_t reencoded = clock_offset::encode(sign, hours, quarter);
    ASSERT_LE(reencoded, clock_offset::MAX_ENCODED) << raw;
    // In range it round-trips; out of range it collapses onto UTC+0.
    ASSERT_EQ(reencoded, raw <= clock_offset::MAX_ENCODED ? raw : clock_offset::BIAS_QUARTER_HOURS) << raw;
  }
}

TEST(ClockOffsetCodec, ClampForSignCapsHoursAndBoundaryMinutes) {
  uint8_t hours = 14, quarter = 2;
  clock_offset::clampForSign(1, hours, quarter);  // negative: max 12, :00 only
  EXPECT_EQ(hours, 12);
  EXPECT_EQ(quarter, 0);

  hours = 14, quarter = 2;
  clock_offset::clampForSign(0, hours, quarter);  // +14:30 -> +14:00
  EXPECT_EQ(hours, 14);
  EXPECT_EQ(quarter, 0);

  hours = 13, quarter = 3;
  clock_offset::clampForSign(0, hours, quarter);  // +13:45 untouched
  EXPECT_EQ(hours, 13);
  EXPECT_EQ(quarter, 3);

  hours = 11, quarter = 3;
  clock_offset::clampForSign(1, hours, quarter);  // -11:45 untouched
  EXPECT_EQ(hours, 11);
  EXPECT_EQ(quarter, 3);
}

TEST(ClockOffsetCodec, MaxHoursPerSign) {
  EXPECT_EQ(clock_offset::maxHoursForSign(0), 14);
  EXPECT_EQ(clock_offset::maxHoursForSign(1), 12);
}

// ---------------------------------------------------------------------------
// quick_resume::syncTimeoutForSleepScreen
// ---------------------------------------------------------------------------

struct QuickResumeState {
  uint8_t sleepScreen = CrossPointSettings::DARK;
  uint8_t timeout = CrossPointSettings::QUICK_RESUME_NEVER;
  bool preserve = false;
  bool autoEnabled = false;

  void sync(const bool sleepScreenChanged, const bool timeoutChanged) {
    quick_resume::syncTimeoutForSleepScreen(sleepScreen, timeout, preserve, autoEnabled, sleepScreenChanged,
                                            timeoutChanged);
  }
};

TEST(QuickResumeSync, SelectingQuickResumeAutoEnablesTimeout) {
  QuickResumeState st;
  st.sleepScreen = CrossPointSettings::QUICK_RESUME;
  st.sync(true, false);
  EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT);
  EXPECT_TRUE(st.autoEnabled);
  EXPECT_FALSE(st.preserve);
}

TEST(QuickResumeSync, LeavingQuickResumeRevertsAutoEnabledTimeout) {
  QuickResumeState st;
  st.sleepScreen = CrossPointSettings::QUICK_RESUME;
  st.sync(true, false);
  st.sleepScreen = CrossPointSettings::COVER;
  st.sync(true, false);
  EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_NEVER);
  EXPECT_FALSE(st.autoEnabled);
}

TEST(QuickResumeSync, UserChosenTimeoutSurvivesLeavingQuickResume) {
  QuickResumeState st;
  st.timeout = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
  st.preserve = true;  // captured on entry: the user had it on
  st.sleepScreen = CrossPointSettings::QUICK_RESUME;
  st.sync(true, false);
  EXPECT_FALSE(st.autoEnabled);
  st.sleepScreen = CrossPointSettings::DARK;
  st.sync(true, false);
  EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT);
}

TEST(QuickResumeSync, TimeoutToggleRecapturesUserChoice) {
  QuickResumeState st;
  st.timeout = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
  st.autoEnabled = true;
  st.sync(false, true);
  EXPECT_TRUE(st.preserve);
  EXPECT_FALSE(st.autoEnabled);

  st.timeout = CrossPointSettings::QUICK_RESUME_NEVER;
  st.sync(false, true);
  EXPECT_FALSE(st.preserve);
}

TEST(QuickResumeSync, TurningTimeoutOffWhileOnQuickResumeIsForcedBackOn) {
  // Quick Resume needs the timeout, so the screen re-asserts it; the toggle
  // counts as auto-enabled again because the user's last choice was Off.
  QuickResumeState st;
  st.sleepScreen = CrossPointSettings::QUICK_RESUME;
  st.timeout = CrossPointSettings::QUICK_RESUME_NEVER;
  st.sync(false, true);
  EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT);
  EXPECT_TRUE(st.autoEnabled);
}

TEST(QuickResumeSync, UnchangedSleepScreenLeavesTimeoutAlone) {
  QuickResumeState st;
  st.timeout = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
  st.autoEnabled = true;
  st.sync(false, false);  // e.g. an unrelated setting changed
  EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT);
  EXPECT_TRUE(st.autoEnabled);
}

TEST(QuickResumeSync, EntryWithQuickResumeAndUnpreservedTimeoutMarksAuto) {
  // onEnter() path: timeout already on but not by choice.
  QuickResumeState st;
  st.sleepScreen = CrossPointSettings::QUICK_RESUME;
  st.timeout = CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
  st.sync(true, false);
  EXPECT_TRUE(st.autoEnabled);
}

TEST(QuickResumeSync, EveryNonQuickResumeSleepScreenRevertsTheSameWay) {
  for (uint8_t mode = 0; mode < CrossPointSettings::SLEEP_SCREEN_MODE_COUNT; ++mode) {
    if (mode == CrossPointSettings::QUICK_RESUME) continue;
    QuickResumeState st;
    st.sleepScreen = CrossPointSettings::QUICK_RESUME;
    st.sync(true, false);
    ASSERT_TRUE(st.autoEnabled) << int(mode);
    st.sleepScreen = mode;
    st.sync(true, false);
    EXPECT_EQ(st.timeout, CrossPointSettings::QUICK_RESUME_NEVER) << int(mode);
    EXPECT_FALSE(st.autoEnabled) << int(mode);
  }
}

// ---------------------------------------------------------------------------
// utf8_cursor
// ---------------------------------------------------------------------------

TEST(Utf8Cursor, PrevAndNextStepWholeCodePoints) {
  const std::string s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";  // a é € 😀
  EXPECT_EQ(utf8_cursor::next(s, 0), 1u);
  EXPECT_EQ(utf8_cursor::next(s, 1), 3u);
  EXPECT_EQ(utf8_cursor::next(s, 3), 6u);
  EXPECT_EQ(utf8_cursor::next(s, 6), 10u);
  EXPECT_EQ(utf8_cursor::prev(s, 10), 6u);
  EXPECT_EQ(utf8_cursor::prev(s, 6), 3u);
  EXPECT_EQ(utf8_cursor::prev(s, 3), 1u);
  EXPECT_EQ(utf8_cursor::prev(s, 1), 0u);
}

TEST(Utf8Cursor, EndsAreSticky) {
  const std::string s = "ab";
  EXPECT_EQ(utf8_cursor::prev(s, 0), 0u);
  EXPECT_EQ(utf8_cursor::next(s, 2), 2u);
  EXPECT_EQ(utf8_cursor::next(s, 50), 2u);
  EXPECT_EQ(utf8_cursor::next(std::string(), 0), 0u);
  EXPECT_EQ(utf8_cursor::prev(std::string(), 0), 0u);
}

TEST(Utf8Cursor, MalformedContinuationRunsAreSkippedNotOverrun) {
  const std::string lone = "\x80\x80\x80";  // continuation bytes with no lead
  EXPECT_EQ(utf8_cursor::prev(lone, 3), 0u);
  EXPECT_EQ(utf8_cursor::next(lone, 0), 3u);
  const std::string truncated = "a\xE2\x82";  // truncated 3-byte sequence
  EXPECT_EQ(utf8_cursor::next(truncated, 1), 3u);
  EXPECT_EQ(utf8_cursor::prev(truncated, 3), 1u);
}

// A cursor can only land mid-sequence if the text was replaced under it (paste,
// a restored draft); the helpers must still stay inside the buffer.
TEST(Utf8Cursor, CursorInsideACodePointStaysInBounds) {
  const std::string s = "a\xF0\x9F\x98\x80z";  // a 😀 z
  EXPECT_EQ(utf8_cursor::prev(s, 3), 1u);      // snaps back to the lead byte
  EXPECT_EQ(utf8_cursor::next(s, 3), 5u);      // runs out the continuation bytes

  std::string text = s;
  size_t cursor = 3;
  EXPECT_TRUE(utf8_cursor::backspace(text, cursor));
  EXPECT_EQ(text, "a\x98\x80z");  // only the bytes before the cursor go
  EXPECT_EQ(cursor, 1u);
}

TEST(Utf8Cursor, InsertAtCursorAdvancesPastText) {
  std::string text = "ac";
  size_t cursor = 1;
  EXPECT_TRUE(utf8_cursor::insert(text, cursor, "\xC3\xA9", 0));
  EXPECT_EQ(text,
            "a\xC3\xA9"
            "c");
  EXPECT_EQ(cursor, 3u);
}

TEST(Utf8Cursor, InsertHonoursByteLimit) {
  std::string text = "abc";
  size_t cursor = 3;
  EXPECT_TRUE(utf8_cursor::insert(text, cursor, "d", 4));  // exact fit
  EXPECT_FALSE(utf8_cursor::insert(text, cursor, "e", 4));
  EXPECT_FALSE(utf8_cursor::insert(text, cursor, "\xC3\xA9", 5));  // 2 bytes over a 1-byte gap
  EXPECT_EQ(text, "abcd");
  EXPECT_EQ(cursor, 4u);
}

TEST(Utf8Cursor, InsertClampsCursorPastEndAndRejectsEmpty) {
  std::string text = "ab";
  size_t cursor = 99;
  EXPECT_FALSE(utf8_cursor::insert(text, cursor, nullptr, 0));
  EXPECT_FALSE(utf8_cursor::insert(text, cursor, "", 0));
  EXPECT_EQ(cursor, 99u);  // refused inserts do not touch the cursor
  EXPECT_TRUE(utf8_cursor::insert(text, cursor, "c", 0));
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(cursor, 3u);
}

TEST(Utf8Cursor, BackspaceRemovesWholeCodePointBeforeCursor) {
  std::string text = "a\xF0\x9F\x98\x80z";
  size_t cursor = 5;
  EXPECT_TRUE(utf8_cursor::backspace(text, cursor));
  EXPECT_EQ(text, "az");
  EXPECT_EQ(cursor, 1u);
  EXPECT_TRUE(utf8_cursor::backspace(text, cursor));
  EXPECT_EQ(text, "z");
  EXPECT_EQ(cursor, 0u);
}

TEST(Utf8Cursor, BackspaceAtStartOrOnEmptyIsNoOp) {
  std::string text = "ab";
  size_t cursor = 0;
  EXPECT_FALSE(utf8_cursor::backspace(text, cursor));
  EXPECT_EQ(text, "ab");
  std::string empty;
  cursor = 0;
  EXPECT_FALSE(utf8_cursor::backspace(empty, cursor));
}

// ---------------------------------------------------------------------------
// keyboard_layouts
// ---------------------------------------------------------------------------

class KeyboardLayoutsTest : public ::testing::Test {
 protected:
  void SetUp() override { resetSettings(); }
  static uint16_t bit(const int i) { return keyboard_layouts::bitAt(static_cast<uint8_t>(i)); }
};

TEST_F(KeyboardLayoutsTest, TableOrderIsThePersistedBitAssignment) {
  ASSERT_EQ(keyboard_layouts::COUNT, 9);
  EXPECT_EQ(keyboard_layouts::ALL[0].id, LayoutId::QwertyEn);
  EXPECT_EQ(keyboard_layouts::ALL[4].id, LayoutId::CyrillicRu);
  EXPECT_EQ(keyboard_layouts::ALL[8].id, LayoutId::HebrewIl);
  EXPECT_EQ(keyboard_layouts::LATIN_BITS, 0x000F);
}

TEST_F(KeyboardLayoutsTest, UnconfiguredEnglishUiEnablesEnglishOnly) {
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0));
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::QwertyEn);
  EXPECT_EQ(keyboard_layouts::next(LayoutId::QwertyEn), LayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutsTest, UnconfiguredUiLanguageAddsItsLayoutToEnglish) {
  I18N.setLanguage(Language::DE);
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0) | bit(2));
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::QwertzDe);
  I18N.setLanguage(Language::UK);
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0) | bit(5));
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::CyrillicUk);
}

TEST_F(KeyboardLayoutsTest, UiLanguageWithoutLayoutTableOpensOnEnglish) {
  I18N.setLanguage(Language::IT);
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0));
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutsTest, ExplicitMaskIsUsedAsIsRegardlessOfUiLanguage) {
  I18N.setLanguage(Language::DE);
  SETTINGS.keyboardLayouts = bit(1) | bit(4);
  EXPECT_EQ(keyboard_layouts::enabled(), bit(1) | bit(4));
}

TEST_F(KeyboardLayoutsTest, MaskWithoutLatinLayoutGetsEnglishAdded) {
  SETTINGS.keyboardLayouts = bit(4) | bit(8);
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0) | bit(4) | bit(8));
}

TEST_F(KeyboardLayoutsTest, BitsNamingNoLayoutAreDropped) {
  SETTINGS.keyboardLayouts = static_cast<uint16_t>(0xFE00 | bit(1));
  EXPECT_EQ(keyboard_layouts::enabled(), bit(1));
  // Only unknown bits: reads as unconfigured, not as "nothing enabled".
  I18N.setLanguage(Language::FR);
  SETTINGS.keyboardLayouts = 0xFE00;
  EXPECT_EQ(keyboard_layouts::enabled(), bit(0) | bit(1));
}

// The mask survives downgrades and hand edits, so a saturated one must still
// resolve to this build's layouts and keep cycling inside the table.
TEST_F(KeyboardLayoutsTest, AllBitsSetKeepsOnlyKnownLayouts) {
  SETTINGS.keyboardLayouts = 0xFFFF;
  const uint16_t all = static_cast<uint16_t>((1u << keyboard_layouts::COUNT) - 1);
  EXPECT_EQ(keyboard_layouts::enabled(), all);
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::QwertyEn);
  LayoutId id = LayoutId::QwertyEn;
  for (uint8_t i = 1; i < keyboard_layouts::COUNT; ++i) {
    id = keyboard_layouts::next(id);
    EXPECT_EQ(id, keyboard_layouts::ALL[i].id) << int(i);
  }
  EXPECT_EQ(keyboard_layouts::next(id), LayoutId::QwertyEn);  // wraps
}

TEST_F(KeyboardLayoutsTest, StartingLayoutSkipsToNextWhenUiLayoutDisabled) {
  I18N.setLanguage(Language::DE);
  SETTINGS.keyboardLayouts = bit(0) | bit(4);
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::CyrillicRu);  // first enabled after DE
  SETTINGS.keyboardLayouts = bit(0) | bit(1);
  EXPECT_EQ(keyboard_layouts::startingLayout(), LayoutId::QwertyEn);  // wraps
}

TEST_F(KeyboardLayoutsTest, NextCyclesEnabledLayoutsAndWraps) {
  SETTINGS.keyboardLayouts = bit(0) | bit(3) | bit(8);
  EXPECT_EQ(keyboard_layouts::next(LayoutId::QwertyEn), LayoutId::SpanishEs);
  EXPECT_EQ(keyboard_layouts::next(LayoutId::SpanishEs), LayoutId::HebrewIl);
  EXPECT_EQ(keyboard_layouts::next(LayoutId::HebrewIl), LayoutId::QwertyEn);
  // A current layout that is switched off still moves to the next enabled one.
  EXPECT_EQ(keyboard_layouts::next(LayoutId::CyrillicRu), LayoutId::HebrewIl);
}

TEST_F(KeyboardLayoutsTest, NextFromUnknownLayoutStartsAtTableHead) {
  SETTINGS.keyboardLayouts = bit(0) | bit(2);
  EXPECT_EQ(keyboard_layouts::next(static_cast<LayoutId>(200)), LayoutId::QwertzDe);
}

// ---------------------------------------------------------------------------
// I18n
// ---------------------------------------------------------------------------

class I18nTest : public ::testing::Test {
 protected:
  void SetUp() override { I18N.setLanguage(Language::EN); }
  void TearDown() override { I18N.setLanguage(Language::EN); }
  static size_t strCount() { return static_cast<size_t>(StrId::_COUNT); }
};

TEST_F(I18nTest, OutOfRangeIdReturnsPlaceholder) {
  EXPECT_STREQ(I18N.get(StrId::_COUNT), "???");
  EXPECT_STREQ(I18N.get(static_cast<StrId>(0xFFFF)), "???");
  EXPECT_STREQ(I18N[static_cast<StrId>(strCount() + 7)], "???");
}

TEST_F(I18nTest, EnglishOffsetsNeverCarryTheFallbackBit) {
  for (size_t i = 0; i < strCount(); ++i) {
    ASSERT_EQ(i18n_strings::OFFSETS_EN[i] & 0x8000, 0) << "id " << i;
  }
}

TEST_F(I18nTest, Bit15OffsetResolvesIntoTheEnglishBlob) {
  I18N.setLanguage(Language::DE);
  const LangStrings de = getLanguageStrings(Language::DE);
  size_t fallbackId = strCount();
  for (size_t i = 0; i < strCount(); ++i) {
    if (de.offsets[i] & 0x8000) {
      fallbackId = i;
      break;
    }
  }
  ASSERT_LT(fallbackId, strCount()) << "German has no English-fallback string";
  const char* got = I18N.get(static_cast<StrId>(fallbackId));
  const char* expected = i18n_strings::STRINGS_EN_DATA + (de.offsets[fallbackId] & 0x7FFF);
  EXPECT_EQ(got, expected);  // pointer identity: it is the English string itself
  I18N.setLanguage(Language::EN);
  EXPECT_STREQ(got, I18N.get(static_cast<StrId>(fallbackId)));
}

TEST_F(I18nTest, PlainOffsetResolvesIntoTheLanguagesOwnBlob) {
  I18N.setLanguage(Language::DE);
  const LangStrings de = getLanguageStrings(Language::DE);
  size_t ownId = strCount();
  for (size_t i = 0; i < strCount(); ++i) {
    if ((de.offsets[i] & 0x8000) == 0) {
      ownId = i;
      break;
    }
  }
  ASSERT_LT(ownId, strCount());
  EXPECT_EQ(I18N.get(static_cast<StrId>(ownId)), de.data + de.offsets[ownId]);
}

TEST_F(I18nTest, EveryIdResolvesInEveryLanguage) {
  for (uint8_t l = 0; l < getLanguageCount(); ++l) {
    I18N.setLanguage(static_cast<Language>(l));
    ASSERT_EQ(I18N.getLanguage(), static_cast<Language>(l));
    for (size_t i = 0; i < strCount(); ++i) {
      const char* s = I18N.get(static_cast<StrId>(i));
      ASSERT_NE(s, nullptr) << "lang " << int(l) << " id " << i;
      ASSERT_LT(strlen(s), 4096u) << "lang " << int(l) << " id " << i;  // ASan catches an overrun
    }
  }
}

TEST_F(I18nTest, SetLanguageRejectsOutOfRange) {
  I18N.setLanguage(Language::FR);
  I18N.setLanguage(Language::_COUNT);
  EXPECT_EQ(I18N.getLanguage(), Language::FR);
  I18N.setLanguage(static_cast<Language>(255));
  EXPECT_EQ(I18N.getLanguage(), Language::FR);
}

TEST_F(I18nTest, LanguageNameAndCharacterSetBounds) {
  EXPECT_STREQ(I18N.getLanguageName(Language::_COUNT), "???");
  EXPECT_STREQ(I18N.getLanguageName(Language::EN), LANGUAGE_NAMES[0]);
  EXPECT_EQ(I18n::getCharacterSet(Language::_COUNT), I18n::getCharacterSet(Language::EN));
  EXPECT_EQ(I18n::getCharacterSet(Language::DE), CHARACTER_SETS[static_cast<size_t>(Language::DE)]);
}

TEST_F(I18nTest, LanguageFromCodeIsExactMatchWithEnglishFallback) {
  EXPECT_EQ(I18n::languageFromCode("DE"), Language::DE);
  EXPECT_EQ(I18n::languageFromCode("UK"), Language::UK);
  EXPECT_EQ(I18n::languageFromCode("EN"), Language::EN);
  EXPECT_EQ(I18n::languageFromCode("de"), Language::EN);  // case-sensitive
  EXPECT_EQ(I18n::languageFromCode(""), Language::EN);
  EXPECT_EQ(I18n::languageFromCode("ZZ"), Language::EN);
}

// ---------------------------------------------------------------------------
// CrossPointSettings derived getters
// ---------------------------------------------------------------------------

class SettingsGettersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    resetSettings();
    Storage.root = processStoreRoot();
    removeTree(Storage.root);
    ::mkdir(Storage.root.c_str(), 0755);
    Storage.resetCounters();
  }
};

TEST_F(SettingsGettersTest, SleepTimeoutScalesMinutesToMs) {
  SETTINGS.sleepTimeoutMinutes = 1;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 60000UL);
  SETTINGS.sleepTimeoutMinutes = 10;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 600000UL);
  SETTINGS.sleepTimeoutMinutes = 30;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 1800000UL);
}

TEST_F(SettingsGettersTest, SleepTimeoutNeverAndBeyondDisableAutoSleep) {
  SETTINGS.sleepTimeoutMinutes = CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 0UL);
  SETTINGS.sleepTimeoutMinutes = 200;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 0UL);
}

TEST_F(SettingsGettersTest, SleepTimeoutZeroClampsToOneMinute) {
  SETTINGS.sleepTimeoutMinutes = 0;
  EXPECT_EQ(SETTINGS.getSleepTimeoutMs(), 60000UL);
}

TEST_F(SettingsGettersTest, RefreshFrequencyTable) {
  const struct {
    uint8_t value;
    int pages;
  } rows[] = {{CrossPointSettings::REFRESH_1, 1},
              {CrossPointSettings::REFRESH_5, 5},
              {CrossPointSettings::REFRESH_10, 10},
              {CrossPointSettings::REFRESH_15, 15},
              {CrossPointSettings::REFRESH_30, 30},
              {CrossPointSettings::REFRESH_NEVER, INT_MAX},
              {99, 15}};
  for (const auto& row : rows) {
    SETTINGS.refreshFrequency = row.value;
    EXPECT_EQ(SETTINGS.getRefreshFrequency(), row.pages) << int(row.value);
  }
}

TEST_F(SettingsGettersTest, PowerButtonDurationIsShortOnlyForSleep) {
  EXPECT_EQ(SETTINGS.getPowerButtonDuration(), 400);
  SETTINGS.shortPwrBtn = CrossPointSettings::SLEEP;
  EXPECT_EQ(SETTINGS.getPowerButtonDuration(), 10);
  SETTINGS.shortPwrBtn = CrossPointSettings::PAGE_TURN;
  EXPECT_EQ(SETTINGS.getPowerButtonDuration(), 400);
}

TEST_F(SettingsGettersTest, LineCompressionPerFamilyAndSpacing) {
  const float serif[] = {0.95f, 1.0f, 1.1f, 1.2f};
  const float sans[] = {0.90f, 0.95f, 1.0f, 1.05f};
  for (uint8_t spacing = 0; spacing < CrossPointSettings::LINE_COMPRESSION_COUNT; ++spacing) {
    SETTINGS.lineSpacing = spacing;
    SETTINGS.fontFamily = CrossPointSettings::NOTOSERIF;
    EXPECT_FLOAT_EQ(SETTINGS.getReaderLineCompression(), serif[spacing]) << int(spacing);
    SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
    EXPECT_FLOAT_EQ(SETTINGS.getReaderLineCompression(), sans[spacing]) << int(spacing);
    // An SD family uses the neutral (serif) table whatever the built-in index says.
    strcpy(SETTINGS.sdFontFamilyName, "Bookerly");
    EXPECT_FLOAT_EQ(SETTINGS.getReaderLineCompression(), serif[spacing]) << int(spacing);
    SETTINGS.sdFontFamilyName[0] = '\0';
  }
  SETTINGS.lineSpacing = 99;
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  EXPECT_FLOAT_EQ(SETTINGS.getReaderLineCompression(), 0.95f);
}

TEST_F(SettingsGettersTest, BuiltinFontIdSnapsPointSizeToShippedSizes) {
  const struct {
    uint8_t pt;
    int serifId;
    int sansId;
  } rows[] = {{12, NOTOSERIF_12_FONT_ID, NOTOSANS_12_FONT_ID}, {13, NOTOSERIF_12_FONT_ID, NOTOSANS_12_FONT_ID},
              {14, NOTOSERIF_14_FONT_ID, NOTOSANS_14_FONT_ID}, {15, NOTOSERIF_14_FONT_ID, NOTOSANS_14_FONT_ID},
              {17, NOTOSERIF_16_FONT_ID, NOTOSANS_16_FONT_ID}, {18, NOTOSERIF_18_FONT_ID, NOTOSANS_18_FONT_ID},
              {40, NOTOSERIF_18_FONT_ID, NOTOSANS_18_FONT_ID}, {5, NOTOSERIF_12_FONT_ID, NOTOSANS_12_FONT_ID}};
  for (const auto& row : rows) {
    SETTINGS.fontPointSize = row.pt;
    SETTINGS.fontFamily = CrossPointSettings::NOTOSERIF;
    EXPECT_EQ(SETTINGS.getReaderFontId(), row.serifId) << int(row.pt);
    SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
    EXPECT_EQ(SETTINGS.getReaderFontId(), row.sansId) << int(row.pt);
  }
}

namespace {
struct ResolverLog {
  int calls = 0;
  std::string family;
  uint8_t size = 0;
  int result = 0;
};
int sdResolver(void* ctx, const char* family, const uint8_t size) {
  auto* log = static_cast<ResolverLog*>(ctx);
  log->calls++;
  log->family = family;
  log->size = size;
  return log->result;
}
}  // namespace

TEST_F(SettingsGettersTest, SdFontIdComesFromResolverWithBuiltinFallback) {
  ResolverLog log;
  log.result = 4242;
  SETTINGS.sdFontIdResolver = &sdResolver;
  SETTINGS.sdFontResolverCtx = &log;
  strcpy(SETTINGS.sdFontFamilyName, "Literata");
  SETTINGS.fontPointSize = 15;  // an SD-only size
  EXPECT_EQ(SETTINGS.getReaderFontId(), 4242);
  EXPECT_EQ(log.calls, 1);
  EXPECT_EQ(log.family, "Literata");
  EXPECT_EQ(log.size, 15);

  log.result = 0;  // family not installed: fall through, snapping 15 -> 14
  EXPECT_EQ(SETTINGS.getReaderFontId(), NOTOSERIF_14_FONT_ID);

  SETTINGS.sdFontIdResolver = nullptr;  // no resolver registered yet: never called
  log.calls = 0;
  EXPECT_EQ(SETTINGS.getReaderFontId(), NOTOSERIF_14_FONT_ID);
  EXPECT_EQ(log.calls, 0);
}

TEST_F(SettingsGettersTest, ReaderRenderSpecMirrorsSettingsAndViewport) {
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.fontPointSize = 16;
  SETTINGS.lineSpacing = CrossPointSettings::WIDE;
  SETTINGS.extraParagraphSpacing = 0;
  SETTINGS.paragraphAlignment = CrossPointSettings::BOOK_STYLE;
  SETTINGS.hyphenationEnabled = 1;
  SETTINGS.embeddedStyle = 0;
  SETTINGS.imageRendering = CrossPointSettings::IMAGES_PLACEHOLDER;
  SETTINGS.focusReadingEnabled = 1;
  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(444, 777);
  EXPECT_EQ(spec.fontId, NOTOSANS_16_FONT_ID);
  EXPECT_FLOAT_EQ(spec.lineCompression, 1.0f);
  EXPECT_FALSE(spec.extraParagraphSpacing);
  EXPECT_EQ(spec.paragraphAlignment, CrossPointSettings::BOOK_STYLE);
  EXPECT_EQ(spec.viewportWidth, 444);
  EXPECT_EQ(spec.viewportHeight, 777);
  EXPECT_TRUE(spec.hyphenationEnabled);
  EXPECT_FALSE(spec.embeddedStyle);
  EXPECT_EQ(spec.imageRendering, CrossPointSettings::IMAGES_PLACEHOLDER);
  EXPECT_TRUE(spec.focusReadingEnabled);
}

TEST_F(SettingsGettersTest, ClearSdFontFamilyClearsNameSnapsSizeAndSavesOnce) {
  strcpy(SETTINGS.sdFontFamilyName, "Literata");
  SETTINGS.fontPointSize = 17;
  SETTINGS.clearSdFontFamily();
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
  EXPECT_EQ(SETTINGS.fontPointSize, 16);
  EXPECT_EQ(Storage.writeCount, 1);
  ASSERT_EQ(Storage.writtenPaths.size(), 1u);
  EXPECT_EQ(Storage.writtenPaths[0], "/.crosspoint/settings.json");
}

TEST_F(SettingsGettersTest, ClearSdFontFamilySavesEvenWhenNothingChanges) {
  // No SD family and an already-shipped size: the save is unconditional, so the
  // caller can rely on the cleared state reaching disk.
  SETTINGS.fontPointSize = 14;
  SETTINGS.clearSdFontFamily();
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
  EXPECT_EQ(SETTINGS.fontPointSize, 14);
  EXPECT_EQ(Storage.writeCount, 1);
}

// ---------------------------------------------------------------------------
// MappedInputManager
// ---------------------------------------------------------------------------

class MappedInputTest : public ::testing::Test {
 protected:
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input{gpio, renderer};

  void SetUp() override {
    resetSettings();
    arduino_host::clock() = 10000;
    Frontlight.presentFlag = false;
    BoardConfig::x4ProFlag() = false;
  }

  void pressOnly(const uint8_t hw) {
    gpio.reset();
    gpio.pressedEdge[hw] = true;
    gpio.down[hw] = true;
  }

  // Which logical buttons report wasPressed() for a single hardware press.
  bool onlyPressed(const uint8_t hw, const std::initializer_list<Button> expected) {
    pressOnly(hw);
    static constexpr Button all[] = {Button::Back,        Button::Confirm,  Button::Left,        Button::Right,
                                     Button::Up,          Button::Down,     Button::Power,       Button::PageBack,
                                     Button::PageForward, Button::NavNext,  Button::NavPrevious, Button::ScreenLeft,
                                     Button::ScreenRight, Button::ScreenUp, Button::ScreenDown};
    for (const Button b : all) {
      bool want = false;
      for (const Button e : expected) want = want || e == b;
      if (input.wasPressed(b) != want) {
        ADD_FAILURE() << "hw " << int(hw) << " logical " << static_cast<int>(b) << " expected " << want;
        return false;
      }
    }
    return true;
  }

  void swipe(const float sx, const float sy, const float ex, const float ey) {
    gpio.swipePending = true;
    gpio.swipeStartNx = sx;
    gpio.swipeStartNy = sy;
    gpio.swipeEndNx = ex;
    gpio.swipeEndNy = ey;
  }
};

TEST_F(MappedInputTest, DefaultMappingIsIdentity) {
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_BACK, {Button::Back}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_CONFIRM, {Button::Confirm}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Left, Button::NavPrevious, Button::ScreenLeft}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_RIGHT, {Button::Right, Button::NavNext, Button::ScreenRight}));
}

TEST_F(MappedInputTest, FrontButtonsFollowTheRemapTable) {
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_RIGHT, {Button::Back}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Confirm}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_BACK, {Button::Left, Button::NavPrevious, Button::ScreenLeft}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_CONFIRM, {Button::Right, Button::NavNext, Button::ScreenRight}));
}

TEST_F(MappedInputTest, SideAndPowerButtonsAreNeverRemapped) {
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_BACK;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_UP, {Button::Up, Button::PageBack, Button::NavPrevious, Button::ScreenUp}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_DOWN, {Button::Down, Button::PageForward, Button::NavNext, Button::ScreenDown}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_POWER, {Button::Power}));
}

TEST_F(MappedInputTest, PageButtonsFollowSideButtonLayout) {
  SETTINGS.sideButtonLayout = CrossPointSettings::NEXT_PREV;
  pressOnly(HalGPIO::BTN_UP);
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
  EXPECT_FALSE(input.wasPressed(Button::PageBack));
  pressOnly(HalGPIO::BTN_DOWN);
  EXPECT_TRUE(input.wasPressed(Button::PageBack));
  EXPECT_FALSE(input.wasPressed(Button::PageForward));

  SETTINGS.sideButtonLayout = CrossPointSettings::SIDE_BUTTONS_DISABLED;
  EXPECT_FALSE(input.wasPressed(Button::PageBack));
  EXPECT_FALSE(input.wasPressed(Button::PageForward));
  EXPECT_TRUE(input.wasPressed(Button::Down));  // plain Down still works
}

TEST_F(MappedInputTest, AxisSwapNeedsTouchOrFollowOrientation) {
  renderer.orientation = GfxRenderer::PortraitInverted;
  EXPECT_FALSE(input.isNavDirectionSwapped());
  SETTINGS.frontButtonFollowOrientation = 1;
  EXPECT_TRUE(input.isNavDirectionSwapped());
  SETTINGS.frontButtonFollowOrientation = 0;
  gpio.touch = true;
  EXPECT_TRUE(input.isNavDirectionSwapped());
}

TEST_F(MappedInputTest, AxisSwapOnlyInInvertedAndLandscapeCcw) {
  SETTINGS.frontButtonFollowOrientation = 1;
  renderer.orientation = GfxRenderer::Portrait;
  EXPECT_FALSE(input.isNavDirectionSwapped());
  renderer.orientation = GfxRenderer::LandscapeClockwise;
  EXPECT_FALSE(input.isNavDirectionSwapped());
  renderer.orientation = GfxRenderer::PortraitInverted;
  EXPECT_TRUE(input.isNavDirectionSwapped());
  renderer.orientation = GfxRenderer::LandscapeCounterClockwise;
  EXPECT_TRUE(input.isNavDirectionSwapped());
}

TEST_F(MappedInputTest, AxisSwapFlipsPageAndNavButtons) {
  gpio.touch = true;
  renderer.orientation = GfxRenderer::LandscapeCounterClockwise;
  pressOnly(HalGPIO::BTN_UP);
  gpio.touch = true;
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
  EXPECT_TRUE(input.wasPressed(Button::NavNext));
  EXPECT_FALSE(input.wasPressed(Button::PageBack));
  EXPECT_FALSE(input.wasPressed(Button::NavPrevious));
  pressOnly(HalGPIO::BTN_LEFT);
  gpio.touch = true;
  EXPECT_TRUE(input.wasPressed(Button::NavNext));
  EXPECT_FALSE(input.wasPressed(Button::NavPrevious));

  SETTINGS.sideButtonLayout = CrossPointSettings::NEXT_PREV;  // both flips compose
  pressOnly(HalGPIO::BTN_UP);
  gpio.touch = true;
  EXPECT_TRUE(input.wasPressed(Button::PageBack));
}

TEST_F(MappedInputTest, ScreenDirectionsStayPhysicalWithoutFollowOrientation) {
  renderer.orientation = GfxRenderer::LandscapeClockwise;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Left, Button::NavPrevious, Button::ScreenLeft}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_UP, {Button::Up, Button::PageBack, Button::NavPrevious, Button::ScreenUp}));
}

TEST_F(MappedInputTest, ScreenDirectionsRotateWithLiveOrientation) {
  SETTINGS.frontButtonFollowOrientation = 1;
  // Landscape CW: physical Left = screen Up, Right = screen Down, Up = screen Right, Down = screen Left.
  renderer.orientation = GfxRenderer::LandscapeClockwise;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Left, Button::NavPrevious, Button::ScreenUp}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_RIGHT, {Button::Right, Button::NavNext, Button::ScreenDown}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_UP, {Button::Up, Button::PageBack, Button::NavPrevious, Button::ScreenRight}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_DOWN, {Button::Down, Button::PageForward, Button::NavNext, Button::ScreenLeft}));
  // Inverted: everything mirrored; the axis swap also flips page/nav.
  renderer.orientation = GfxRenderer::PortraitInverted;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Left, Button::NavNext, Button::ScreenRight}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_UP, {Button::Up, Button::PageForward, Button::NavNext, Button::ScreenDown}));
  // Landscape CCW: physical Left = screen Down, Up = screen Left.
  renderer.orientation = GfxRenderer::LandscapeCounterClockwise;
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_LEFT, {Button::Left, Button::NavNext, Button::ScreenDown}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_RIGHT, {Button::Right, Button::NavPrevious, Button::ScreenUp}));
  EXPECT_TRUE(onlyPressed(HalGPIO::BTN_UP, {Button::Up, Button::PageForward, Button::NavNext, Button::ScreenLeft}));
}

TEST_F(MappedInputTest, IsPressedAndWasReleasedShareTheMapping) {
  // Swap Confirm and Left so the physical Left button drives exactly one of them.
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_CONFIRM;
  gpio.down[HalGPIO::BTN_LEFT] = true;
  EXPECT_TRUE(input.isPressed(Button::Confirm));
  EXPECT_FALSE(input.isPressed(Button::Left));
  gpio.releasedEdge[HalGPIO::BTN_LEFT] = true;
  EXPECT_TRUE(input.wasReleased(Button::Confirm));
  EXPECT_FALSE(input.wasReleased(Button::Back));
  EXPECT_TRUE(input.wasAnyReleased());
  EXPECT_FALSE(input.wasAnyPressed());
}

TEST_F(MappedInputTest, LongPressFiresOnceAtThreshold) {
  gpio.down[HalGPIO::BTN_CONFIRM] = true;
  gpio.heldMs = 399;
  EXPECT_FALSE(input.wasLongPressed(Button::Confirm, 400));
  gpio.heldMs = 400;
  EXPECT_TRUE(input.wasLongPressed(Button::Confirm, 400));
  gpio.heldMs = 2000;
  EXPECT_FALSE(input.wasLongPressed(Button::Confirm, 400));  // one-shot per hold
  input.update();
  EXPECT_FALSE(input.wasLongPressed(Button::Confirm, 400));  // still held: stays latched
}

TEST_F(MappedInputTest, LongPressRequiresTheButtonToBeDown) {
  gpio.heldMs = 5000;
  EXPECT_FALSE(input.wasLongPressed(Button::Confirm, 400));
  gpio.down[HalGPIO::BTN_BACK] = true;
  EXPECT_FALSE(input.wasLongPressed(Button::Confirm, 400));
  EXPECT_TRUE(input.wasLongPressed(Button::Back, 400));
}

TEST_F(MappedInputTest, LongPressRearmsAfterUpdateSeesRelease) {
  gpio.down[HalGPIO::BTN_UP] = true;
  gpio.heldMs = 700;
  EXPECT_TRUE(input.wasLongPressed(Button::PageBack, 700));
  gpio.down[HalGPIO::BTN_UP] = false;
  input.update();
  gpio.down[HalGPIO::BTN_UP] = true;
  EXPECT_TRUE(input.wasLongPressed(Button::PageBack, 700));
}

TEST_F(MappedInputTest, LongPressSuppressesTheFollowingRelease) {
  gpio.down[HalGPIO::BTN_CONFIRM] = true;
  gpio.heldMs = 400;
  ASSERT_TRUE(input.wasLongPressed(Button::Confirm, 400));
  EXPECT_FALSE(input.consumeSuppressedRelease());  // nothing released yet
  gpio.down[HalGPIO::BTN_CONFIRM] = false;
  gpio.releasedEdge[HalGPIO::BTN_CONFIRM] = true;
  EXPECT_TRUE(input.consumeSuppressedRelease());
  EXPECT_FALSE(input.consumeSuppressedRelease());  // consumed exactly once
}

TEST_F(MappedInputTest, ConsumeSuppressedReleaseIgnoresOtherButtons) {
  gpio.down[HalGPIO::BTN_CONFIRM] = true;
  gpio.heldMs = 400;
  ASSERT_TRUE(input.wasLongPressed(Button::Confirm, 400));
  gpio.releasedEdge[HalGPIO::BTN_BACK] = true;
  EXPECT_FALSE(input.consumeSuppressedRelease());
  gpio.releasedEdge[HalGPIO::BTN_BACK] = false;
  gpio.releasedEdge[HalGPIO::BTN_CONFIRM] = true;
  EXPECT_TRUE(input.consumeSuppressedRelease());
}

TEST_F(MappedInputTest, HintLabelsFollowTheRemap) {
  auto l = input.mapLabels("B", "C", "P", "N");
  EXPECT_STREQ(l.btn1, "B");
  EXPECT_STREQ(l.btn2, "C");
  EXPECT_STREQ(l.btn3, "P");
  EXPECT_STREQ(l.btn4, "N");

  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
  l = input.mapLabels("B", "C", "P", "N");
  EXPECT_STREQ(l.btn1, "C");
  EXPECT_STREQ(l.btn2, "B");
  EXPECT_STREQ(l.btn3, "N");
  EXPECT_STREQ(l.btn4, "P");
}

TEST_F(MappedInputTest, HintLabelsSwapPrevNextWithTheAxis) {
  SETTINGS.frontButtonFollowOrientation = 1;
  renderer.orientation = GfxRenderer::PortraitInverted;
  const auto l = input.mapLabels("B", "C", "P", "N");
  EXPECT_STREQ(l.btn3, "N");
  EXPECT_STREQ(l.btn4, "P");
}

TEST_F(MappedInputTest, DirectionalLabelsRotateWithOrientation) {
  auto l = input.mapDirectionalLabels("B", "C", "L", "R", "U", "D");
  EXPECT_STREQ(l.btn3, "L");
  EXPECT_STREQ(l.btn4, "R");
  SETTINGS.frontButtonFollowOrientation = 1;
  renderer.orientation = GfxRenderer::LandscapeClockwise;  // physical Left = screen Up
  l = input.mapDirectionalLabels("B", "C", "L", "R", "U", "D");
  EXPECT_STREQ(l.btn3, "U");
  EXPECT_STREQ(l.btn4, "D");
  renderer.orientation = GfxRenderer::PortraitInverted;
  l = input.mapDirectionalLabels("B", "C", "L", "R", "U", "D");
  EXPECT_STREQ(l.btn3, "R");
  EXPECT_STREQ(l.btn4, "L");
}

TEST_F(MappedInputTest, HintLabelsWithDuplicateRolesLeaveUnmatchedSlotsEmpty) {
  // validateFrontButtonMapping() normally prevents this; the label builder
  // must still not read past the table.
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_BACK;
  const auto l = input.mapLabels("B", "C", "P", "N");
  EXPECT_STREQ(l.btn1, "B");
  EXPECT_STREQ(l.btn2, "");
  EXPECT_STREQ(l.btn3, "");
  EXPECT_STREQ(l.btn4, "");
}

TEST_F(MappedInputTest, PressedFrontButtonScansRawHardwareOrder) {
  EXPECT_EQ(input.getPressedFrontButton(), -1);
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_RIGHT;  // remap must not matter
  gpio.pressedEdge[HalGPIO::BTN_RIGHT] = true;
  EXPECT_EQ(input.getPressedFrontButton(), HalGPIO::BTN_RIGHT);
  gpio.pressedEdge[HalGPIO::BTN_CONFIRM] = true;
  EXPECT_EQ(input.getPressedFrontButton(), HalGPIO::BTN_CONFIRM);
  gpio.pressedEdge[HalGPIO::BTN_BACK] = true;
  EXPECT_EQ(input.getPressedFrontButton(), HalGPIO::BTN_BACK);
  gpio.reset();
  gpio.pressedEdge[HalGPIO::BTN_UP] = true;  // side buttons are not front buttons
  EXPECT_EQ(input.getPressedFrontButton(), -1);
}

TEST_F(MappedInputTest, LeftEdgeRightSwipeIsBack) {
  gpio.touch = true;
  swipe(0.05f, 0.5f, 0.6f, 0.5f);
  EXPECT_TRUE(input.wasBackGesture());
  EXPECT_TRUE(input.wasPressed(Button::Back));
  EXPECT_TRUE(input.wasReleased(Button::Back));
  EXPECT_FALSE(input.wasPressed(Button::Confirm));
  swipe(0.5f, 0.5f, 0.9f, 0.5f);  // mid-screen: a plain swipe, not Back
  EXPECT_FALSE(input.wasBackGesture());
  EXPECT_FALSE(input.wasPressed(Button::Back));
  EXPECT_EQ(input.wasSwipe(), MappedInputManager::SwipeDir::Right);
  swipe(0.05f, 0.2f, 0.2f, 0.9f);  // edge-anchored but vertical-dominant
  EXPECT_FALSE(input.wasBackGesture());
}

TEST_F(MappedInputTest, SwipeDirectionClassification) {
  swipe(0.5f, 0.5f, 0.2f, 0.55f);
  EXPECT_EQ(input.wasSwipe(), MappedInputManager::SwipeDir::Left);
  swipe(0.5f, 0.5f, 0.55f, 0.2f);
  EXPECT_EQ(input.wasSwipe(), MappedInputManager::SwipeDir::Up);
  swipe(0.5f, 0.5f, 0.55f, 0.9f);
  EXPECT_EQ(input.wasSwipe(), MappedInputManager::SwipeDir::Down);
  gpio.swipePending = false;
  EXPECT_EQ(input.wasSwipe(), MappedInputManager::SwipeDir::None);
}

TEST_F(MappedInputTest, HomeGestureIsBottomEdgeUpSwipeOrHomeKeyTap) {
  gpio.touch = true;
  swipe(0.5f, 0.95f, 0.5f, 0.4f);
  EXPECT_TRUE(input.wasHomeGesture());
  EXPECT_FALSE(input.wasReaderMenuSwipeUp());  // no home key: bottom edge is Home
  EXPECT_FALSE(input.wasHomeKeyHold());

  gpio.homeKey = true;
  EXPECT_FALSE(input.wasHomeGesture());  // swipe no longer means Home
  EXPECT_TRUE(input.wasReaderMenuSwipeUp());
  gpio.homeKeyTapped = true;
  EXPECT_TRUE(input.wasHomeGesture());
  gpio.homeKeyLong = true;
  EXPECT_TRUE(input.wasHomeKeyHold());
}

TEST_F(MappedInputTest, TopEdgeDownSwipeIsMenuOrLightPanel) {
  gpio.touch = true;
  swipe(0.5f, 0.05f, 0.5f, 0.6f);
  EXPECT_TRUE(input.wasMenuGesture());
  EXPECT_FALSE(input.wasLightPanelGesture());
  Frontlight.presentFlag = true;
  EXPECT_TRUE(input.wasLightPanelGesture());
  swipe(0.5f, 0.5f, 0.5f, 0.9f);  // not edge-anchored
  EXPECT_FALSE(input.wasMenuGesture());
  EXPECT_FALSE(input.wasLightPanelGesture());
}

TEST_F(MappedInputTest, TapMapsToLogicalCoordinatesAndRectHitTest) {
  gpio.tapPending = true;
  gpio.tapNx = 0.5f;
  gpio.tapNy = 0.25f;
  int x = -1, y = -1;
  EXPECT_TRUE(input.wasScreenTapped(x, y));
  EXPECT_EQ(x, 240);
  EXPECT_EQ(y, 200);
  EXPECT_TRUE(input.wasTapInRect(200, 150, 100, 100));
  EXPECT_FALSE(input.wasTapInRect(241, 150, 100, 100));
  EXPECT_FALSE(input.wasTapInRect(200, 150, 100, 50));  // y == 200 is one past the bottom
  gpio.tapPending = false;
  EXPECT_FALSE(input.wasScreenTapped(x, y));
}

TEST_F(MappedInputTest, TouchDownSelectionWaitsForTheHoldDelay) {
  gpio.tapCandidate = true;
  gpio.tapNx = 0.1f;
  gpio.tapNy = 0.1f;
  gpio.tapCandidateHeldMs = 89;
  int x, y;
  EXPECT_FALSE(input.wasScreenTouchDown(x, y));
  gpio.tapCandidateHeldMs = 90;
  EXPECT_TRUE(input.wasScreenTouchDown(x, y));
  EXPECT_EQ(x, 48);
  EXPECT_EQ(y, 80);
}

TEST_F(MappedInputTest, RowTouchHitTestsBandGeometry) {
  gpio.tapPending = true;
  gpio.tapNx = 0.5f;
  gpio.tapNy = 0.25f;  // y = 200
  int row = -1;
  EXPECT_EQ(input.rowTouch(row, 100, 40, 5), MappedInputManager::RowTouch::Tap);
  EXPECT_EQ(row, 2);
  EXPECT_EQ(input.rowTouch(row, 100, 40, 2), MappedInputManager::RowTouch::None);          // past rowCount
  EXPECT_EQ(input.rowTouch(row, 250, 40, 5), MappedInputManager::RowTouch::None);          // above the band
  EXPECT_EQ(input.rowTouch(row, 100, 40, 5, 0, 200), MappedInputManager::RowTouch::None);  // x >= xEnd
  // y - top = 100 lands 20 px into its 40 px step: a 20 px row band rejects it, a 21 px one hits.
  EXPECT_EQ(input.rowTouch(row, 100, 40, 5, 0, INT32_MAX, 20), MappedInputManager::RowTouch::None);
  EXPECT_EQ(input.rowTouch(row, 100, 40, 5, 0, INT32_MAX, 21), MappedInputManager::RowTouch::Tap);
  EXPECT_EQ(input.rowTouch(row, 0, 40, 0), MappedInputManager::RowTouch::None);
  gpio.tapPending = false;
  gpio.tapCandidate = true;
  gpio.tapCandidateHeldMs = 500;
  EXPECT_EQ(input.rowTouch(row, 100, 40, 5), MappedInputManager::RowTouch::Down);
  EXPECT_EQ(row, 2);
}

TEST_F(MappedInputTest, ScreenLongPressSuppressesTheRestOfTheContact) {
  gpio.touchLongPress = true;
  gpio.tapNx = 0.5f;
  gpio.tapNy = 0.5f;
  int x, y;
  EXPECT_TRUE(input.wasScreenLongPress(x, y));
  EXPECT_EQ(gpio.suppressCount, 1);
  EXPECT_EQ(x, 240);
  EXPECT_EQ(y, 400);
}

TEST_F(MappedInputTest, HeldTimeUsesTheTouchOverrideOnlyInsideItsWindow) {
  gpio.heldMs = 5;
  gpio.touchHeldMs = 1200;
  gpio.tapPending = true;
  int x, y;
  ASSERT_TRUE(input.wasScreenTapped(x, y));
  gpio.tapPending = false;
  EXPECT_EQ(input.getHeldTime(), 1200UL);
  arduino_host::clock() += 250;
  EXPECT_EQ(input.getHeldTime(), 1200UL);
  arduino_host::clock() += 1;
  EXPECT_EQ(input.getHeldTime(), 5UL);
  EXPECT_EQ(input.getHeldTime(), 5UL);  // override is dropped, not re-armed
}

TEST_F(MappedInputTest, HeldTimeOverrideYieldsToButtonEdges) {
  gpio.heldMs = 5;
  gpio.touchHeldMs = 1200;
  gpio.tapPending = true;
  int x, y;
  ASSERT_TRUE(input.wasScreenTapped(x, y));
  gpio.pressedEdge[HalGPIO::BTN_BACK] = true;
  EXPECT_EQ(input.getHeldTime(), 5UL);
}

TEST_F(MappedInputTest, ColTouchHitTestsBandGeometry) {
  gpio.tapPending = true;
  gpio.tapNx = 0.5f;   // x = 240
  gpio.tapNy = 0.25f;  // y = 200
  int col = -1;
  EXPECT_EQ(input.colTouch(col, 40, 100, 5, 0, INT32_MAX), MappedInputManager::RowTouch::Tap);
  EXPECT_EQ(col, 2);
  EXPECT_EQ(input.colTouch(col, 40, 100, 2, 0, INT32_MAX), MappedInputManager::RowTouch::None);    // past colCount
  EXPECT_EQ(input.colTouch(col, 300, 100, 5, 0, INT32_MAX), MappedInputManager::RowTouch::None);   // left of the band
  EXPECT_EQ(input.colTouch(col, 40, 100, 5, 0, 200), MappedInputManager::RowTouch::None);          // y >= yEnd
  EXPECT_EQ(input.colTouch(col, 40, 100, 5, 201, INT32_MAX), MappedInputManager::RowTouch::None);  // y < yStart
  // x - left = 210 lands 10 px into its 100 px step: a 10 px column rejects it, an 11 px one hits.
  EXPECT_EQ(input.colTouch(col, 30, 100, 5, 0, INT32_MAX, 10), MappedInputManager::RowTouch::None);
  EXPECT_EQ(input.colTouch(col, 30, 100, 5, 0, INT32_MAX, 11), MappedInputManager::RowTouch::Tap);
  EXPECT_EQ(input.colTouch(col, 0, 100, 0, 0, INT32_MAX), MappedInputManager::RowTouch::None);  // no columns
  EXPECT_EQ(input.colTouch(col, 0, 0, 5, 0, INT32_MAX), MappedInputManager::RowTouch::None);    // zero step
  gpio.tapPending = false;
  gpio.tapCandidate = true;
  gpio.tapCandidateHeldMs = 500;
  EXPECT_EQ(input.colTouch(col, 40, 100, 5, 0, INT32_MAX), MappedInputManager::RowTouch::Down);
  EXPECT_EQ(col, 2);
}

TEST_F(MappedInputTest, HeldContactTracksLivePositionAndReportsRelease) {
  int x = -1, y = -1;
  EXPECT_FALSE(input.isScreenTouchHeld(x, y));
  EXPECT_FALSE(input.wasScreenTouchReleased());
  gpio.touchHeld = true;
  gpio.tapNx = 0.25f;
  gpio.tapNy = 0.5f;
  EXPECT_TRUE(input.isScreenTouchHeld(x, y));
  EXPECT_EQ(x, 120);
  EXPECT_EQ(y, 400);
  gpio.touchHeld = false;
  gpio.touchReleasedEdge = true;
  EXPECT_FALSE(input.isScreenTouchHeld(x, y));
  EXPECT_TRUE(input.wasScreenTouchReleased());
}

// Power-as-Confirm only exists on the touch-capable build (FREEINK_CAP_TOUCH).
TEST_F(MappedInputTest, PowerClickIsConfirmOnlyForTheConfirmRoleOnATouchBoard) {
  gpio.releasedEdge[HalGPIO::BTN_POWER] = true;
  gpio.powerHeldMs = 100;
  EXPECT_FALSE(input.wasPressed(Button::Confirm));  // role is still Ignore
  SETTINGS.shortPwrBtn = CrossPointSettings::PWR_CONFIRM;
  EXPECT_FALSE(input.wasPressed(Button::Confirm));  // no touch panel on this board
  gpio.touch = true;
  EXPECT_TRUE(input.wasPressed(Button::Confirm));
  EXPECT_TRUE(input.wasReleased(Button::Confirm));
  EXPECT_FALSE(input.wasPressed(Button::Back));  // Power never stands in for Back
}

TEST_F(MappedInputTest, PowerConfirmClickNeedsAShortReleaseEdge) {
  SETTINGS.shortPwrBtn = CrossPointSettings::PWR_CONFIRM;
  gpio.touch = true;
  gpio.releasedEdge[HalGPIO::BTN_POWER] = true;
  gpio.powerHeldMs = SETTINGS.getPowerButtonDuration();
  EXPECT_TRUE(input.wasPressed(Button::Confirm));
  gpio.powerHeldMs = SETTINGS.getPowerButtonDuration() + 1;
  EXPECT_FALSE(input.wasPressed(Button::Confirm));  // a hold, not a click
  gpio.powerHeldMs = 100;
  gpio.releasedEdge[HalGPIO::BTN_POWER] = false;
  EXPECT_FALSE(input.wasPressed(Button::Confirm));  // still down: nothing yet
}

TEST_F(MappedInputTest, X4ProTakesThePowerClickFromTheDelayedFrame) {
  BoardConfig::x4ProFlag() = true;
  SETTINGS.shortPwrBtn = CrossPointSettings::PWR_CONFIRM;
  gpio.touch = true;
  gpio.releasedEdge[HalGPIO::BTN_POWER] = true;
  gpio.powerHeldMs = 100;
  EXPECT_FALSE(input.wasPressed(Button::Confirm));  // release edge alone is too early
  input.setPowerConfirmClickFrame(true);
  EXPECT_TRUE(input.wasPressed(Button::Confirm));
  EXPECT_TRUE(input.wasReleased(Button::Confirm));
  input.setPowerConfirmClickFrame(false);
  EXPECT_FALSE(input.wasPressed(Button::Confirm));
}

// ---------------------------------------------------------------------------
// HalTiltSensor
// ---------------------------------------------------------------------------

class TiltSensorTest : public ::testing::Test {
 protected:
  HalTiltSensor sensor;
  Imu::HostState& imu = Imu::host();

  void SetUp() override {
    imu = Imu::HostState();
    imu.beginOk = true;
    arduino_host::clock() = 10000;
  }

  // begin() + the first enabled poll (which only wakes the IMU), then step
  // past both the 300 ms settle and the 600 ms post-wake cooldown.
  void beginAwakeAndSettled() {
    sensor.begin();
    sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
    arduino_host::clock() += 600;
  }

  void poll(const float gx, const float gy, const uint8_t mode = CrossPointTiltPageTurn::TILT_NORMAL,
            const uint8_t orientation = CrossPointOrientation::PORTRAIT) {
    imu.sample.gx = gx;
    imu.sample.gy = gy;
    sensor.update(mode, orientation, true);
  }
};

TEST_F(TiltSensorTest, MissingImuMakesEverythingInert) {
  imu.beginOk = false;
  sensor.begin();
  EXPECT_FALSE(sensor.isAvailable());
  EXPECT_FALSE(sensor.wake());
  EXPECT_FALSE(sensor.deepSleep());
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.wakeCalls, 0);
  EXPECT_EQ(imu.readCalls, 0);
  EXPECT_FALSE(sensor.wasTiltedForward());
  EXPECT_FALSE(sensor.hadActivity());
}

TEST_F(TiltSensorTest, BeginStandsTheImuByUntilTiltIsEnabled) {
  sensor.begin();
  EXPECT_TRUE(sensor.isAvailable());
  EXPECT_EQ(imu.sleepCalls, 1);
  EXPECT_EQ(imu.wakeCalls, 0);
  sensor.update(CrossPointTiltPageTurn::TILT_OFF, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.wakeCalls, 0);
  EXPECT_EQ(imu.readCalls, 0);
}

TEST_F(TiltSensorTest, FirstEnabledPollWakesWithoutReading) {
  sensor.begin();
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, false);
  EXPECT_EQ(imu.wakeCalls, 1);
  EXPECT_EQ(imu.readCalls, 0);
}

TEST_F(TiltSensorTest, DisablingWhileAwakePutsTheImuToSleep) {
  beginAwakeAndSettled();
  sensor.update(CrossPointTiltPageTurn::TILT_OFF, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.sleepCalls, 2);
  arduino_host::clock() += 100;
  sensor.update(CrossPointTiltPageTurn::TILT_OFF, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.readCalls, 0);
}

TEST_F(TiltSensorTest, WakeFailureIsRetriedOnTheNextPoll) {
  sensor.begin();
  imu.wakeOk = false;
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.wakeCalls, 2);
  imu.wakeOk = true;
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.wakeCalls, 3);
  arduino_host::clock() += 300;
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  EXPECT_EQ(imu.wakeCalls, 3);
  EXPECT_EQ(imu.readCalls, 1);
}

TEST_F(TiltSensorTest, ReadingsAreDiscardedDuringTheWakeSettle) {
  sensor.begin();
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);
  arduino_host::clock() += 299;
  poll(300.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 0);
  arduino_host::clock() += 1;
  poll(300.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 1);
}

TEST_F(TiltSensorTest, PollsAtTwentyHertzOnlyInsideTheReader) {
  beginAwakeAndSettled();
  poll(0.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 1);
  arduino_host::clock() += 49;
  poll(0.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 1);
  arduino_host::clock() += 1;
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, false);
  EXPECT_EQ(imu.readCalls, 1);  // outside the reader: no I2C traffic
  poll(0.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 2);
}

TEST_F(TiltSensorTest, ForwardAndBackTriggerStrictlyBeyondTheRateThreshold) {
  beginAwakeAndSettled();
  poll(270.0f, 0.0f);
  EXPECT_FALSE(sensor.wasTiltedForward());
  EXPECT_FALSE(sensor.hadActivity());
  arduino_host::clock() += 50;
  poll(270.5f, 0.0f);
  EXPECT_TRUE(sensor.wasTiltedForward());
  EXPECT_FALSE(sensor.wasTiltedForward());  // consumed
  EXPECT_FALSE(sensor.wasTiltedBack());
  EXPECT_TRUE(sensor.hadActivity());
  EXPECT_FALSE(sensor.hadActivity());  // consumed too
}

TEST_F(TiltSensorTest, BackwardTrigger) {
  beginAwakeAndSettled();
  poll(-271.0f, 0.0f);
  EXPECT_TRUE(sensor.wasTiltedBack());
  EXPECT_FALSE(sensor.wasTiltedForward());
}

TEST_F(TiltSensorTest, RetriggerNeedsReturnToNeutralAndCooldown) {
  beginAwakeAndSettled();
  poll(300.0f, 0.0f);
  ASSERT_TRUE(sensor.wasTiltedForward());
  arduino_host::clock() += 600;
  poll(300.0f, 0.0f);  // cooldown over, but still tilted: no re-arm
  EXPECT_FALSE(sensor.wasTiltedForward());
  arduino_host::clock() += 50;
  poll(50.0f, 0.0f);  // 50 dps is not below the neutral rate
  arduino_host::clock() += 50;
  poll(300.0f, 0.0f);
  EXPECT_FALSE(sensor.wasTiltedForward());
  arduino_host::clock() += 50;
  poll(49.0f, 0.0f);  // re-armed
  arduino_host::clock() += 50;
  poll(300.0f, 0.0f);
  EXPECT_TRUE(sensor.wasTiltedForward());
}

TEST_F(TiltSensorTest, CooldownBlocksAnImmediateSecondGesture) {
  beginAwakeAndSettled();
  poll(300.0f, 0.0f);
  ASSERT_TRUE(sensor.wasTiltedForward());
  arduino_host::clock() += 50;
  poll(0.0f, 0.0f);  // neutral: re-armed, cooldown still running
  arduino_host::clock() += 50;
  poll(-300.0f, 0.0f);
  EXPECT_FALSE(sensor.wasTiltedBack());
  arduino_host::clock() += 500;  // 600 ms since the trigger
  poll(-300.0f, 0.0f);
  EXPECT_TRUE(sensor.wasTiltedBack());
}

TEST_F(TiltSensorTest, OrientationAndInversionSelectTheAxisAndSign) {
  struct Row {
    uint8_t orientation;
    uint8_t mode;
    float gx, gy;
    bool forward;
  };
  const Row rows[] = {
      {CrossPointOrientation::PORTRAIT, CrossPointTiltPageTurn::TILT_NORMAL, 300, 0, true},
      {CrossPointOrientation::PORTRAIT, CrossPointTiltPageTurn::TILT_INVERTED, 300, 0, false},
      {CrossPointOrientation::INVERTED, CrossPointTiltPageTurn::TILT_NORMAL, 300, 0, false},
      {CrossPointOrientation::INVERTED, CrossPointTiltPageTurn::TILT_INVERTED, 300, 0, true},
      {CrossPointOrientation::LANDSCAPE_CW, CrossPointTiltPageTurn::TILT_NORMAL, 0, 300, false},
      {CrossPointOrientation::LANDSCAPE_CW, CrossPointTiltPageTurn::TILT_INVERTED, 0, 300, true},
      {CrossPointOrientation::LANDSCAPE_CCW, CrossPointTiltPageTurn::TILT_NORMAL, 0, 300, true},
      {CrossPointOrientation::LANDSCAPE_CCW, CrossPointTiltPageTurn::TILT_INVERTED, 0, 300, false},
      {CrossPointOrientation::LANDSCAPE_CW, CrossPointTiltPageTurn::TILT_NORMAL, 300, 0, false},  // wrong axis
  };
  for (const Row& row : rows) {
    HalTiltSensor fresh;
    imu = Imu::HostState();
    imu.beginOk = true;
    fresh.begin();
    fresh.update(row.mode, row.orientation, true);
    arduino_host::clock() += 600;
    imu.sample.gx = row.gx;
    imu.sample.gy = row.gy;
    fresh.update(row.mode, row.orientation, true);
    const bool fwd = fresh.wasTiltedForward();
    const bool back = fresh.wasTiltedBack();
    const bool wrongAxis = row.orientation == CrossPointOrientation::LANDSCAPE_CW && row.gx != 0;
    if (wrongAxis) {
      EXPECT_FALSE(fwd || back);
      continue;
    }
    EXPECT_EQ(fwd, row.forward) << "orientation " << int(row.orientation) << " mode " << int(row.mode);
    EXPECT_EQ(back, !row.forward) << "orientation " << int(row.orientation) << " mode " << int(row.mode);
  }
}

TEST_F(TiltSensorTest, ReadFailureProducesNoEvent) {
  beginAwakeAndSettled();
  imu.readOk = false;
  poll(300.0f, 0.0f);
  EXPECT_EQ(imu.readCalls, 1);
  EXPECT_FALSE(sensor.wasTiltedForward());
  EXPECT_FALSE(sensor.hadActivity());
}

TEST_F(TiltSensorTest, ClearPendingEventsDropsEventsButKeepsTheTiltLatch) {
  beginAwakeAndSettled();
  poll(300.0f, 0.0f);
  sensor.clearPendingEvents();
  EXPECT_FALSE(sensor.wasTiltedForward());
  EXPECT_FALSE(sensor.hadActivity());
  arduino_host::clock() += 600;
  poll(300.0f, 0.0f);  // still tilted: the latch survives the clear
  EXPECT_FALSE(sensor.wasTiltedForward());
}

TEST_F(TiltSensorTest, DeepSleepClearsEventsAndLatch) {
  beginAwakeAndSettled();
  poll(300.0f, 0.0f);
  EXPECT_TRUE(sensor.deepSleep());
  EXPECT_FALSE(sensor.wasTiltedForward());
  imu.sleepOk = false;
  sensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true);  // wakes again
  EXPECT_FALSE(sensor.deepSleep());  // I2C failure reported, state kept awake
}

}  // namespace
