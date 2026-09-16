// Host tests for the PersistableStore JSON persistence family:
// CrossPointSettings, CrossPointState, RecentBooksStore, OpdsServerStore,
// WifiCredentialStore and KOReaderCredentialStore, compiled from the
// production sources against real ArduinoJson 7.4.2 and the real generated
// I18n tables.
//
// The stores are singletons, so tests share instances; every test explicitly
// establishes the state it asserts on instead of assuming a fresh store.
// CrossPointSettings is the one store whose fromJson() treats the *current
// in-memory values* as defaults for missing keys — several tests pin that.
//
// Board context: the BoardConfig stub models a plain button board (no touch,
// no home key, no IMU), so the touch-only settings entries are pruned from
// the persisted key set — pinned explicitly below.

#include <ArduinoJson.h>
#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <I18n.h>
#include <ObfuscationUtils.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"

namespace {

// ctest runs each discovered test as its own process, in parallel, in one
// shared working directory — the storage root must be per-process.
std::string processStoreRoot() { return "store_root_" + std::to_string(::getpid()); }

void removeTree(const std::string& path) {
  const std::string cmd = "rm -rf '" + path + "'";
  [[maybe_unused]] const int rc = std::system(cmd.c_str());
}

class StoreRootCleanup : public ::testing::Environment {
 public:
  void TearDown() override { removeTree(processStoreRoot()); }
};

const ::testing::Environment* const storeRootCleanup = ::testing::AddGlobalTestEnvironment(new StoreRootCleanup);

class StoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.root = processStoreRoot();
    removeTree(Storage.root);
    ::mkdir(Storage.root.c_str(), 0755);
    Storage.resetCounters();
  }

  // Write a device-side path (e.g. "/.crosspoint/wifi.json") through the
  // stub, then zero the write counters so tests count only store activity.
  static void writeDeviceFile(const char* path, const std::string& content) {
    Storage.mkdir("/.crosspoint");
    ASSERT_TRUE(Storage.writeFile(path, String(content)));
    Storage.resetCounters();
  }

  static std::string readDeviceFile(const char* path) { return Storage.readFile(path).str(); }

  static JsonDocument parseDeviceFile(const char* path) {
    JsonDocument doc;
    const std::string content = readDeviceFile(path);
    const auto err = deserializeJson(doc, content);
    EXPECT_EQ(err, DeserializationError::Ok) << "unparseable " << path << ": " << content;
    return doc;
  }

  // Create the backing file for a book path so pruneMissing() keeps it.
  static void createDeviceFile(const std::string& devicePath) {
    std::string dir = devicePath.substr(0, devicePath.find_last_of('/'));
    if (!dir.empty()) Storage.mkdir(dir.c_str());
    ASSERT_TRUE(Storage.writeFile(devicePath.c_str(), String("x")));
    Storage.resetCounters();
  }

  static std::string obf(const std::string& plaintext) { return obfuscation::obfuscateToBase64(plaintext).c_str(); }
};

// ---------------------------------------------------------------------------
// PersistableStoreBase plumbing (exercised through CrossPointState, the
// simplest concrete store).
// ---------------------------------------------------------------------------

using PersistableStoreBaseTest = StoreTest;

TEST_F(PersistableStoreBaseTest, SaveCreatesCrosspointDirAndFile) {
  APP_STATE.openEpubPath = "/books/x.epub";
  EXPECT_TRUE(APP_STATE.saveToFile());
  EXPECT_TRUE(Storage.exists("/.crosspoint/state.json"));
  EXPECT_EQ(Storage.writeCount, 1);
  ASSERT_EQ(Storage.writtenPaths.size(), 1u);
  EXPECT_EQ(Storage.writtenPaths[0], "/.crosspoint/state.json");
}

TEST_F(PersistableStoreBaseTest, LoadMissingFileReturnsFalseAndLeavesStateAlone) {
  APP_STATE.openEpubPath = "/books/kept.epub";
  EXPECT_FALSE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/kept.epub");
  EXPECT_EQ(Storage.writeCount, 0);
}

TEST_F(PersistableStoreBaseTest, LoadEmptyFileReturnsFalse) {
  writeDeviceFile("/.crosspoint/state.json", "");
  APP_STATE.openEpubPath = "/books/kept.epub";
  EXPECT_FALSE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/kept.epub");
}

TEST_F(PersistableStoreBaseTest, LoadNonJsonBytesReturnsFalse) {
  writeDeviceFile("/.crosspoint/state.json", std::string("\x01\x02\xffgarbage", 10));
  APP_STATE.openEpubPath = "/books/kept.epub";
  EXPECT_FALSE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/kept.epub");
}

TEST_F(PersistableStoreBaseTest, LoadTruncatedJsonReturnsFalse) {
  writeDeviceFile("/.crosspoint/state.json", "{\"openEpubPath\": \"/books/a.ep");
  APP_STATE.openEpubPath = "/books/kept.epub";
  EXPECT_FALSE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/kept.epub");
}

TEST_F(PersistableStoreBaseTest, LoadNonObjectTopLevelSucceedsWithDefaults) {
  // A JSON array parses fine; every key lookup misses, so the store loads
  // its documented defaults rather than failing. Pins current behavior.
  writeDeviceFile("/.crosspoint/state.json", "[1,2,3]");
  APP_STATE.openEpubPath = "/books/stale.epub";
  APP_STATE.showBootScreen = false;
  EXPECT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "");
  EXPECT_TRUE(APP_STATE.showBootScreen);
}

TEST_F(PersistableStoreBaseTest, DuplicateKeysLastValueWins) {
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"openEpubPath\":\"/books/first.epub\",\"openEpubPath\":\"/books/second.epub\"}");
  EXPECT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/second.epub");
}

TEST_F(PersistableStoreBaseTest, UnknownKeysAreIgnored) {
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"openEpubPath\":\"/books/a.epub\",\"someFutureKey\":{\"deep\":[1,2]},\"other\":true}");
  EXPECT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/a.epub");
}

TEST_F(PersistableStoreBaseTest, SaveFailurePropagates) {
  Storage.failNextWrite = true;
  EXPECT_FALSE(APP_STATE.saveToFile());
}

// ---------------------------------------------------------------------------
// CrossPointState
// ---------------------------------------------------------------------------

using CrossPointStateTest = StoreTest;

TEST_F(CrossPointStateTest, RoundTripAllFields) {
  APP_STATE.openEpubPath = "/books/round.epub";
  for (int i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; i++) {
    APP_STATE.recentSleepImages[i] = static_cast<uint16_t>(100 + i);
    APP_STATE.recentOverlaySleepImages[i] = static_cast<uint16_t>(500 + i);
  }
  APP_STATE.recentSleepPos = 5;
  APP_STATE.recentSleepFill = 16;
  APP_STATE.recentOverlaySleepPos = 9;
  APP_STATE.recentOverlaySleepFill = 12;
  APP_STATE.readerActivityLoadCount = 3;
  APP_STATE.lastSleepFromReader = true;
  APP_STATE.showBootScreen = false;
  ASSERT_TRUE(APP_STATE.saveToFile());

  APP_STATE.openEpubPath = "scrambled";
  APP_STATE.recentSleepImages[0] = 9999;
  APP_STATE.recentSleepPos = 0;
  APP_STATE.recentSleepFill = 0;
  APP_STATE.recentOverlaySleepPos = 0;
  APP_STATE.recentOverlaySleepFill = 0;
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.lastSleepFromReader = false;
  APP_STATE.showBootScreen = true;

  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "/books/round.epub");
  for (int i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; i++) {
    EXPECT_EQ(APP_STATE.recentSleepImages[i], 100 + i);
    EXPECT_EQ(APP_STATE.recentOverlaySleepImages[i], 500 + i);
  }
  EXPECT_EQ(APP_STATE.recentSleepPos, 5);
  EXPECT_EQ(APP_STATE.recentSleepFill, 16);
  EXPECT_EQ(APP_STATE.recentOverlaySleepPos, 9);
  EXPECT_EQ(APP_STATE.recentOverlaySleepFill, 12);
  EXPECT_EQ(APP_STATE.readerActivityLoadCount, 3);
  EXPECT_TRUE(APP_STATE.lastSleepFromReader);
  EXPECT_FALSE(APP_STATE.showBootScreen);
}

