// Corpus-driven robustness tests: every file in test/corpus/json/ is fed
// through ReleaseJsonParser. Files prefixed release_ are GitHub-release-shaped
// with a specific defect; everything else is generic malformed JSON that must
// never look like a valid release. Regenerate the corpus with
// scripts/generate_test_corpus.py.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/ReleaseJsonParser.h"
#include "test/corpus/CorpusTestSupport.h"

namespace {

const std::string kCorpusDir = CORPUS_JSON_DIR;

constexpr const char* kCanonicalRelease =
    R"({"tag_name":"v2.0.0","assets":[{"name":"firmware.bin",)"
    R"("browser_download_url":"https://example.invalid/canonical.bin","size":4096}]})";

class ReleaseJsonParserCorpusTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string input = corpus::readCorpusFile(kCorpusDir, GetParam());
  ReleaseJsonParser parser;

  void feedAll() { parser.feed(input.data(), input.size()); }

  bool isReleaseShaped() const { return GetParam().rfind("release_", 0) == 0; }
};

// Invariants that must hold for ANY input, including corpus files added later.
TEST_P(ReleaseJsonParserCorpusTest, SurvivesArbitraryBytesWithConsistentOutputs) {
  feedAll();

  // Output strings must stay NUL-terminated inside their fixed buffers.
  EXPECT_LT(strlen(parser.getTagName()), 32u);
  EXPECT_LT(strlen(parser.getFirmwareUrl()), 512u);

  // Not-found results must be empty, never stale garbage.
  if (!parser.foundTag()) {
    EXPECT_STREQ(parser.getTagName(), "");
  }
  if (!parser.foundFirmware()) {
    EXPECT_STREQ(parser.getFirmwareUrl(), "");
    EXPECT_EQ(parser.getFirmwareSize(), 0u);
  }

  // reset() must fully recover the parser no matter what the corpus file did.
  parser.reset();
  parser.feed(kCanonicalRelease, strlen(kCanonicalRelease));
  EXPECT_TRUE(parser.foundTag());
  EXPECT_STREQ(parser.getTagName(), "v2.0.0");
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_STREQ(parser.getFirmwareUrl(), "https://example.invalid/canonical.bin");
  EXPECT_EQ(parser.getFirmwareSize(), 4096u);
}

// No generic malformed-JSON corpus file contains a top-level "tag_name" or a
// matching firmware asset, so none may ever report a found release.
TEST_P(ReleaseJsonParserCorpusTest, GenericMalformedJsonNeverLooksLikeARelease) {
  if (isReleaseShaped()) {
    GTEST_SKIP() << "release-shaped files are pinned individually below";
  }
  feedAll();
  EXPECT_FALSE(parser.foundTag());
  EXPECT_FALSE(parser.foundFirmware());
}

INSTANTIATE_TEST_SUITE_P(JsonCorpus, ReleaseJsonParserCorpusTest,
                         ::testing::ValuesIn(corpus::listCorpusFiles(kCorpusDir)), corpus::NameFromFilename{});

// -- Behavior pins for the release-shaped corpus files ----------------------

class ReleaseCorpusBehavior : public ::testing::Test {
 protected:
  ReleaseJsonParser parser;

  void feedFile(const std::string& name) {
    const std::string input = corpus::readCorpusFile(kCorpusDir, name);
    ASSERT_FALSE(input.empty()) << "corpus file missing: " << name;
    parser.feed(input.data(), input.size());
  }
};

TEST_F(ReleaseCorpusBehavior, TruncationInsideAssetKeepsTagButNeverCommitsTheAsset) {
  feedFile("release_trunc_mid_assets.json");
  EXPECT_TRUE(parser.foundTag());
  EXPECT_STREQ(parser.getTagName(), "v9.9.9");
  // The asset object never closed, so its data must not leak into the result.
  EXPECT_FALSE(parser.foundFirmware());
  EXPECT_STREQ(parser.getFirmwareUrl(), "");
  EXPECT_EQ(parser.getFirmwareSize(), 0u);
}

// An asset size too large for the device's 32-bit size_t (here 26 digits)
// invalidates the whole asset instead of saturating to a fake ~4GB size:
// release metadata that broken must never select a firmware image.
TEST_F(ReleaseCorpusBehavior, HugeAssetSizeRejectsTheAsset) {
  feedFile("release_huge_asset_size.json");
  EXPECT_TRUE(parser.foundTag());
  EXPECT_FALSE(parser.foundFirmware());
  EXPECT_STREQ(parser.getFirmwareUrl(), "");
  EXPECT_EQ(parser.getFirmwareSize(), 0u);
}

TEST_F(ReleaseCorpusBehavior, WrongValueTypesAreIgnoredWithoutFalsePositives) {
  feedFile("release_wrong_types.json");
  EXPECT_FALSE(parser.foundTag());  // "tag_name":123 — a number is not a tag
  EXPECT_FALSE(parser.foundFirmware());
}

}  // namespace
