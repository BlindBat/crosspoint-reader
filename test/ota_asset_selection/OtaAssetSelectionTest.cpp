// OTA asset selection on top of ReleaseJsonParser (which has its own suite):
// the board-tag -> release-asset-name derivation in OtaUpdater::checkForUpdate,
// the version comparison gate (isUpdateNewer, incl. the RC->release rule from
// upstream #778), and the download-time guards (chip id + embedded board tag)
// in installUpdate. This binary is built as an X4PRO board; the combined
// X4/X3 ("x4") naming special case is covered by X4VariantTest.cpp.
//
// NOTE on asset naming: upstream develop switched the release assets to
// crosspoint-<tag>-<device>.bin ("fix: update OTA to recognize the new
// format", #3493 / 46d91253). This branch (v1.6 master lineage) still selects
// firmware.bin / firmware-<board>.bin — the tests below pin THIS branch's
// behavior and mark the divergence where it shows.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "FakeHttp.h"
#include "network/FirmwareBoardTag.h"
#include "network/OtaUpdater.h"
#include "stubs/esp_ota_ops.h"

namespace {

constexpr char kApiUrl[] = "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest";

class OtaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FakeHttp::instance().reset();
    EspOtaRecorder::instance().reset();
    g_testChipId = 0x0005;
    g_testCurrentVersion = "1.6.0";
  }

  // Run a checkForUpdate() against a release carrying this board's asset.
  static OtaUpdater::OtaUpdaterError checkWithTag(OtaUpdater& updater, const std::string& tag,
                                                  const size_t assetSize = 1000) {
    FakeHttp::instance().requestedUrls.clear();
    FakeHttp::instance().setBody(
        makeReleaseJson(tag, {{"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", assetSize}}));
    return updater.checkForUpdate();
  }

  // Minimal ESP image: 14+ header bytes with chip_id at offset 12, then a
  // payload that may embed a board tag.
  static std::string makeImage(const uint16_t chipId, const std::string& payload = "", const size_t padTo = 64) {
    std::string image(14, '.');
    std::memcpy(&image[12], &chipId, sizeof(chipId));
    image += payload;
    if (image.size() < padTo) image.append(padTo - image.size(), '.');
    return image;
  }

  static void feedString(board_tag::Scanner& scanner, const std::string& data, const size_t chunk = 64) {
    for (size_t off = 0; off < data.size(); off += chunk) {
      const size_t n = std::min(chunk, data.size() - off);
      scanner.feed(reinterpret_cast<const uint8_t*>(data.data()) + off, n);
    }
  }
};

// ---------------------------------------------------------------------------
// FirmwareBoardTag: name derivation + scanner
// ---------------------------------------------------------------------------

TEST_F(OtaTest, BoardTagDerivesFromDeviceFlag) {
  EXPECT_STREQ(board_tag::TAG, "CROSSPOINT-BOARD-V1:x4pro;");
  EXPECT_EQ(board_tag::boardNameLen(), 5u);
  EXPECT_EQ(std::string(board_tag::boardName(), board_tag::boardNameLen()), "x4pro");
}

TEST_F(OtaTest, ScannerAcceptsOwnTagAndUntaggedData) {
  board_tag::Scanner scanner;
  feedString(scanner, "......CROSSPOINT-BOARD-V1:x4pro;......");
  EXPECT_FALSE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "");

  board_tag::Scanner untagged;
  feedString(untagged, std::string(4096, 'Q') + "no tags here CROSSPOINT-BOARD but incomplete");
  EXPECT_FALSE(untagged.mismatch());
}

TEST_F(OtaTest, ScannerRejectsForeignTagAndReportsName) {
  board_tag::Scanner scanner;
  feedString(scanner, "binary CROSSPOINT-BOARD-V1:sticky; trailer");
  EXPECT_TRUE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "sticky");
}