TEST_F(CrossPointStateTest, EmptyObjectLoadsDocumentedDefaults) {
  APP_STATE.openEpubPath = "/books/stale.epub";
  APP_STATE.pushRecentSleep(42);
  APP_STATE.readerActivityLoadCount = 9;
  APP_STATE.lastSleepFromReader = true;
  APP_STATE.showBootScreen = false;

  writeDeviceFile("/.crosspoint/state.json", "{}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "");
  EXPECT_EQ(APP_STATE.recentSleepFill, 0);
  EXPECT_EQ(APP_STATE.recentSleepPos, 0);
  EXPECT_EQ(APP_STATE.readerActivityLoadCount, 0);
  EXPECT_FALSE(APP_STATE.lastSleepFromReader);
  EXPECT_TRUE(APP_STATE.showBootScreen);  // documented default is true
}

TEST_F(CrossPointStateTest, WrongTypesFallBackToDefaults) {
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"openEpubPath\":12,\"recentSleepImages\":\"nope\",\"recentSleepPos\":\"three\","
                  "\"recentSleepFill\":[1],\"lastSleepFromReader\":\"yes\",\"showBootScreen\":0}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.openEpubPath, "");
  EXPECT_EQ(APP_STATE.recentSleepFill, 0);
  EXPECT_EQ(APP_STATE.recentSleepPos, 0);
  EXPECT_FALSE(APP_STATE.lastSleepFromReader);
  // JSON integer 0 is not a boolean for ArduinoJson: `| true` keeps the
  // default rather than coercing. Pins that a 0/1-writing client cannot
  // switch this flag off.
  EXPECT_TRUE(APP_STATE.showBootScreen);
}

TEST_F(CrossPointStateTest, OutOfRangePosWrapsWhenArrayPresent) {
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"recentSleepImages\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16],"
                  "\"recentSleepPos\":20,\"recentSleepFill\":16}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.recentSleepPos, 20 % CrossPointState::SLEEP_RECENT_COUNT);
  EXPECT_EQ(APP_STATE.recentSleepFill, 16);
}

TEST_F(CrossPointStateTest, OutOfRangePosResetsWhenArrayMissing) {
  writeDeviceFile("/.crosspoint/state.json", "{\"recentSleepPos\":20,\"recentSleepFill\":9}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.recentSleepPos, 0);
  EXPECT_EQ(APP_STATE.recentSleepFill, 0);  // clamped to actual array size
}

TEST_F(CrossPointStateTest, FillClampedToArraySizeAndOversizeArrayTruncated) {
  // 20 entries in the file, only SLEEP_RECENT_COUNT (16) are kept.
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"recentSleepImages\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20],"
                  "\"recentSleepPos\":0,\"recentSleepFill\":20}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.recentSleepFill, 16);
  EXPECT_EQ(APP_STATE.recentSleepImages[15], 16);
}

TEST_F(CrossPointStateTest, LegacyLastSleepImageSeedsRing) {
  writeDeviceFile("/.crosspoint/state.json", "{\"lastSleepImage\":7}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.recentSleepFill, 1);
  EXPECT_TRUE(APP_STATE.isRecentSleep(7, 1));
}

TEST_F(CrossPointStateTest, LegacyLastSleepImageIgnoredWhenRingPresent) {
  writeDeviceFile("/.crosspoint/state.json",
                  "{\"lastSleepImage\":7,\"recentSleepImages\":[3,4],\"recentSleepPos\":2,\"recentSleepFill\":2}");
  ASSERT_TRUE(APP_STATE.loadFromFile());
  EXPECT_EQ(APP_STATE.recentSleepFill, 2);
  EXPECT_FALSE(APP_STATE.isRecentSleep(7, 16));
  EXPECT_TRUE(APP_STATE.isRecentSleep(3, 16));
}

TEST_F(CrossPointStateTest, RecentSleepRingEvictsOldest) {
  writeDeviceFile("/.crosspoint/state.json", "{}");
  ASSERT_TRUE(APP_STATE.loadFromFile());  // reset ring
  for (uint16_t i = 0; i < 17; i++) APP_STATE.pushRecentSleep(i);
  EXPECT_FALSE(APP_STATE.isRecentSleep(0, 16));  // evicted by the 17th push
  EXPECT_TRUE(APP_STATE.isRecentSleep(1, 16));
  EXPECT_TRUE(APP_STATE.isRecentSleep(16, 1));  // most recent within lookback 1
  EXPECT_FALSE(APP_STATE.isRecentSleep(15, 1));
}

// ---------------------------------------------------------------------------
// CrossPointSettings
// ---------------------------------------------------------------------------

using CrossPointSettingsTest = StoreTest;

TEST_F(CrossPointSettingsTest, RoundTripRepresentativeFields) {
  SETTINGS.uiTheme = CrossPointSettings::ROUNDEDRAFF;
  SETTINGS.orientation = CrossPointSettings::LANDSCAPE_CCW;
  SETTINGS.screenMargin = 25;
  SETTINGS.hyphenationEnabled = 1;
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.fontPointSize = 16;
  SETTINGS.lineSpacing = CrossPointSettings::WIDE;
  SETTINGS.sleepTimeoutMinutes = 22;
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_30;
  snprintf(SETTINGS.opdsDownloadFolder, sizeof(SETTINGS.opdsDownloadFolder), "Books/OPDS");
  snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "MyFont");
  snprintf(SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName), "webster");
  SETTINGS.language = static_cast<uint8_t>(I18n::languageFromCode("DE"));
  SETTINGS.keyboardLayouts = 0x0005;
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
  ASSERT_TRUE(SETTINGS.saveToFile());

  SETTINGS.uiTheme = CrossPointSettings::LYRA;
  SETTINGS.orientation = CrossPointSettings::PORTRAIT;
  SETTINGS.screenMargin = 5;
  SETTINGS.hyphenationEnabled = 0;
  SETTINGS.fontFamily = CrossPointSettings::NOTOSERIF;
  SETTINGS.fontPointSize = 14;
  SETTINGS.lineSpacing = CrossPointSettings::NORMAL;
  SETTINGS.sleepTimeoutMinutes = 10;
  SETTINGS.refreshFrequency = CrossPointSettings::REFRESH_15;
  SETTINGS.opdsDownloadFolder[0] = '\0';
  SETTINGS.sdFontFamilyName[0] = '\0';
  SETTINGS.dictionaryName[0] = '\0';
  SETTINGS.language = 0;
  SETTINGS.keyboardLayouts = 0;
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;

  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.uiTheme, CrossPointSettings::ROUNDEDRAFF);
  EXPECT_EQ(SETTINGS.orientation, CrossPointSettings::LANDSCAPE_CCW);
  EXPECT_EQ(SETTINGS.screenMargin, 25);
  EXPECT_EQ(SETTINGS.hyphenationEnabled, 1);
  EXPECT_EQ(SETTINGS.fontFamily, CrossPointSettings::NOTOSANS);
  EXPECT_EQ(SETTINGS.fontPointSize, 16);
  EXPECT_EQ(SETTINGS.lineSpacing, CrossPointSettings::WIDE);
  EXPECT_EQ(SETTINGS.sleepTimeoutMinutes, 22);
  EXPECT_EQ(SETTINGS.refreshFrequency, CrossPointSettings::REFRESH_30);
  EXPECT_STREQ(SETTINGS.opdsDownloadFolder, "Books/OPDS");
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "MyFont");
  EXPECT_STREQ(SETTINGS.dictionaryName, "webster");
  EXPECT_EQ(SETTINGS.language, static_cast<uint8_t>(I18n::languageFromCode("DE")));
  EXPECT_EQ(SETTINGS.keyboardLayouts, 0x0005);
  EXPECT_EQ(SETTINGS.frontButtonBack, CrossPointSettings::FRONT_HW_CONFIRM);
  EXPECT_EQ(SETTINGS.frontButtonConfirm, CrossPointSettings::FRONT_HW_BACK);
  EXPECT_EQ(SETTINGS.frontButtonLeft, CrossPointSettings::FRONT_HW_RIGHT);
  EXPECT_EQ(SETTINGS.frontButtonRight, CrossPointSettings::FRONT_HW_LEFT);
  EXPECT_EQ(Storage.writeCount, 1);  // the save; no resave on a modern file
}

