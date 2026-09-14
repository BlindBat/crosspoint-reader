// Corpus-driven robustness tests: every file in test/corpus/css/ is loaded
// through CssParser::loadFromStream. Universal invariants hold for any input;
// a pinned table records the ParseResult and rule count the parser currently
// produces for each committed file. Regenerate the corpus with
// scripts/generate_test_corpus.py.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "CssParser.h"
#include "test/corpus/CorpusTestSupport.h"

namespace fs = std::filesystem;

namespace {

const std::string kCorpusDir = CORPUS_CSS_DIR;

class CssCorpusTest : public ::testing::TestWithParam<std::string> {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_css_corpus_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
  }

  void TearDown() override { fs::remove_all(directory_); }

  CssParser::ParseResult loadCorpusFile(CssParser& parser, const std::string& name) {
    HalFile source;
    const std::string path = (fs::path(kCorpusDir) / name).string();
    EXPECT_TRUE(Storage.openFileForRead("TST", path, source)) << "corpus file missing: " << path;
    return parser.loadFromStream(source);
  }

  CssParser::ParseResult loadInlineCss(CssParser& parser, const std::string& css) {
    const fs::path sourcePath = directory_ / "extra.css";
    std::ofstream output(sourcePath, std::ios::binary);
    output.write(css.data(), static_cast<std::streamsize>(css.size()));
    output.close();
    HalFile source;
    EXPECT_TRUE(Storage.openFileForRead("TST", sourcePath.string(), source));
    return parser.loadFromStream(source);
  }

  fs::path directory_;
};

// Invariants that must hold for ANY input, including corpus files added later.
TEST_P(CssCorpusTest, SurvivesArbitraryBytesAndStaysUsable) {
  CssParser parser((directory_ / "cache").string());
  loadCorpusFile(parser, GetParam());

  // Rule storage is bounded regardless of input.
  EXPECT_LE(parser.ruleCount(), 1500u);

  // Style resolution must not crash on junk queries after a hostile load.
  (void)parser.resolveStyle("p", "note");
  (void)parser.resolveStyle("", "");
  (void)parser.resolveStyle("no-such-tag", std::string("cls\0with\0nuls", 13));

  // The parser must still accept a well-formed stylesheet afterwards: a
  // malformed file may poison at most its own rules, never the parser.
  EXPECT_EQ(loadInlineCss(parser, "zqcanonical { font-weight: bold }\n"), CssParser::ParseResult::Complete);
  EXPECT_EQ(parser.resolveStyle("zqcanonical", "").fontWeight, CssFontWeight::Bold);
}

// Pins the CURRENT ParseResult and stored rule count per committed file.
// Empty rule bodies are dropped (style.defined.anySet() gate), which is why
// several malformed files pin to 0 rules.
struct PinnedOutcome {
  CssParser::ParseResult result;
  size_t ruleCount;
};

const std::map<std::string, PinnedOutcome>& pinnedOutcomes() {
  using R = CssParser::ParseResult;
  static const std::map<std::string, PinnedOutcome> outcomes = {
      {"empty_zero_bytes.css", {R::Complete, 0}},
      {"empty_whitespace.css", {R::Complete, 0}},
      {"trunc_mid_selector.css", {R::Partial, 0}},
      {"trunc_after_lbrace.css", {R::Partial, 0}},
      {"trunc_mid_decl.css", {R::Partial, 0}},
      {"unterminated_block.css", {R::Partial, 0}},
      {"unterminated_comment.css", {R::Partial, 1}},
      {"unterminated_atrule.css", {R::Partial, 0}},
      {"unterminated_string.css", {R::Complete, 1}},
      {"deep_nesting_64_blocks.css", {R::Complete, 0}},
      {"deep_nesting_64_unbalanced.css", {R::Partial, 0}},
      {"huge_selector_4k.css", {R::Partial, 0}},
      {"huge_decl_value_8k.css", {R::Partial, 1}},
      {"huge_numeric_value.css", {R::Complete, 1}},
      {"enc_nul_in_value.css", {R::Complete, 1}},
      {"enc_nul_in_selector.css", {R::Complete, 1}},
      {"enc_invalid_utf8.css", {R::Complete, 1}},
      {"enc_bom_utf8.css", {R::Complete, 1}},
      {"mismatch_extra_closers.css", {R::Complete, 0}},
  };
  return outcomes;
}

TEST_P(CssCorpusTest, ParseResultAndRuleCountMatchPinnedOutcome) {
  const auto it = pinnedOutcomes().find(GetParam());
  if (it == pinnedOutcomes().end()) {
    GTEST_SKIP() << "no pinned outcome yet for " << GetParam() << "; add one to pinnedOutcomes()";
  }
  CssParser parser((directory_ / "cache").string());
  EXPECT_EQ(loadCorpusFile(parser, GetParam()), it->second.result);
  EXPECT_EQ(parser.ruleCount(), it->second.ruleCount);
}

