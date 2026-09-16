// Settings persistence on a touch board with a home key and an IMU.
//
// test/persistable_stores compiles the same production sources for a plain
// button board; the two binaries differ only in the CROSSPOINT_TEST_BOARD_*
// macros the BoardConfig/HalTiltSensor stubs read, so every board-gated entry
// in SettingsList.h is pinned from both sides. Entries pruned there must be
// present here, and vice versa.

#include <AllocCounter.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <I18n.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingsList.h"

namespace {

std::string processStoreRoot() { return "board_variant_root_" + std::to_string(::getpid()); }

void removeTree(const std::string& path) {
  const std::string cmd = "rm -rf '" + path + "'";
  [[maybe_unused]] const int rc = std::system(cmd.c_str());
}

class StoreRootCleanup : public ::testing::Environment {
 public:
  void TearDown() override { removeTree(processStoreRoot()); }
};

const ::testing::Environment* const storeRootCleanup = ::testing::AddGlobalTestEnvironment(new StoreRootCleanup);

class TouchBoardSettingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.root = processStoreRoot();
    removeTree(Storage.root);
    ::mkdir(Storage.root.c_str(), 0755);
    Storage.resetCounters();
  }

  static void writeDeviceFile(const char* path, const std::string& content) {
    Storage.mkdir("/.crosspoint");
    ASSERT_TRUE(Storage.writeFile(path, String(content)));
    Storage.resetCounters();
  }

  static JsonDocument parseSettingsFile() {
    JsonDocument doc;
    const std::string content = Storage.readFile("/.crosspoint/settings.json").str();
    const auto err = deserializeJson(doc, content);
    EXPECT_EQ(err, DeserializationError::Ok) << "unparseable settings.json: " << content;
    return doc;
  }

  static bool listHasKey(const char* key) {
    for (const auto& info : getSettingsList()) {
      if (info.key != nullptr && std::string(info.key) == key) return true;
    }
    return false;
  }
};

// --- credentials must never leave through the settings API -------------------

TEST_F(TouchBoardSettingsTest, CredentialSettingsAreMarkedSecret) {
  // GET /api/settings serialises a secret string as an empty value plus a
  // hasPassword flag (CrossPointWebServer, SettingType::STRING). The rule is
  // keyed off this flag, so every credential-bearing entry must carry it.
  bool sawKoPassword = false;
  for (const auto& info : getSettingsList()) {
    if (info.key == nullptr) continue;
    const std::string key = info.key;
    if (key == "koPassword") {
      sawKoPassword = true;
      EXPECT_TRUE(info.secret) << "koPassword would be sent in full over /api/settings";
    }
    if (key.find("assword") != std::string::npos) {
      EXPECT_TRUE(info.secret) << key << " looks like a credential but is not marked secret";
    }
  }
  EXPECT_TRUE(sawKoPassword) << "koPassword entry missing from the settings list";
}

TEST_F(TouchBoardSettingsTest, NonSecretStringSettingsStayReadable) {
  // The masking must not spill onto ordinary strings: the sync server URL is
  // shown in the web UI and is not a credential.
  for (const auto& info : getSettingsList()) {
    if (info.key != nullptr && std::string(info.key) == "koServerUrl") {
      EXPECT_FALSE(info.secret);
    }
  }
}

// --- the board class this binary models ------------------------------------

TEST_F(TouchBoardSettingsTest, BoardStubReportsTouchHomeKeyAndImu) {
  EXPECT_TRUE(BoardConfig::hasTouch());
  EXPECT_TRUE(BoardConfig::hasHomeKey());
  EXPECT_TRUE(halTiltSensor.isAvailable());
}

// --- entries the button board prunes, present here --------------------------

TEST_F(TouchBoardSettingsTest, TouchOnlyEntriesAreInTheList) {
  EXPECT_TRUE(listHasKey("touchReaderControls"));
  EXPECT_TRUE(listHasKey("readerMenuStyle"));
  EXPECT_TRUE(listHasKey("tapForReaderMenu"));  // legacy key of showReaderMenu
}

TEST_F(TouchBoardSettingsTest, TouchOnlyKeysArePersisted) {
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_INVERTED_TAP;
  SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_SWIPE_UP;
  ASSERT_TRUE(SETTINGS.saveToFile());

  JsonDocument doc = parseSettingsFile();
  EXPECT_EQ(doc["touchReaderControls"] | -1, CrossPointSettings::TOUCH_READER_INVERTED_TAP);
  EXPECT_EQ(doc["readerMenuStyle"] | -1, CrossPointSettings::READER_MENU_TOOLBAR);
  EXPECT_EQ(doc["tapForReaderMenu"] | -1, CrossPointSettings::READER_MENU_SWIPE_UP);
}

TEST_F(TouchBoardSettingsTest, TouchOnlyKeysAreLoaded) {
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_SWIPE;
  SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_LIST;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_TAP;
  writeDeviceFile("/.crosspoint/settings.json",
                  "{\"touchReaderControls\":0,\"readerMenuStyle\":1,\"tapForReaderMenu\":2}");
  ASSERT_TRUE(SETTINGS.loadFromFile());

  EXPECT_EQ(SETTINGS.touchReaderControls, CrossPointSettings::TOUCH_READER_OFF);
  EXPECT_EQ(SETTINGS.readerMenuStyle, CrossPointSettings::READER_MENU_TOOLBAR);
  EXPECT_EQ(SETTINGS.showReaderMenu, CrossPointSettings::READER_MENU_SWIPE_UP);
}