TEST_F(CrossPointSettingsTest, LanguagePersistsAsCodeStringNotIndex) {
  SETTINGS.language = static_cast<uint8_t>(I18n::languageFromCode("DE"));
  ASSERT_TRUE(SETTINGS.saveToFile());
  JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
  ASSERT_TRUE(doc["language"].is<const char*>());
  EXPECT_STREQ(doc["language"].as<const char*>(), "DE");
}

TEST_F(CrossPointSettingsTest, MissingKeysKeepCurrentValuesForListSettings) {
  // For settings in the generic list the fromJson() "default" is the current
  // in-memory value, NOT the factory default. Pins that semantic.
  SETTINGS.sleepScreen = CrossPointSettings::LIGHT;
  SETTINGS.screenMargin = 30;
  SETTINGS.keyboardLayouts = 7;
  SETTINGS.language = static_cast<uint8_t>(I18n::languageFromCode("FR"));
  writeDeviceFile("/.crosspoint/settings.json", "{}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepScreen, CrossPointSettings::LIGHT);
  EXPECT_EQ(SETTINGS.screenMargin, 30);
  EXPECT_EQ(SETTINGS.keyboardLayouts, 7);
  EXPECT_EQ(SETTINGS.language, static_cast<uint8_t>(I18n::languageFromCode("FR")));
}

TEST_F(CrossPointSettingsTest, MissingKeysFactoryResetManualFields) {
  // The manually-handled fields behave differently from the generic list:
  // fontFamily/fontSize/front-button remap fall back to factory defaults and
  // sdFontFamilyName/dictionaryName are cleared outright. Pins the split.
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.fontPointSize = 18;
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_BACK;
  snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "MyFont");
  snprintf(SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName), "webster");
  snprintf(SETTINGS.opdsDownloadFolder, sizeof(SETTINGS.opdsDownloadFolder), "kept");

  writeDeviceFile("/.crosspoint/settings.json", "{}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.fontFamily, CrossPointSettings::NOTOSERIF);
  EXPECT_EQ(SETTINGS.fontPointSize, CrossPointSettings::DEFAULT_FONT_POINT_SIZE);
  EXPECT_EQ(SETTINGS.frontButtonBack, CrossPointSettings::FRONT_HW_BACK);
  EXPECT_EQ(SETTINGS.frontButtonConfirm, CrossPointSettings::FRONT_HW_CONFIRM);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
  EXPECT_STREQ(SETTINGS.dictionaryName, "");
  // ...while a generic-list string setting keeps its current value.
  EXPECT_STREQ(SETTINGS.opdsDownloadFolder, "kept");
}

TEST_F(CrossPointSettingsTest, EnumOutOfRangeFallsBackToCurrentValue) {
  SETTINGS.sleepScreen = CrossPointSettings::DARK;
  writeDeviceFile("/.crosspoint/settings.json", "{\"sleepScreen\":200}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepScreen, CrossPointSettings::DARK);

  writeDeviceFile("/.crosspoint/settings.json", "{\"sleepScreen\":8}");  // == SLEEP_SCREEN_MODE_COUNT
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepScreen, CrossPointSettings::DARK);

  writeDeviceFile("/.crosspoint/settings.json", "{\"sleepScreen\":7}");  // last valid value
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepScreen, CrossPointSettings::TRANSPARENT_CUSTOM);
}

TEST_F(CrossPointSettingsTest, HostileNumericAndStringTypes) {
  SETTINGS.sleepScreen = CrossPointSettings::COVER;
  SETTINGS.screenMargin = 15;
  SETTINGS.statusBarBattery = 1;
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"sleepScreen\":\"dark\",\"screenMargin\":300,\"statusBarBattery\":5,\"orientation\":-1}");
  SETTINGS.orientation = CrossPointSettings::INVERTED;
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepScreen, CrossPointSettings::COVER);     // string -> current value
  EXPECT_EQ(SETTINGS.screenMargin, 15);                           // 300 exceeds uint8 -> current value
  EXPECT_EQ(SETTINGS.statusBarBattery, 1);                        // toggle 5 -> current value
  EXPECT_EQ(SETTINGS.orientation, CrossPointSettings::INVERTED);  // -1 -> current value
}

TEST_F(CrossPointSettingsTest, ValueSettingsClampToRange) {
  // screenMargin range is {5, 40, 5}: representable out-of-range values clamp
  // to the bounds (unlike unrepresentable ones, which keep the current value).
  writeDeviceFile("/.crosspoint/settings.json", "{\"screenMargin\":200}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.screenMargin, CrossPointSettings::SCREEN_MARGIN_MAX);

  writeDeviceFile("/.crosspoint/settings.json", "{\"screenMargin\":3}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.screenMargin, CrossPointSettings::SCREEN_MARGIN_MIN);
}

TEST_F(CrossPointSettingsTest, LegacyFontSizeSlotFoldsToPointSizeAndResaves) {
  writeDeviceFile("/.crosspoint/settings.json", "{\"fontSize\":2}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.fontPointSize, 16);  // slot 2 (LARGE) -> 16pt
  EXPECT_EQ(Storage.writeCount, 1);       // migration resave
  JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
  EXPECT_EQ(doc["fontSize"] | 0, 16);

  // Reloading the migrated file must not resave again.
  Storage.resetCounters();
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(Storage.writeCount, 0);
}

TEST_F(CrossPointSettingsTest, LegacyOpenDyslexicFamilyBecomesSdFont) {
  writeDeviceFile("/.crosspoint/settings.json", "{\"fontFamily\":2}");
  SETTINGS.sdFontFamilyName[0] = '\0';
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.fontFamily, CrossPointSettings::NOTOSERIF);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "OpenDyslexic");
  EXPECT_GE(Storage.writeCount, 1);
}

TEST_F(CrossPointSettingsTest, LegacyFamilyIndexWithSdFontKeepsSdFont) {
  writeDeviceFile("/.crosspoint/settings.json", "{\"fontFamily\":2,\"sdFontFamilyName\":\"Custom\"}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.fontFamily, CrossPointSettings::NOTOSERIF);  // clamped
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "Custom");
  EXPECT_GE(Storage.writeCount, 1);  // out-of-range family still resaves
}

TEST_F(CrossPointSettingsTest, LegacySleepTimeoutEnumMigrates) {
  writeDeviceFile("/.crosspoint/settings.json", "{\"sleepTimeout\":3}");  // SLEEP_15_MIN
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepTimeoutMinutes, 15);
  EXPECT_EQ(Storage.writeCount, 1);
  JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
  EXPECT_EQ(doc["sleepTimeoutMinutes"] | 0, 15);
}