INSTANTIATE_TEST_SUITE_P(CssCorpus, CssCorpusTest, ::testing::ValuesIn(corpus::listCorpusFiles(kCorpusDir)),
                         corpus::NameFromFilename{});

// -- Behavior pins for individual file classes ------------------------------

class CssCorpusBehavior : public ::testing::Test {
 protected:
  CssParser parser{(fs::temp_directory_path() / "crosspoint_css_corpus_behavior").string()};

  void loadCorpusFile(const std::string& name) {
    HalFile source;
    const std::string path = (fs::path(kCorpusDir) / name).string();
    ASSERT_TRUE(Storage.openFileForRead("TST", path, source)) << "corpus file missing: " << path;
    parser.loadFromStream(source);
  }
};

TEST_F(CssCorpusBehavior, RuleBeforeUnterminatedCommentIsKept) {
  loadCorpusFile("unterminated_comment.css");
  EXPECT_EQ(parser.resolveStyle("p", "").textAlign, CssTextAlign::Center);
}

// Documents a current limitation: the tokenizer has no string awareness, so a
// '}' inside a quoted value closes the rule block early. The rule after the
// bogus quote still parses.
TEST_F(CssCorpusBehavior, BraceInsideQuotedValueEndsTheBlockEarly) {
  loadCorpusFile("unterminated_string.css");
  EXPECT_EQ(parser.resolveStyle("div", "x").fontWeight, CssFontWeight::Bold);
  EXPECT_FALSE(parser.resolveStyle("p", "").defined.anySet());
}

TEST_F(CssCorpusBehavior, DeclarationOverflowDropsOnlyTheOversizedDeclaration) {
  loadCorpusFile("huge_decl_value_8k.css");
  // The 8KB font-family value overflowed the declaration buffer and was
  // dropped, but the following text-align declaration in the same block
  // still applies.
  EXPECT_EQ(parser.resolveStyle("p", "").textAlign, CssTextAlign::Center);
}

// Documents a current limitation: absurd magnitudes are not rejected.
// margin-top: 999...9em (30 digits) becomes a defined 1e30em margin that
// flows into layout arithmetic unclamped. text-indent: 1e4999em is
// misparsed — the unit scanner stops at 'e', so the value is read as number
// "1" with unrecognized unit "e4999em" and stored as a defined 1px indent.
TEST_F(CssCorpusBehavior, AbsurdNumericLengthsSaturateOrMisparse) {
  loadCorpusFile("huge_numeric_value.css");
  const CssStyle style = parser.resolveStyle("p", "");
  ASSERT_TRUE(style.hasMarginTop());
  EXPECT_FLOAT_EQ(style.marginTop.value, 1e30f);
  EXPECT_EQ(style.marginTop.unit, CssUnit::Em);
  ASSERT_TRUE(style.hasTextIndent());
  EXPECT_FLOAT_EQ(style.textIndent.value, 1.0f);
  EXPECT_EQ(style.textIndent.unit, CssUnit::Pixels);
}

// Documents a current limitation: an unrecognized (here NUL-corrupted)
// text-align value is not dropped — it silently becomes the Left fallback.
TEST_F(CssCorpusBehavior, NulCorruptedAlignmentValueFallsBackToLeft) {
  loadCorpusFile("enc_nul_in_value.css");
  const CssStyle style = parser.resolveStyle("p", "");
  ASSERT_TRUE(style.hasTextAlign());
  EXPECT_EQ(style.textAlign, CssTextAlign::Left);
}

TEST_F(CssCorpusBehavior, InvalidUtf8SelectorBytesAreOpaqueAndResolvable) {
  loadCorpusFile("enc_invalid_utf8.css");
  EXPECT_EQ(parser.resolveStyle("p", "\xc3\x28\x80").fontWeight, CssFontWeight::Bold);
}

TEST_F(CssCorpusBehavior, NulByteInSelectorPreventsNormalLookup) {
  loadCorpusFile("enc_nul_in_selector.css");
  EXPECT_FALSE(parser.resolveStyle("p", "ab").hasFontWeight());
  EXPECT_FALSE(parser.resolveStyle("p", "a").hasFontWeight());
}

// A UTF-8 BOM before the stylesheet is consumed by the tokenizer, so the
// first rule of a BOM-prefixed stylesheet matches its element normally
// instead of being stored under the unmatchable selector "\xEF\xBB\xBFp".
TEST_F(CssCorpusBehavior, Utf8BomIsSkippedBeforeTheFirstSelector) {
  loadCorpusFile("enc_bom_utf8.css");
  EXPECT_EQ(parser.ruleCount(), 1u);
  const CssStyle style = parser.resolveStyle("p", "");
  ASSERT_TRUE(style.hasTextAlign());
  EXPECT_EQ(style.textAlign, CssTextAlign::Center);
}

}  // namespace