TEST_F(OtaTest, ScannerRejectsPrefixAndExtensionOfOwnName) {
  // "x4" is a DIFFERENT compatibility class than "x4pro": both directions of
  // the prefix relation must mismatch.
  board_tag::Scanner prefix;
  feedString(prefix, "CROSSPOINT-BOARD-V1:x4;");
  EXPECT_TRUE(prefix.mismatch());
  EXPECT_STREQ(prefix.foundName(), "x4");

  board_tag::Scanner extended;
  feedString(extended, "CROSSPOINT-BOARD-V1:x4pro2;");
  EXPECT_TRUE(extended.mismatch());
  EXPECT_STREQ(extended.foundName(), "x4pro2");
}

TEST_F(OtaTest, ScannerRejectsSameLengthForeignName) {
  // A name of exactly boardNameLen() bytes with different content must be
  // rejected by the byte comparison, not slip through the length check —
  // whether the difference sits in the last byte or the first.
  ASSERT_EQ(sizeof("x4prq") - 1, board_tag::boardNameLen());
  board_tag::Scanner lastByte;
  feedString(lastByte, "CROSSPOINT-BOARD-V1:x4prq;");
  EXPECT_TRUE(lastByte.mismatch());
  EXPECT_STREQ(lastByte.foundName(), "x4prq");

  board_tag::Scanner firstByte;
  feedString(firstByte, "CROSSPOINT-BOARD-V1:y4pro;");
  EXPECT_TRUE(firstByte.mismatch());
  EXPECT_STREQ(firstByte.foundName(), "y4pro");
}

TEST_F(OtaTest, ScannerHandlesTagSplitAcrossEveryChunkBoundary) {
  const std::string data = "xxCROSSPOINT-BOARD-V1:papermono;yy";
  for (size_t chunk = 1; chunk <= data.size(); chunk++) {
    board_tag::Scanner scanner;
    feedString(scanner, data, chunk);
    EXPECT_TRUE(scanner.mismatch()) << "chunk size " << chunk;
    EXPECT_STREQ(scanner.foundName(), "papermono") << "chunk size " << chunk;
  }
}

TEST_F(OtaTest, ScannerLatchesFirstMismatch) {
  board_tag::Scanner scanner;
  feedString(scanner, "CROSSPOINT-BOARD-V1:sticky;");
  ASSERT_TRUE(scanner.mismatch());
  // A later matching tag cannot clear the verdict.
  feedString(scanner, "CROSSPOINT-BOARD-V1:x4pro;");
  EXPECT_TRUE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "sticky");
}

TEST_F(OtaTest, ScannerTreatsOverlongCaptureAsChanceCollision) {
  // 24+ printable characters after the magic exceed MAX_NAME: not a real tag.
  board_tag::Scanner scanner;
  feedString(scanner, "CROSSPOINT-BOARD-V1:" + std::string(30, 'z') + ";");
  EXPECT_FALSE(scanner.mismatch());
}

TEST_F(OtaTest, ScannerTreatsNonPrintableCaptureAsChanceCollision) {
  board_tag::Scanner scanner;
  const std::string data = std::string("CROSSPOINT-BOARD-V1:st") + '\x01' + "icky;";
  feedString(scanner, data);
  EXPECT_FALSE(scanner.mismatch());
}

TEST_F(OtaTest, ScannerRestartsMagicMatchOnRepeatedFirstByte) {
  // The magic's first byte recurring right before the real magic must not
  // desync the matcher (single-byte lookback).
  board_tag::Scanner scanner;
  feedString(scanner, "CCROSSPOINT-BOARD-V1:sticky;");
  EXPECT_TRUE(scanner.mismatch());
  EXPECT_STREQ(scanner.foundName(), "sticky");
}

// ---------------------------------------------------------------------------
// checkForUpdate: asset selection for this board
// ---------------------------------------------------------------------------

TEST_F(OtaTest, ChecksGitHubLatestReleaseEndpoint) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  ASSERT_EQ(FakeHttp::instance().requestedUrls.size(), 1u);
  EXPECT_EQ(FakeHttp::instance().requestedUrls[0], kApiUrl);
}