TEST_F(CrossPointSettingsTest, ModernSleepTimeoutWinsOverLegacy) {
  writeDeviceFile("/.crosspoint/settings.json", "{\"sleepTimeout\":0,\"sleepTimeoutMinutes\":25}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.sleepTimeoutMinutes, 25);
  EXPECT_EQ(Storage.writeCount, 0);  // no migration write
}

TEST_F(CrossPointSettingsTest, DuplicateFrontButtonMappingResetsToIdentity) {
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"frontButtonBack\":1,\"frontButtonConfirm\":1,\"frontButtonLeft\":2,\"frontButtonRight\":3}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.frontButtonBack, CrossPointSettings::FRONT_HW_BACK);
  EXPECT_EQ(SETTINGS.frontButtonConfirm, CrossPointSettings::FRONT_HW_CONFIRM);
  EXPECT_EQ(SETTINGS.frontButtonLeft, CrossPointSettings::FRONT_HW_LEFT);
  EXPECT_EQ(SETTINGS.frontButtonRight, CrossPointSettings::FRONT_HW_RIGHT);
}

TEST_F(CrossPointSettingsTest, ValidFrontButtonPermutationKept) {
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"frontButtonBack\":3,\"frontButtonConfirm\":2,\"frontButtonLeft\":1,\"frontButtonRight\":0}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.frontButtonBack, CrossPointSettings::FRONT_HW_RIGHT);
  EXPECT_EQ(SETTINGS.frontButtonConfirm, CrossPointSettings::FRONT_HW_LEFT);
  EXPECT_EQ(SETTINGS.frontButtonLeft, CrossPointSettings::FRONT_HW_CONFIRM);
  EXPECT_EQ(SETTINGS.frontButtonRight, CrossPointSettings::FRONT_HW_BACK);
}

TEST_F(CrossPointSettingsTest, OversizedStringsTruncateToFieldCapacity) {
  const std::string longFolder(100, 'f');
  const std::string longFont(40, 'g');
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"opdsDownloadFolder\":\"" + longFolder + "\",\"sdFontFamilyName\":\"" + longFont + "\"}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(std::string(SETTINGS.opdsDownloadFolder), std::string(63, 'f'));
  EXPECT_EQ(std::string(SETTINGS.sdFontFamilyName), std::string(31, 'g'));
}

TEST_F(CrossPointSettingsTest, UnknownLanguageCodeFallsBackToEnglish) {
  SETTINGS.language = static_cast<uint8_t>(I18n::languageFromCode("DE"));
  writeDeviceFile("/.crosspoint/settings.json", "{\"language\":\"XX\"}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.language, static_cast<uint8_t>(Language::EN));

  // A numeric language value (pre-code format) is ignored entirely.
  SETTINGS.language = static_cast<uint8_t>(I18n::languageFromCode("DE"));
  writeDeviceFile("/.crosspoint/settings.json", "{\"language\":3}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.language, static_cast<uint8_t>(I18n::languageFromCode("DE")));
}

TEST_F(CrossPointSettingsTest, KeyboardLayoutsOmittedWhenUnconfigured) {
  SETTINGS.keyboardLayouts = 0;
  ASSERT_TRUE(SETTINGS.saveToFile());
  {
    JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
    EXPECT_TRUE(doc["keyboardLayouts"].isNull());
  }
  SETTINGS.keyboardLayouts = 0x0203;
  ASSERT_TRUE(SETTINGS.saveToFile());
  {
    JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
    EXPECT_EQ(doc["keyboardLayouts"] | 0, 0x0203);
  }
}

TEST_F(CrossPointSettingsTest, TouchOnlyKeysNotPersistedOnButtonBoard) {
  // The stub board has neither touch nor a home key, so SettingsList prunes
  // touchReaderControls, readerMenuStyle and tapForReaderMenu from the list —
  // they never reach settings.json on such a board.
  ASSERT_TRUE(SETTINGS.saveToFile());
  JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
  EXPECT_TRUE(doc["touchReaderControls"].isNull());
  EXPECT_TRUE(doc["readerMenuStyle"].isNull());
  EXPECT_TRUE(doc["tapForReaderMenu"].isNull());
  // ...and a value in the file for a pruned key is ignored on load.
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_SWIPE;
  writeDeviceFile("/.crosspoint/settings.json", "{\"touchReaderControls\":0}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.touchReaderControls, CrossPointSettings::TOUCH_READER_SWIPE);
}

TEST_F(CrossPointSettingsTest, EnumListSizeIsBoardDependentForShortPwrBtn) {
  // Non-touch boards list 5 short-power-button options (no PWR_CONFIRM), so a
  // file written by a touch board with shortPwrBtn=5 falls back to the current
  // value on this board. Pins the cross-board load behavior.
  SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
  writeDeviceFile("/.crosspoint/settings.json", "{\"shortPwrBtn\":5}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.shortPwrBtn, CrossPointSettings::IGNORE);

  writeDeviceFile("/.crosspoint/settings.json", "{\"shortPwrBtn\":4}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.shortPwrBtn, CrossPointSettings::FOOTNOTES);
}

TEST_F(CrossPointSettingsTest, LongPressMenuReaderMenuValueRejectedWithoutHomeKey) {
  // buildLongPressMenuValues() drops LP_MENU_READER_MENU (4) on boards
  // without a home key, so the stored index 4 cannot survive a load there.
  SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_DISABLED;
  writeDeviceFile("/.crosspoint/settings.json", "{\"longPressMenuFunction\":4}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_DISABLED);

  writeDeviceFile("/.crosspoint/settings.json", "{\"longPressMenuFunction\":3}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_DICTIONARY);
}

TEST_F(CrossPointSettingsTest, MigrationResaveDropsUnknownKeys) {
  // A resave rewrites the whole document from the in-memory model: keys this
  // firmware does not know are lost. Pins the (lossy) current behavior.
  writeDeviceFile("/.crosspoint/settings.json", "{\"fontSize\":1,\"newerFirmwareKey\":42}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(Storage.writeCount, 1);
  JsonDocument doc = parseDeviceFile("/.crosspoint/settings.json");
  EXPECT_TRUE(doc["newerFirmwareKey"].isNull());
  EXPECT_EQ(doc["fontSize"] | 0, 14);  // slot 1 -> 14pt
}

TEST_F(CrossPointSettingsTest, StatusBarSpecReflectsPersistedFields) {
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"statusBarProgressBar\":1,\"statusBarProgressBarThickness\":2,\"statusBarTitle\":0,"
                  "\"statusBarBattery\":1,\"hideBatteryPercentage\":2,\"clockFormat\":1}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  const auto spec = SETTINGS.statusBarSpec();
  EXPECT_EQ(spec.progressBarMode, CrossPointSettings::CHAPTER_PROGRESS);
  EXPECT_EQ(spec.progressBarHeightPx, 6);  // (thickness 2 + 1) * 2
  EXPECT_EQ(spec.titleMode, CrossPointSettings::BOOK_TITLE);
  EXPECT_TRUE(spec.showBattery);
  EXPECT_FALSE(spec.showBatteryPercent);  // HIDE_ALWAYS
  EXPECT_TRUE(spec.clock12h);
}

// ---------------------------------------------------------------------------
// RecentBooksStore
// ---------------------------------------------------------------------------

class RecentBooksTest : public StoreTest {
 protected:
  void SetUp() override {
    StoreTest::SetUp();
    // Reset the singleton list through the JSON path.
    writeDeviceFile("/.crosspoint/recent.json", "{\"books\":[]}");
    ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
    Storage.resetCounters();
  }

  static std::string bookPath(int i) { return "/books/book" + std::to_string(i) + ".epub"; }

  void addExistingBook(int i) {
    createDeviceFile(bookPath(i));
    RECENT_BOOKS.addBook(bookPath(i), "Title" + std::to_string(i), "Author" + std::to_string(i), "");
  }
};

TEST_F(RecentBooksTest, AddBookOrdersMostRecentFirstAndPersists) {
  addExistingBook(1);
  addExistingBook(2);
  addExistingBook(3);
  ASSERT_EQ(RECENT_BOOKS.getCount(), 3);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, bookPath(3));
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].path, bookPath(2));
  EXPECT_EQ(RECENT_BOOKS.getBooks()[2].path, bookPath(1));
  EXPECT_TRUE(Storage.exists("/.crosspoint/recent.json"));
}

TEST_F(RecentBooksTest, ReAddingMovesToFrontWithoutDuplicate) {
  addExistingBook(1);
  addExistingBook(2);
  addExistingBook(1);
  ASSERT_EQ(RECENT_BOOKS.getCount(), 2);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, bookPath(1));
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].path, bookPath(2));
}

TEST_F(RecentBooksTest, EvictionCapsListAtTen) {
  for (int i = 1; i <= 11; i++) addExistingBook(i);
  ASSERT_EQ(RECENT_BOOKS.getCount(), 10);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, bookPath(11));
  // book1 (oldest) was evicted; book2 survived as the last entry.
  EXPECT_EQ(RECENT_BOOKS.getBooks()[9].path, bookPath(2));
  for (const auto& b : RECENT_BOOKS.getBooks()) EXPECT_NE(b.path, bookPath(1));
}

TEST_F(RecentBooksTest, AddPrunesMissingEntriesBeforeEvicting) {
  for (int i = 1; i <= 10; i++) addExistingBook(i);
  // book7's backing file disappears from the SD card.
  ASSERT_TRUE(Storage.remove(bookPath(7).c_str()));
  addExistingBook(11);
  ASSERT_EQ(RECENT_BOOKS.getCount(), 10);
  // The stale entry was pruned instead of evicting valid book1.
  for (const auto& b : RECENT_BOOKS.getBooks()) EXPECT_NE(b.path, bookPath(7));
  EXPECT_EQ(RECENT_BOOKS.getBooks()[9].path, bookPath(1));
}

TEST_F(RecentBooksTest, RoundTripPreservesOrderAndFields) {
  createDeviceFile("/books/a.epub");
  createDeviceFile("/books/b.fb2");
  RECENT_BOOKS.addBook("/books/a.epub", "Alpha", "Anna Author", "/.crosspoint/epub_1/cover.bmp");
  RECENT_BOOKS.addBook("/books/b.fb2", "Beta", "Bob Writer", "");
  const std::string savedJson = readDeviceFile("/.crosspoint/recent.json");

  // Scramble memory (removeByPath also persists, so restore the file after).
  RECENT_BOOKS.removeByPath("/books/a.epub");
  writeDeviceFile("/.crosspoint/recent.json", savedJson);
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  ASSERT_EQ(RECENT_BOOKS.getCount(), 2);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, "/books/b.fb2");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].title, "Beta");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].author, "Bob Writer");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].path, "/books/a.epub");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].title, "Alpha");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].coverBmpPath, "/.crosspoint/epub_1/cover.bmp");
}