TEST_F(TouchBoardSettingsTest, TiltPageTurnEntryExistsWhenImuPresent) {
  EXPECT_TRUE(listHasKey("tiltPageTurn"));
  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_NORMAL;
  ASSERT_TRUE(SETTINGS.saveToFile());
  EXPECT_EQ(parseSettingsFile()["tiltPageTurn"] | -1, CrossPointSettings::TILT_NORMAL);
}

// --- entries the touch board prunes instead ---------------------------------

TEST_F(TouchBoardSettingsTest, ButtonOnlyEntriesArePrunedHere) {
  EXPECT_FALSE(listHasKey("frontButtonFollowOrientation"));
  EXPECT_FALSE(listHasKey("fadingFix"));
  EXPECT_FALSE(listHasKey("backShortToFileBrowser"));
}

TEST_F(TouchBoardSettingsTest, ButtonOnlyKeysAreNotPersisted) {
  SETTINGS.frontButtonFollowOrientation = 1;
  SETTINGS.fadingFix = 1;
  SETTINGS.backShortToFileBrowser = 1;
  ASSERT_TRUE(SETTINGS.saveToFile());

  JsonDocument doc = parseSettingsFile();
  EXPECT_TRUE(doc["frontButtonFollowOrientation"].isNull());
  EXPECT_TRUE(doc["fadingFix"].isNull());
  EXPECT_TRUE(doc["backShortToFileBrowser"].isNull());
}