TEST_F(OtaTest, SelectsThisBoardsAssetAmongMany) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {
                                   {"firmware.bin", "https://cdn.example/c3.bin", 1111},
                                   {"firmware-sticky.bin", "https://cdn.example/sticky.bin", 3333},
                                   {"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 2222},
                                   {"firmware-papermono.bin", "https://cdn.example/pm.bin", 4444},
                               }));
  ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_EQ(updater.getLatestVersion(), "1.7.0");
  EXPECT_EQ(updater.getOtaSize(), 2222u);  // proves firmware-x4pro.bin won
  EXPECT_EQ(updater.getTotalSize(), 2222u);
}

TEST_F(OtaTest, GenericC3AssetIsNotAFallbackForX4pro) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(makeReleaseJson("1.7.0", {{"firmware.bin", "https://cdn.example/c3.bin", 1111}}));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::NO_UPDATE);
}

TEST_F(OtaTest, NewCrosspointAssetNamingNotRecognizedOnThisBranch) {
  // Upstream develop (#3493) renamed assets to crosspoint-<tag>-<device>.bin
  // and taught OtaUpdater to build that name from the parsed tag. This branch
  // predates the fix, so a new-format release yields NO_UPDATE — fixed
  // upstream; pin the divergence.
  OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"crosspoint-1.7.0-x4pro.bin", "https://cdn.example/new-x4pro.bin", 2222},
                                {"crosspoint-1.7.0-x3-x4.bin", "https://cdn.example/new-c3.bin", 1111}}));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::NO_UPDATE);
}

TEST_F(OtaTest, AssetNameMatchIsCaseSensitive) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"Firmware-X4pro.bin", "https://cdn.example/case.bin", 2222}}));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::NO_UPDATE);
}

TEST_F(OtaTest, DuplicateAssetNamesLastOneWins) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"firmware-x4pro.bin", "https://cdn.example/first.bin", 100},
                                {"firmware-x4pro.bin", "https://cdn.example/second.bin", 200}}));
  ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  EXPECT_EQ(updater.getOtaSize(), 200u);  // commitAsset overwrote the earlier match
}