TEST_F(RecentBooksTest, LoadCapsAtTenEntries) {
  std::string json = "{\"books\":[";
  for (int i = 0; i < 12; i++) {
    if (i) json += ",";
    json +=
        "{\"path\":\"/books/x" + std::to_string(i) + ".epub\",\"title\":\"T\",\"author\":\"A\",\"coverBmpPath\":\"\"}";
  }
  json += "]}";
  writeDeviceFile("/.crosspoint/recent.json", json);
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  EXPECT_EQ(RECENT_BOOKS.getCount(), 10);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[9].path, "/books/x9.epub");
}

// ---------------------------------------------------------------------------
// Untrusted-length bounds (T144). A store file comes off a removable card and
// the API body off the network, so neither may size an allocation on its own.
// ---------------------------------------------------------------------------

TEST_F(StoreTest, AnOversizedStoreFileIsRefusedBeforeItIsRead) {
  // Valid JSON, just far too big: readDocFromFile checks the size first, so the
  // 380 KB heap never sees it.
  std::string huge = "{\"books\":[{\"title\":\"";
  huge.append(PersistableStoreBase::MAX_STORE_FILE_BYTES + 1024, 'x');
  huge += "\"}]}";
  writeDeviceFile("/.crosspoint/recent.json", huge);

  const int countBefore = RECENT_BOOKS.getCount();
  EXPECT_FALSE(RECENT_BOOKS.loadFromFile());
  EXPECT_EQ(RECENT_BOOKS.getCount(), countBefore) << "a refused load must leave the model alone";
}

TEST_F(StoreTest, AFileJustUnderTheCeilingStillLoads) {
  const size_t padding = PersistableStoreBase::MAX_STORE_FILE_BYTES - 512;
  std::string doc = "{\"books\":[{\"path\":\"/books/a.epub\",\"title\":\"t\"}],\"pad\":\"";
  doc.append(padding - doc.size(), 'x');
  doc += "\"}";
  ASSERT_LT(doc.size(), PersistableStoreBase::MAX_STORE_FILE_BYTES);
  writeDeviceFile("/.crosspoint/recent.json", doc);

  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  ASSERT_EQ(RECENT_BOOKS.getCount(), 1);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, "/books/a.epub");
}

TEST_F(StoreTest, OverLongRecentBookFieldsAreRefusedNotTruncated) {
  const std::string longPath(PersistableStoreBase::MAX_PATH_BYTES + 1, 'p');
  const std::string longTitle(PersistableStoreBase::MAX_TITLE_BYTES + 1, 't');
  writeDeviceFile("/.crosspoint/recent.json",
                  "{\"books\":[{\"path\":\"" + longPath + "\",\"title\":\"" + longTitle + "\",\"author\":\"ok\"}]}");

  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  ASSERT_EQ(RECENT_BOOKS.getCount(), 1);
  EXPECT_TRUE(RECENT_BOOKS.getBooks()[0].path.empty()) << "a truncated path would point at the wrong file";
  EXPECT_TRUE(RECENT_BOOKS.getBooks()[0].title.empty());
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].author, "ok") << "fields within the bound are untouched";
}

TEST_F(StoreTest, RecentBookFieldsAtTheBoundAreKept) {
  const std::string atBound(PersistableStoreBase::MAX_PATH_BYTES, 'p');
  writeDeviceFile("/.crosspoint/recent.json", "{\"books\":[{\"path\":\"" + atBound + "\"}]}");

  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  ASSERT_EQ(RECENT_BOOKS.getCount(), 1);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path.size(), PersistableStoreBase::MAX_PATH_BYTES);
}

TEST_F(StoreTest, OverLongOpdsFieldsAreRefusedNotTruncated) {
  const std::string longName(PersistableStoreBase::MAX_NAME_BYTES + 1, 'n');
  const std::string longUrl(PersistableStoreBase::MAX_URL_BYTES + 1, 'u');
  writeDeviceFile("/.crosspoint/opds.json",
                  "{\"servers\":[{\"name\":\"" + longName + "\",\"url\":\"" + longUrl + "\",\"username\":\"user\"}]}");

  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  const auto& server = OPDS_STORE.getServers()[0];
  EXPECT_TRUE(server.name.empty());
  EXPECT_TRUE(server.url.empty());
  EXPECT_EQ(server.username, "user");
}

TEST_F(RecentBooksTest, LoadToleratesMissingOrWrongTypedBooksKey) {
  writeDeviceFile("/.crosspoint/recent.json", "{}");
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  EXPECT_EQ(RECENT_BOOKS.getCount(), 0);

  writeDeviceFile("/.crosspoint/recent.json", "{\"books\":42}");
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  EXPECT_EQ(RECENT_BOOKS.getCount(), 0);

  writeDeviceFile("/.crosspoint/recent.json", "{\"books\":[{\"title\":\"orphan\"},{\"path\":\"/books/p.epub\"}]}");
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  ASSERT_EQ(RECENT_BOOKS.getCount(), 2);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, "");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].title, "orphan");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].path, "/books/p.epub");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].title, "");
}

TEST_F(RecentBooksTest, LoadDoesNotPruneMissingBooks) {
  // fromJson keeps entries whose backing file is gone; pruning is a separate,
  // caller-driven step. Pins the split of responsibilities.
  writeDeviceFile(
      "/.crosspoint/recent.json",
      "{\"books\":[{\"path\":\"/books/ghost.epub\",\"title\":\"G\",\"author\":\"\",\"coverBmpPath\":\"\"}]}");
  ASSERT_TRUE(RECENT_BOOKS.loadFromFile());
  EXPECT_EQ(RECENT_BOOKS.getCount(), 1);
  Storage.resetCounters();
  EXPECT_TRUE(RECENT_BOOKS.pruneMissing());
  EXPECT_EQ(RECENT_BOOKS.getCount(), 0);
  EXPECT_EQ(Storage.writeCount, 0);  // pruneMissing does not persist
}

TEST_F(RecentBooksTest, RemoveByPathPersistsAndReportsAccurately) {
  addExistingBook(1);
  addExistingBook(2);
  Storage.resetCounters();
  EXPECT_TRUE(RECENT_BOOKS.removeByPath(bookPath(1)));
  EXPECT_EQ(RECENT_BOOKS.getCount(), 1);
  EXPECT_EQ(Storage.writeCount, 1);
  EXPECT_FALSE(RECENT_BOOKS.removeByPath("/books/absent.epub"));
  EXPECT_EQ(Storage.writeCount, 1);  // no write for a no-op removal
}