TEST_F(TouchBoardSettingsTest, ButtonOnlyValueInFileIsIgnoredOnLoad) {
  SETTINGS.backShortToFileBrowser = 0;
  writeDeviceFile("/.crosspoint/settings.json", "{\"backShortToFileBrowser\":1}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.backShortToFileBrowser, 0);
}

// --- enum option lists that differ by board ---------------------------------

TEST_F(TouchBoardSettingsTest, ShortPwrBtnAcceptsConfirmOnTouchBoard) {
  // The button board lists five options and rejects index 5; FREEINK_CAP_TOUCH
  // adds Confirm, so the same file loads differently here.
  SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
  writeDeviceFile("/.crosspoint/settings.json", "{\"shortPwrBtn\":5}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.shortPwrBtn, CrossPointSettings::PWR_CONFIRM);
}

TEST_F(TouchBoardSettingsTest, ShortPwrBtnStillRejectsOutOfRange) {
  SETTINGS.shortPwrBtn = CrossPointSettings::FOOTNOTES;
  writeDeviceFile("/.crosspoint/settings.json", "{\"shortPwrBtn\":6}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.shortPwrBtn, CrossPointSettings::FOOTNOTES);
}

TEST_F(TouchBoardSettingsTest, LongPressMenuAcceptsReaderMenuWithHomeKey) {
  // buildLongPressMenuValues() keeps LP_MENU_READER_MENU (4) only where a home
  // key exists; the button board binary pins the rejection.
  SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_DISABLED;
  writeDeviceFile("/.crosspoint/settings.json", "{\"longPressMenuFunction\":4}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_READER_MENU);
}

TEST_F(TouchBoardSettingsTest, SaveLoadRoundTripKeepsBoardGatedValues) {
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_ON;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_OFF;
  SETTINGS.shortPwrBtn = CrossPointSettings::PWR_CONFIRM;
  SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_READER_MENU;
  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_NVERTED;
  ASSERT_TRUE(SETTINGS.saveToFile());

  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_SWIPE;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_TAP;
  SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
  SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_DISABLED;
  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_OFF;
  ASSERT_TRUE(SETTINGS.loadFromFile());

  EXPECT_EQ(SETTINGS.touchReaderControls, CrossPointSettings::TOUCH_READER_ON);
  EXPECT_EQ(SETTINGS.showReaderMenu, CrossPointSettings::READER_MENU_OFF);
  EXPECT_EQ(SETTINGS.shortPwrBtn, CrossPointSettings::PWR_CONFIRM);
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_READER_MENU);
  EXPECT_EQ(SETTINGS.tiltPageTurn, CrossPointSettings::TILT_NVERTED);
}

// --- the list is shared, not rebuilt, on every call --------------------------

TEST_F(TouchBoardSettingsTest, RepeatedCallsDoNotRebuildTheList) {
  // CrossPointSettings::toJson()/fromJson() and every web settings request walk
  // this list; rebuilding it per call copied ~60 SettingInfo (two vectors and
  // four std::function each) every time.
  {
    const auto& warm = getSettingsList();  // first call builds it
    ASSERT_FALSE(warm.empty());
  }

  size_t allocations = 0;
  size_t entries = 0;
  {
    alloc_counter::CountingScope scope;
    for (int i = 0; i < 8; i++) {
      const auto& list = getSettingsList();
      entries = list.size();
    }
    allocations = scope.count();
  }
  EXPECT_GT(entries, 0u);
  EXPECT_EQ(allocations, 0u) << "getSettingsList() allocated " << allocations << " times across 8 calls";
}

TEST_F(TouchBoardSettingsTest, EveryCallReturnsTheSameEntriesInTheSameOrder) {
  // The board pruning above is what makes the list board-specific; a shared
  // instance must still present exactly the entries a fresh build would.
  std::vector<std::string> firstKeys;
  std::vector<int> firstNames;
  for (const auto& info : getSettingsList()) {
    firstKeys.emplace_back(info.key != nullptr ? info.key : "");
    firstNames.push_back(static_cast<int>(info.nameId));
  }

  std::vector<std::string> secondKeys;
  std::vector<int> secondNames;
  for (const auto& info : getSettingsList()) {
    secondKeys.emplace_back(info.key != nullptr ? info.key : "");
    secondNames.push_back(static_cast<int>(info.nameId));
  }

  EXPECT_FALSE(firstKeys.empty());
  EXPECT_EQ(firstKeys, secondKeys);
  EXPECT_EQ(firstNames, secondNames);
}

TEST_F(TouchBoardSettingsTest, RegistrylessOverloadMatchesTheSharedList) {
  // getSettingsList(registry) must present exactly what the shared no-arg list
  // does when there is no registry to fold in: the two used to be one function,
  // and the settings screen reaches the list only through the overload.
  std::vector<std::string> sharedKeys;
  std::vector<int> sharedNames;
  for (const auto& info : getSettingsList()) {
    sharedKeys.emplace_back(info.key != nullptr ? info.key : "");
    sharedNames.push_back(static_cast<int>(info.nameId));
  }

  std::vector<std::string> overloadKeys;
  std::vector<int> overloadNames;
  for (const auto& info : getSettingsList(nullptr)) {
    overloadKeys.emplace_back(info.key != nullptr ? info.key : "");
    overloadNames.push_back(static_cast<int>(info.nameId));
  }

  EXPECT_FALSE(sharedKeys.empty());
  EXPECT_EQ(sharedKeys, overloadKeys);
  EXPECT_EQ(sharedNames, overloadNames);
}

TEST_F(TouchBoardSettingsTest, SharedFontSizeEntryStaysLiveAndFamilyIndependent) {
  // The font-size entry is built once now and handed out with the shared list,
  // so it must hold nothing that can go stale: its options come from the
  // built-in point sizes (readerFontPointSizes ignores the SD family name when
  // there is no registry) and its accessors must read SETTINGS on every call.
  auto fontSizeEntry = []() -> SettingInfo {
    for (const auto& info : getSettingsList()) {
      if (info.key != nullptr && std::string(info.key) == "fontSize") return info;
    }
    return SettingInfo{};
  };

  const SettingInfo entry = fontSizeEntry();
  ASSERT_GT(entry.enumStringValues.size(), 1u) << "font-size entry missing or left as the empty placeholder";
  ASSERT_TRUE(static_cast<bool>(entry.valueGetter));
  ASSERT_TRUE(static_cast<bool>(entry.valueSetter));
  const std::vector<std::string> labels = entry.enumStringValues;
  const uint8_t originalPt = SETTINGS.fontPointSize;

  // A write through one copy of the entry is visible through the next one:
  // the accessors go to SETTINGS, not to a value frozen at build time.
  const uint8_t lastIndex = static_cast<uint8_t>(labels.size() - 1);
  entry.valueSetter(0);
  EXPECT_EQ(fontSizeEntry().valueGetter(), 0);
  const uint8_t smallestPt = SETTINGS.fontPointSize;
  entry.valueSetter(lastIndex);
  EXPECT_EQ(fontSizeEntry().valueGetter(), lastIndex);
  EXPECT_GT(SETTINGS.fontPointSize, smallestPt);

  // Selecting an SD family must not change what the shared list offers — if it
  // ever did, caching the entry would serve stale sizes.
  std::strncpy(SETTINGS.sdFontFamilyName, "SomeSdFamily", sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  EXPECT_EQ(fontSizeEntry().enumStringValues, labels);
  SETTINGS.sdFontFamilyName[0] = '\0';
  SETTINGS.fontPointSize = originalPt;
}

TEST_F(TouchBoardSettingsTest, FileFromButtonBoardLeavesTouchValuesUntouched) {
  // A card moved from a button board carries no touch keys: the in-memory
  // values stand in as defaults (CrossPointSettings::fromJson semantics).
  SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_INVERTED_TAP;
  SETTINGS.showReaderMenu = CrossPointSettings::READER_MENU_SWIPE_UP;
  writeDeviceFile("/.crosspoint/settings.json", "{\"fontSize\":3}");
  ASSERT_TRUE(SETTINGS.loadFromFile());
  EXPECT_EQ(SETTINGS.touchReaderControls, CrossPointSettings::TOUCH_READER_INVERTED_TAP);
  EXPECT_EQ(SETTINGS.showReaderMenu, CrossPointSettings::READER_MENU_SWIPE_UP);
}

}  // namespace