TEST_F(OtaTest, MissingTagNameIsParseError) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(
      makeReleaseJson("unused", {{"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 2222}}, /*includeTag=*/false));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::JSON_PARSE_ERROR);
}

TEST_F(OtaTest, EmptyAssetsIsNoUpdate) {
  OtaUpdater updater;
  FakeHttp::instance().setBody(makeReleaseJson("1.7.0", {}));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::NO_UPDATE);
}

TEST_F(OtaTest, FetchFailureIsHttpError) {
  OtaUpdater updater;
  FakeHttp::instance().succeed = false;
  FakeHttp::instance().setBody(
      makeReleaseJson("1.7.0", {{"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 2222}}));
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::HTTP_ERROR);
}

TEST_F(OtaTest, SelectionIsChunkSizeInvariant) {
  const std::string json = makeReleaseJson("1.7.0", {{"firmware.bin", "https://cdn.example/c3.bin", 1111},
                                                     {"firmware-x4pro.bin", "https://cdn.example/x4pro.bin", 2222}});
  for (const size_t chunk : {size_t{1}, size_t{3}, size_t{17}, json.size()}) {
    OtaUpdater updater;
    FakeHttp::instance().reset();
    FakeHttp::instance().chunkSize = chunk;
    FakeHttp::instance().setBody(json);
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK) << "chunk " << chunk;
    EXPECT_EQ(updater.getOtaSize(), 2222u) << "chunk " << chunk;
  }
}

// ---------------------------------------------------------------------------
// isUpdateNewer: semantic version gate incl. the RC rules (#778)
// ---------------------------------------------------------------------------

TEST_F(OtaTest, NewerVersionsDetectedPerSegment) {
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0";
  ASSERT_EQ(checkWithTag(updater, "1.6.1"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "2.0.0"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  // Higher patch on a lower minor does not win.
  ASSERT_EQ(checkWithTag(updater, "1.5.9"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "0.9.9"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "1.6.0"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
}

TEST_F(OtaTest, RcBuildUpgradesToSameVersionRelease) {
  // Upstream #778: an RC build must accept the equal-numbered release.
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0-rc+abc123";
  ASSERT_EQ(checkWithTag(updater, "1.6.0"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
}

TEST_F(OtaTest, RcBuildTreatsAnyDifferentRcOfSameVersionAsNewer) {
  // The RC rule only checks that the CURRENT build is an RC and the strings
  // differ — it cannot order rc hashes. Pins that a different rc of the same
  // base version always counts as newer.
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0-rc+abc123";
  ASSERT_EQ(checkWithTag(updater, "1.6.0-rc+zzz999"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
}

TEST_F(OtaTest, RcBuildIgnoresIdenticalVersionString) {
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0-rc+abc123";
  ASSERT_EQ(checkWithTag(updater, "1.6.0-rc+abc123"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
}

TEST_F(OtaTest, ReleaseBuildDoesNotDowngradeToRc) {
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0";
  ASSERT_EQ(checkWithTag(updater, "1.6.0-rc+abc123"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
}

TEST_F(OtaTest, NotNewerBeforeAnySuccessfulCheck) {
  OtaUpdater updater;
  EXPECT_FALSE(updater.isUpdateNewer());
}

TEST_F(OtaTest, VPrefixedTagComparesNumerically) {
  // Regression guard: isUpdateNewer() used to ignore sscanf's return value,
  // so GitHub's common "vX.Y.Z" tag form left the parsed segments
  // uninitialized and compared garbage. A single leading 'v'/'V' is now
  // stripped before the semver comparison.
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0";
  ASSERT_EQ(checkWithTag(updater, "v1.7.0"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "V1.7.0"), OtaUpdater::OK);
  EXPECT_TRUE(updater.isUpdateNewer());
  // Same version behind a 'v' prefix is not an update.
  ASSERT_EQ(checkWithTag(updater, "v1.6.0"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
  // Nor is a v-prefixed older release.
  ASSERT_EQ(checkWithTag(updater, "v1.5.9"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
}

TEST_F(OtaTest, NonSemverTagIsNeverNewer) {
  // A tag that does not parse as MAJOR.MINOR.PATCH must fail safe: no update
  // offered, no garbage comparison.
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0";
  ASSERT_EQ(checkWithTag(updater, "nightly"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "release-2"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "1.7"), OtaUpdater::OK);  // only two segments
  EXPECT_FALSE(updater.isUpdateNewer());
  ASSERT_EQ(checkWithTag(updater, "v"), OtaUpdater::OK);
  EXPECT_FALSE(updater.isUpdateNewer());
}

// ---------------------------------------------------------------------------
// installUpdate: streaming guards
// ---------------------------------------------------------------------------

TEST_F(OtaTest, InstallStreamsSelectedAssetIntoOtaPartition) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  const std::string image = makeImage(0x0005, "payload CROSSPOINT-BOARD-V1:x4pro; more", 256);
  FakeHttp::instance().setBody(image);
  FakeHttp::instance().requestedUrls.clear();

  ASSERT_EQ(updater.installUpdate(), OtaUpdater::OK);
  ASSERT_EQ(FakeHttp::instance().requestedUrls.size(), 1u);
  EXPECT_EQ(FakeHttp::instance().requestedUrls[0], "https://cdn.example/x4pro.bin");
  const auto& rec = EspOtaRecorder::instance();
  EXPECT_EQ(rec.written.size(), image.size());
  EXPECT_EQ(std::string(rec.written.begin(), rec.written.end()), image);
  EXPECT_EQ(rec.endCalls, 1);
  EXPECT_EQ(rec.setBootCalls, 1);
  EXPECT_EQ(rec.abortCalls, 0);
  EXPECT_EQ(updater.getProcessedSize(), image.size());
}

TEST_F(OtaTest, UntaggedImageIsAccepted) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  FakeHttp::instance().setBody(makeImage(0x0005, "no board tag inside", 256));
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::OK);
  EXPECT_EQ(EspOtaRecorder::instance().setBootCalls, 1);
}

TEST_F(OtaTest, WrongChipIdAbortsBeforeCommit) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  g_testChipId = 0x0005;
  FakeHttp::instance().setBody(makeImage(0x0009, "", 256));  // ESP32-S3 image on a C3
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::WRONG_DEVICE_ERROR);
  const auto& rec = EspOtaRecorder::instance();
  EXPECT_EQ(rec.abortCalls, 1);
  EXPECT_EQ(rec.setBootCalls, 0);
  EXPECT_EQ(rec.endCalls, 0);
}

TEST_F(OtaTest, UnknownRunningChipSkipsChipCheck) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  g_testChipId = 0xFFFF;  // running chip id unreadable
  FakeHttp::instance().setBody(makeImage(0x0009, "", 256));
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::OK);
}

TEST_F(OtaTest, ForeignBoardTagInImageAborts) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  FakeHttp::instance().setBody(makeImage(0x0005, "CROSSPOINT-BOARD-V1:sticky;", 256));
  FakeHttp::instance().chunkSize = 3;  // tag split across chunks must still be caught
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::WRONG_DEVICE_ERROR);
  EXPECT_EQ(EspOtaRecorder::instance().abortCalls, 1);
  EXPECT_EQ(EspOtaRecorder::instance().setBootCalls, 0);
}

TEST_F(OtaTest, DownloadFailureAbortsWithHttpError) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  FakeHttp::instance().succeed = false;
  FakeHttp::instance().setBody(makeImage(0x0005, "", 256));
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::HTTP_ERROR);
  EXPECT_EQ(EspOtaRecorder::instance().abortCalls, 1);
  EXPECT_EQ(EspOtaRecorder::instance().setBootCalls, 0);
}

TEST_F(OtaTest, FlashWriteFailureAbortsWithInternalError) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  EspOtaRecorder::instance().writeResult = ESP_FAIL;
  FakeHttp::instance().setBody(makeImage(0x0005, "", 256));
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_EQ(EspOtaRecorder::instance().abortCalls, 1);
}

TEST_F(OtaTest, NoOtaPartitionFailsBeforeDownload) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0"), OtaUpdater::OK);
  EspOtaRecorder::instance().havePartition = false;
  FakeHttp::instance().requestedUrls.clear();
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::INTERNAL_UPDATE_ERROR);
  EXPECT_TRUE(FakeHttp::instance().requestedUrls.empty());
}

TEST_F(OtaTest, OlderReleaseRefusesInstall) {
  OtaUpdater updater;
  g_testCurrentVersion = "1.6.0";
  ASSERT_EQ(checkWithTag(updater, "1.5.0"), OtaUpdater::OK);
  FakeHttp::instance().requestedUrls.clear();
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::UPDATE_OLDER_ERROR);
  EXPECT_TRUE(FakeHttp::instance().requestedUrls.empty());  // no download started
}

TEST_F(OtaTest, ProgressCallbackFiresOncePerWholePercent) {
  OtaUpdater updater;
  ASSERT_EQ(checkWithTag(updater, "1.7.0", /*assetSize=*/200), OtaUpdater::OK);
  FakeHttp::instance().setBody(makeImage(0x0005, "", 200));  // exactly totalSize bytes
  FakeHttp::instance().chunkSize = 2;                        // 100 chunks -> 1%..100%
  int calls = 0;
  ASSERT_EQ(updater.installUpdate([](void* ctx) { (*static_cast<int*>(ctx))++; }, &calls), OtaUpdater::OK);
  EXPECT_EQ(calls, 100);
}

}  // namespace