TEST_F(RecentBooksTest, UpdateBookRewritesFieldsInPlace) {
  addExistingBook(1);
  addExistingBook(2);
  Storage.resetCounters();
  RECENT_BOOKS.updateBook(bookPath(1), "New Title", "New Author", "/covers/new.bmp");
  ASSERT_EQ(RECENT_BOOKS.getCount(), 2);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].path, bookPath(1));  // position kept
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].title, "New Title");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[1].author, "New Author");
  EXPECT_EQ(Storage.writeCount, 1);

  RECENT_BOOKS.updateBook("/books/absent.epub", "T", "A", "");
  EXPECT_EQ(Storage.writeCount, 1);  // no write when nothing matched
}

TEST_F(RecentBooksTest, UpdatePathRepointsCoverUnderOldCacheDir) {
  createDeviceFile("/books/a.epub");
  RECENT_BOOKS.addBook("/books/a.epub", "A", "", "/.crosspoint/epub_old/cover.bmp");
  RECENT_BOOKS.updatePath("/books/a.epub", "/Read/a.epub", "/.crosspoint/epub_old", "/.crosspoint/epub_new");
  ASSERT_EQ(RECENT_BOOKS.getCount(), 1);
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, "/Read/a.epub");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].coverBmpPath, "/.crosspoint/epub_new/cover.bmp");
}

TEST_F(RecentBooksTest, UpdatePathLeavesForeignCoverAlone) {
  createDeviceFile("/books/b.epub");
  RECENT_BOOKS.addBook("/books/b.epub", "B", "", "/sleep/custom.bmp");
  RECENT_BOOKS.updatePath("/books/b.epub", "/Read/b.epub", "/.crosspoint/epub_old", "/.crosspoint/epub_new");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].path, "/Read/b.epub");
  EXPECT_EQ(RECENT_BOOKS.getBooks()[0].coverBmpPath, "/sleep/custom.bmp");
}

// ---------------------------------------------------------------------------
// OpdsServerStore
// ---------------------------------------------------------------------------

class OpdsTest : public StoreTest {
 protected:
  void SetUp() override {
    StoreTest::SetUp();
    writeDeviceFile("/.crosspoint/opds.json", "{\"servers\":[]}");
    ASSERT_TRUE(OPDS_STORE.loadFromFile());
    Storage.resetCounters();
  }
};

TEST_F(OpdsTest, RoundTripObfuscatesPasswordOnDisk) {
  ASSERT_TRUE(OPDS_STORE.addServer({"Calibre", "http://192.168.1.2:8080/opds", "reader", "s3cretPW"}));
  const std::string raw = readDeviceFile("/.crosspoint/opds.json");
  EXPECT_EQ(raw.find("s3cretPW"), std::string::npos) << raw;
  JsonDocument doc = parseDeviceFile("/.crosspoint/opds.json");
  EXPECT_FALSE(doc["servers"][0]["password_obf"].isNull());
  EXPECT_TRUE(doc["servers"][0]["password"].isNull());

  // Scramble memory (removeServer also persists, so restore the file after),
  // reload, verify the plaintext comes back.
  ASSERT_TRUE(OPDS_STORE.removeServer(0));
  writeDeviceFile("/.crosspoint/opds.json", raw);
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  EXPECT_EQ(OPDS_STORE.getServer(0)->name, "Calibre");
  EXPECT_EQ(OPDS_STORE.getServer(0)->url, "http://192.168.1.2:8080/opds");
  EXPECT_EQ(OPDS_STORE.getServer(0)->username, "reader");
  EXPECT_EQ(OPDS_STORE.getServer(0)->password, "s3cretPW");
}

TEST_F(OpdsTest, LegacyPlaintextPasswordUpgradedOnLoad) {
  writeDeviceFile(
      "/.crosspoint/opds.json",
      "{\"servers\":[{\"name\":\"Old\",\"url\":\"http://x\",\"username\":\"u\",\"password\":\"legacyPW\"}]}");
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  EXPECT_EQ(OPDS_STORE.getServer(0)->password, "legacyPW");
  EXPECT_EQ(Storage.writeCount, 1);  // upgrade resave
  const std::string raw = readDeviceFile("/.crosspoint/opds.json");
  EXPECT_EQ(raw.find("legacyPW"), std::string::npos) << raw;
  JsonDocument doc = parseDeviceFile("/.crosspoint/opds.json");
  EXPECT_FALSE(doc["servers"][0]["password_obf"].isNull());
}

TEST_F(OpdsTest, EmptyPasswordRoundTripsWithoutResave) {
  ASSERT_TRUE(OPDS_STORE.addServer({"NoAuth", "http://x", "", ""}));
  Storage.resetCounters();
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  EXPECT_EQ(OPDS_STORE.getServer(0)->password, "");
  EXPECT_EQ(Storage.writeCount, 0);
}

TEST_F(OpdsTest, CorruptBase64FallsBackToLegacyPlaintextIfPresent) {
  writeDeviceFile("/.crosspoint/opds.json",
                  "{\"servers\":[{\"name\":\"S\",\"url\":\"http://x\",\"username\":\"u\","
                  "\"password_obf\":\"!!!notbase64!!!\",\"password\":\"fallbackPW\"}]}");
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  EXPECT_EQ(OPDS_STORE.getServer(0)->password, "fallbackPW");
  EXPECT_EQ(Storage.writeCount, 1);  // re-obfuscation resave

  writeDeviceFile("/.crosspoint/opds.json",
                  "{\"servers\":[{\"name\":\"S\",\"url\":\"http://x\",\"username\":\"u\","
                  "\"password_obf\":\"!!!notbase64!!!\"}]}");
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  ASSERT_EQ(OPDS_STORE.getCount(), 1u);
  EXPECT_EQ(OPDS_STORE.getServer(0)->password, "");  // nothing to recover
  // An unrecoverable password decodes to empty without flagging a resave:
  // the corrupt password_obf stays in the file. Pins current behavior.
  EXPECT_EQ(Storage.writeCount, 0);
}

TEST_F(OpdsTest, ServerLimitEnforcedOnAddAndLoad) {
  for (int i = 0; i < 8; i++) {
    ASSERT_TRUE(OPDS_STORE.addServer({"S" + std::to_string(i), "http://s", "", ""}));
  }
  EXPECT_FALSE(OPDS_STORE.addServer({"overflow", "http://s", "", ""}));
  EXPECT_EQ(OPDS_STORE.getCount(), 8u);

  std::string json = "{\"servers\":[";
  for (int i = 0; i < 10; i++) {
    if (i) json += ",";
    json += "{\"name\":\"L" + std::to_string(i) + "\",\"url\":\"http://x\",\"username\":\"\",\"password\":\"\"}";
  }
  json += "]}";
  writeDeviceFile("/.crosspoint/opds.json", json);
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  EXPECT_EQ(OPDS_STORE.getCount(), 8u);
  EXPECT_EQ(OPDS_STORE.getServer(7)->name, "L7");
}

TEST_F(OpdsTest, UpdateAndRemoveCheckBounds) {
  ASSERT_TRUE(OPDS_STORE.addServer({"S", "http://x", "", ""}));
  Storage.resetCounters();
  EXPECT_FALSE(OPDS_STORE.updateServer(1, {"N", "http://y", "", ""}));
  EXPECT_FALSE(OPDS_STORE.removeServer(1));
  EXPECT_EQ(Storage.writeCount, 0);
  EXPECT_TRUE(OPDS_STORE.updateServer(0, {"N", "http://y", "", ""}));
  EXPECT_EQ(OPDS_STORE.getServer(0)->name, "N");
  EXPECT_EQ(OPDS_STORE.getServer(1), nullptr);
  EXPECT_TRUE(OPDS_STORE.removeServer(0));
  EXPECT_FALSE(OPDS_STORE.hasServers());
}

TEST_F(OpdsTest, MissingOrWrongTypedServersKeyLoadsEmpty) {
  ASSERT_TRUE(OPDS_STORE.addServer({"S", "http://x", "", ""}));
  writeDeviceFile("/.crosspoint/opds.json", "{\"servers\":\"oops\"}");
  ASSERT_TRUE(OPDS_STORE.loadFromFile());
  EXPECT_EQ(OPDS_STORE.getCount(), 0u);
}

// ---------------------------------------------------------------------------
// WifiCredentialStore (CredentialIntegrity in the loop)
// ---------------------------------------------------------------------------

class WifiTest : public StoreTest {
 protected:
  void SetUp() override {
    StoreTest::SetUp();
    WIFI_STORE.clearAll();
    // clearAll saved an empty file; recount from a clean slate.
    Storage.resetCounters();
  }
};

TEST_F(WifiTest, RoundTripCredentialsAndLastConnected) {
  ASSERT_TRUE(WIFI_STORE.addCredential("HomeNet", "hunter2!"));
  ASSERT_TRUE(WIFI_STORE.addCredential("Cafe", ""));
  WIFI_STORE.setLastConnectedSsid("HomeNet");
  const std::string savedJson = readDeviceFile("/.crosspoint/wifi.json");

  // Scramble memory (removeCredential also persists, so restore the file
  // after), then reload from disk.
  ASSERT_TRUE(WIFI_STORE.removeCredential("HomeNet"));
  writeDeviceFile("/.crosspoint/wifi.json", savedJson);
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 2u);
  const auto cred = WIFI_STORE.findCredential("HomeNet");
  ASSERT_TRUE(cred.has_value());
  EXPECT_EQ(cred->password, "hunter2!");
  EXPECT_EQ(WIFI_STORE.getLastConnectedSsid(), "HomeNet");
  const auto summaries = WIFI_STORE.getCredentialSummaries();
  ASSERT_EQ(summaries.size(), 2u);
  EXPECT_TRUE(summaries[0].hasPassword);
  EXPECT_TRUE(summaries[0].isLastConnected);
  EXPECT_FALSE(summaries[1].hasPassword);
  EXPECT_FALSE(summaries[1].isLastConnected);
}

TEST_F(WifiTest, FileCarriesObfuscationAndIntegrityFields) {
  ASSERT_TRUE(WIFI_STORE.addCredential("HomeNet", "hunter2!"));
  const std::string raw = readDeviceFile("/.crosspoint/wifi.json");
  EXPECT_EQ(raw.find("hunter2!"), std::string::npos) << raw;
  JsonDocument doc = parseDeviceFile("/.crosspoint/wifi.json");
  JsonVariantConst entry = doc["credentials"][0];
  EXPECT_STREQ(entry["ssid"] | "", "HomeNet");
  EXPECT_FALSE(entry["password_obf"].isNull());
  EXPECT_EQ(entry["password_len"] | 0u, 8u);
  EXPECT_EQ(entry["password_crc32"] | 0u, credential_integrity::crc32("hunter2!"));
}

TEST_F(WifiTest, CorruptedObfuscatedPasswordDiscardedByChecksum) {
  ASSERT_TRUE(WIFI_STORE.addCredential("HomeNet", "hunter2!"));
  ASSERT_TRUE(WIFI_STORE.addCredential("Other", "pass1234"));
  std::string raw = readDeviceFile("/.crosspoint/wifi.json");
  // Flip one character inside HomeNet's base64 payload: still decodable and
  // the same length, so only the CRC can catch it.
  const std::string good = obf("hunter2!");
  std::string bad = good;
  bad[0] = (bad[0] == 'A') ? 'B' : 'A';
  ASSERT_NE(raw.find(good), std::string::npos);
  raw.replace(raw.find(good), good.size(), bad);
  writeDeviceFile("/.crosspoint/wifi.json", raw);

  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 1u);
  EXPECT_FALSE(WIFI_STORE.findCredential("HomeNet").has_value());
  EXPECT_TRUE(WIFI_STORE.findCredential("Other").has_value());
  EXPECT_EQ(Storage.writeCount, 1);  // dropped entry -> file rewritten
  const std::string rewritten = readDeviceFile("/.crosspoint/wifi.json");
  EXPECT_EQ(rewritten.find("HomeNet"), std::string::npos);
}

TEST_F(WifiTest, LengthMismatchDiscardsCredential) {
  writeDeviceFile("/.crosspoint/wifi.json", "{\"credentials\":[{\"ssid\":\"N\",\"password_obf\":\"" + obf("hunter2!") +
                                                "\",\"password_len\":3,\"password_crc32\":" +
                                                std::to_string(credential_integrity::crc32("hunter2!")) + "}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 0u);
  EXPECT_EQ(Storage.writeCount, 1);
}

TEST_F(WifiTest, HostileLengthTypesDiscardCredential) {
  const std::string entryPrefix = "{\"credentials\":[{\"ssid\":\"N\",\"password_obf\":\"" + obf("pw") + "\",";
  for (const char* lenJson :
       {"\"password_len\":\"2\"", "\"password_len\":-1", "\"password_len\":2.5", "\"password_len\":100"}) {
    writeDeviceFile("/.crosspoint/wifi.json", entryPrefix + lenJson + "}]}");
    ASSERT_TRUE(WIFI_STORE.loadFromFile()) << lenJson;
    EXPECT_EQ(WIFI_STORE.getCredentialCount(), 0u) << lenJson;
  }
}

TEST_F(WifiTest, HostileChecksumTypeDiscardsCredential) {
  writeDeviceFile("/.crosspoint/wifi.json", "{\"credentials\":[{\"ssid\":\"N\",\"password_obf\":\"" + obf("pw") +
                                                "\",\"password_len\":2,\"password_crc32\":\"abc\"}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 0u);
}

TEST_F(WifiTest, MissingIntegrityFieldsAcceptedAndUpgraded) {
  // Files written before password_len/password_crc32 existed load fine and
  // get rewritten with the integrity fields.
  writeDeviceFile("/.crosspoint/wifi.json",
                  "{\"credentials\":[{\"ssid\":\"OldNet\",\"password_obf\":\"" + obf("oldpass") + "\"}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  ASSERT_EQ(WIFI_STORE.getCredentialCount(), 1u);
  EXPECT_EQ(WIFI_STORE.findCredential("OldNet")->password, "oldpass");
  EXPECT_EQ(Storage.writeCount, 1);
  JsonDocument doc = parseDeviceFile("/.crosspoint/wifi.json");
  EXPECT_EQ(doc["credentials"][0]["password_len"] | 0u, 7u);
  EXPECT_EQ(doc["credentials"][0]["password_crc32"] | 0u, credential_integrity::crc32("oldpass"));
}

TEST_F(WifiTest, LegacyPlaintextPasswordUpgraded) {
  writeDeviceFile("/.crosspoint/wifi.json", "{\"credentials\":[{\"ssid\":\"Legacy\",\"password\":\"plainPW\"}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  ASSERT_EQ(WIFI_STORE.getCredentialCount(), 1u);
  EXPECT_EQ(WIFI_STORE.findCredential("Legacy")->password, "plainPW");
  EXPECT_EQ(Storage.writeCount, 1);
  const std::string raw = readDeviceFile("/.crosspoint/wifi.json");
  EXPECT_EQ(raw.find("plainPW"), std::string::npos) << raw;
}

TEST_F(WifiTest, OversizedPasswordsDiscarded) {
  const std::string huge(65, 'p');  // MAX_PASSWORD_LENGTH is 64
  writeDeviceFile("/.crosspoint/wifi.json", "{\"credentials\":[{\"ssid\":\"Long\",\"password\":\"" + huge + "\"}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 0u);

  writeDeviceFile("/.crosspoint/wifi.json",
                  "{\"credentials\":[{\"ssid\":\"LongObf\",\"password_obf\":\"" + obf(huge) + "\"}]}");
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 0u);
}

TEST_F(WifiTest, NetworkLimitEnforcedOnAddAndLoad) {
  for (int i = 0; i < 8; i++) {
    ASSERT_TRUE(WIFI_STORE.addCredential("Net" + std::to_string(i), "pw"));
  }
  EXPECT_FALSE(WIFI_STORE.addCredential("overflow", "pw"));
  // Updating an existing network is allowed at the cap.
  EXPECT_TRUE(WIFI_STORE.addCredential("Net3", "newpw"));
  EXPECT_EQ(WIFI_STORE.findCredential("Net3")->password, "newpw");
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 8u);

  std::string json = "{\"credentials\":[";
  for (int i = 0; i < 10; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"L" + std::to_string(i) + "\",\"password\":\"\"}";
  }
  json += "]}";
  writeDeviceFile("/.crosspoint/wifi.json", json);
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  EXPECT_EQ(WIFI_STORE.getCredentialCount(), 8u);
}

TEST_F(WifiTest, RedundantWriteGuardsSkipSdWrites) {
  // AGENTS.md: "Guard redundant writes". setLastConnectedSsid/clearLastConnectedSsid
  // skip the SD write when nothing changed — pin it.
  ASSERT_TRUE(WIFI_STORE.addCredential("Net", "pw"));
  Storage.resetCounters();
  WIFI_STORE.setLastConnectedSsid("Net");
  EXPECT_EQ(Storage.writeCount, 1);
  WIFI_STORE.setLastConnectedSsid("Net");  // unchanged -> no write
  EXPECT_EQ(Storage.writeCount, 1);
  WIFI_STORE.clearLastConnectedSsid();
  EXPECT_EQ(Storage.writeCount, 2);
  WIFI_STORE.clearLastConnectedSsid();  // already empty -> no write
  EXPECT_EQ(Storage.writeCount, 2);
}

TEST_F(WifiTest, RemovingLastConnectedNetworkClearsIt) {
  ASSERT_TRUE(WIFI_STORE.addCredential("Net", "pw"));
  WIFI_STORE.setLastConnectedSsid("Net");
  ASSERT_TRUE(WIFI_STORE.removeCredential("Net"));
  EXPECT_EQ(WIFI_STORE.getLastConnectedSsid(), "");
  EXPECT_FALSE(WIFI_STORE.removeCredential("Net"));  // second removal is a no-op
}

TEST_F(WifiTest, EmptyPasswordCredentialSurvivesIntegrityChecks) {
  ASSERT_TRUE(WIFI_STORE.addCredential("Open", ""));
  Storage.resetCounters();
  ASSERT_TRUE(WIFI_STORE.loadFromFile());
  ASSERT_EQ(WIFI_STORE.getCredentialCount(), 1u);
  EXPECT_EQ(WIFI_STORE.findCredential("Open")->password, "");
  EXPECT_EQ(Storage.writeCount, 0);  // no resave needed
}

// ---------------------------------------------------------------------------
// KOReaderCredentialStore
// ---------------------------------------------------------------------------

class KoReaderTest : public StoreTest {
 protected:
  void SetUp() override {
    StoreTest::SetUp();
    // fromJson fully replaces state; feed a modern empty config as reset.
    writeDeviceFile("/.crosspoint/koreader.json",
                    "{\"cfgVersion\":2,\"username\":\"\",\"serverUrl\":\"\",\"matchMethod\":0,"
                    "\"sendMetadata\":false,\"syncBehavior\":1}");
    ASSERT_TRUE(KOREADER_STORE.loadFromFile());
    Storage.resetCounters();
  }
};

TEST_F(KoReaderTest, RoundTripAllFieldsWithObfuscatedPassword) {
  KOREADER_STORE.setCredentials("reader", "syncPW42");
  KOREADER_STORE.setServerUrl("https://sync.example.org");
  KOREADER_STORE.setMatchMethod(DocumentMatchMethod::BINARY);
  KOREADER_STORE.setSendMetadata(true);
  KOREADER_STORE.setSyncBehavior(KOReaderSyncBehavior::ASK_EVERY_TIME);
  ASSERT_TRUE(KOREADER_STORE.saveToFile());
  const std::string raw = readDeviceFile("/.crosspoint/koreader.json");
  EXPECT_EQ(raw.find("syncPW42"), std::string::npos) << raw;

  // Scramble memory, then reload from disk.
  KOREADER_STORE.setCredentials("", "");
  KOREADER_STORE.setServerUrl("");
  writeDeviceFile("/.crosspoint/koreader.json", raw);
  ASSERT_TRUE(KOREADER_STORE.loadFromFile());
  EXPECT_EQ(KOREADER_STORE.getUsername(), "reader");
  EXPECT_EQ(KOREADER_STORE.getPassword(), "syncPW42");
  EXPECT_EQ(KOREADER_STORE.getServerUrl(), "https://sync.example.org");
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::BINARY);
  EXPECT_TRUE(KOREADER_STORE.getSendMetadata());
  EXPECT_EQ(KOREADER_STORE.getSyncBehavior(), KOReaderSyncBehavior::ASK_EVERY_TIME);
}

TEST_F(KoReaderTest, PreV2ConfigWithCredentialsPinsLegacyServer) {
  writeDeviceFile("/.crosspoint/koreader.json", "{\"username\":\"u\",\"password_obf\":\"" + obf("pw") +
                                                    "\",\"serverUrl\":\"\",\"matchMethod\":0,\"sendMetadata\":false}");
  ASSERT_TRUE(KOREADER_STORE.loadFromFile());
  EXPECT_EQ(KOREADER_STORE.getServerUrl(), "https://sync.koreader.rocks:443");
  EXPECT_GE(Storage.writeCount, 1);  // cfgVersion stamp resave
  JsonDocument doc = parseDeviceFile("/.crosspoint/koreader.json");
  EXPECT_EQ(doc["cfgVersion"] | 0, 2);
}

TEST_F(KoReaderTest, PreV2ConfigWithoutCredentialsKeepsNewDefault) {
  writeDeviceFile("/.crosspoint/koreader.json", "{\"username\":\"\",\"serverUrl\":\"\"}");
  ASSERT_TRUE(KOREADER_STORE.loadFromFile());
  EXPECT_EQ(KOREADER_STORE.getServerUrl(), "");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "https://sync.crosspointreader.com");
}

TEST_F(KoReaderTest, InvalidEnumsResetToSafeValues) {
  writeDeviceFile("/.crosspoint/koreader.json",
                  "{\"cfgVersion\":2,\"matchMethod\":9,\"syncBehavior\":9,\"sendMetadata\":true}");
  ASSERT_TRUE(KOREADER_STORE.loadFromFile());
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::FILENAME);
  EXPECT_EQ(KOREADER_STORE.getSyncBehavior(), KOReaderSyncBehavior::ASK_EVERY_TIME);
  EXPECT_TRUE(KOREADER_STORE.getSendMetadata());
  EXPECT_GE(Storage.writeCount, 1);  // invalid syncBehavior forces a resave
}

TEST_F(KoReaderTest, MissingSyncBehaviorDefaultsToAskAndResaves) {
  writeDeviceFile("/.crosspoint/koreader.json", "{\"cfgVersion\":2,\"matchMethod\":1}");
  ASSERT_TRUE(KOREADER_STORE.loadFromFile());
  EXPECT_EQ(KOREADER_STORE.getSyncBehavior(), KOReaderSyncBehavior::ASK_EVERY_TIME);
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::BINARY);
  EXPECT_EQ(Storage.writeCount, 1);
}

// ---------------------------------------------------------------------------
// Obfuscation layer (production ObfuscationUtils with the deterministic
// test MAC)
// ---------------------------------------------------------------------------

using ObfuscationTest = StoreTest;

TEST_F(ObfuscationTest, RoundTripAndNonIdentity) {
  const std::string enc = obf("WiFi P@ssw0rd!");
  EXPECT_NE(enc, "WiFi P@ssw0rd!");
  bool ok = false;
  bool tooLong = false;
  const std::string dec = obfuscation::deobfuscateFromBase64(enc.c_str(), 64, &ok, &tooLong);
  EXPECT_TRUE(ok);
  EXPECT_FALSE(tooLong);
  EXPECT_EQ(dec, "WiFi P@ssw0rd!");
  EXPECT_EQ(obf(""), "");
}

TEST_F(ObfuscationTest, InvalidBase64ReportsFailure) {
  bool ok = true;
  const std::string dec = obfuscation::deobfuscateFromBase64("###", &ok);
  EXPECT_FALSE(ok);
  EXPECT_EQ(dec, "");
}

TEST_F(ObfuscationTest, OversizeDecodeReportsTooLong) {
  const std::string enc = obf("0123456789");
  bool ok = true;
  bool tooLong = false;
  const std::string dec = obfuscation::deobfuscateFromBase64(enc.c_str(), 5, &ok, &tooLong);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(tooLong);
  EXPECT_EQ(dec, "");
}

}  // namespace
