// Corpus-driven robustness tests: every file in test/corpus/html/ is run
// through ChapterHtmlSlimParser's public resumable-parse API with a bounded
// step budget, so an infinite parse loop fails the test instead of hanging
// it. A pinned table records whether each committed file currently builds
// pages or is rejected. Regenerate the corpus with
// scripts/generate_test_corpus.py.
//
// Note: this host build links the system expat (XML_GE=1); the firmware
// builds the vendored expat with XML_GE=0. Entity-reference outcomes pinned
// here are host outcomes — the XML_GE=0 configuration is covered by
// test/xml_parser_utils/XmlParserUtilsCorpusTest.cpp.

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "test/corpus/CorpusTestSupport.h"

namespace {

const std::string kCorpusDir = CORPUS_HTML_DIR;

struct ParseRun {
  bool built = false;   // parse completed and pages were flushed
  bool hung = false;    // step budget exhausted — would spin forever on device
  size_t pages = 0;     // pages handed to completePageFn
  size_t maxSteps = 0;  // budget the run was given
};

// Drives beginParse/parseStep/finishParse with a step budget derived from the
// file size: each successful step consumes up to 1KB, so a healthy parse of N
// bytes needs at most N/1024 + 1 steps. Slack of 16 keeps the bound generous.
ParseRun runBoundedParse(const std::string& filepath, GfxRenderer& renderer, const CssParser& cssParser) {
  ParseRun run;
  run.maxSteps = std::filesystem::file_size(filepath) / 1024 + 16;

  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               [&run](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) { ++run.pages; },
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  if (!parser.beginParse()) {
    return run;
  }
  for (size_t step = 0; step < run.maxSteps; ++step) {
    switch (parser.parseStep()) {
      case ChapterHtmlSlimParser::ParseStatus::Error:
        parser.abortParse();
        EXPECT_EQ(parser.parseBytesConsumed(), 0u);  // file handle released
        return run;
      case ChapterHtmlSlimParser::ParseStatus::Done:
        run.built = parser.finishParse();
        EXPECT_EQ(parser.parseBytesConsumed(), 0u);  // file handle released
        return run;
      case ChapterHtmlSlimParser::ParseStatus::More:
        break;
    }
  }
  run.hung = true;
  parser.abortParse();
  return run;
}

class ChapterHtmlCorpusTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string filepath = (std::filesystem::path(kCorpusDir) / GetParam()).string();
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
};

// Invariants that must hold for ANY input, including corpus files added later.
TEST_P(ChapterHtmlCorpusTest, TerminatesWithinStepBudgetAndIsDeterministic) {
  const ParseRun first = runBoundedParse(filepath, renderer, cssParser);
  EXPECT_FALSE(first.hung) << "parse did not finish within " << first.maxSteps << " steps";

  // A successful build of a non-empty chapter always flushes at least the
  // trailing page; a rejected parse must not report pages via finishParse.
  if (first.built) {
    EXPECT_GE(first.pages, 1u);
  }

  const ParseRun second = runBoundedParse(filepath, renderer, cssParser);
  EXPECT_EQ(second.built, first.built);
  EXPECT_EQ(second.pages, first.pages);
}

// Pins whether each committed corpus file currently builds pages (true) or is
// rejected by the expat-backed parse (false).
const std::map<std::string, bool>& pinnedBuilds() {
  static const std::map<std::string, bool> builds = {
      {"empty_zero_bytes.html", false},
      {"empty_whitespace.html", false},
      {"trunc_mid_tag.html", false},
      {"trunc_mid_attr.html", false},
      {"trunc_after_text.html", false},
      {"trunc_trailing_garbage.html", true},
      {"unterminated_comment.html", false},
      {"unterminated_cdata.html", false},
      {"deep_nesting_64_divs.html", true},
      {"mismatch_tags.html", false},
      {"mismatch_duplicate_attr.html", false},
      {"mismatch_undefined_entity.html", false},
      {"enc_invalid_utf8.html", false},
      {"enc_nul_in_text.html", false},
      {"enc_bom_utf8.html", true},
      {"huge_word_1k.html", true},
  };
  return builds;
}

TEST_P(ChapterHtmlCorpusTest, BuildOutcomeMatchesPinnedOutcome) {
  const auto it = pinnedBuilds().find(GetParam());
  if (it == pinnedBuilds().end()) {
    GTEST_SKIP() << "no pinned outcome yet for " << GetParam() << "; add one to pinnedBuilds()";
  }
  const ParseRun run = runBoundedParse(filepath, renderer, cssParser);
  EXPECT_FALSE(run.hung);
  EXPECT_EQ(run.built, it->second);
}

INSTANTIATE_TEST_SUITE_P(HtmlCorpus, ChapterHtmlCorpusTest, ::testing::ValuesIn(corpus::listCorpusFiles(kCorpusDir)),
                         corpus::NameFromFilename{});

// -- Behavior pins for individual file classes ------------------------------

class ChapterHtmlCorpusBehavior : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};

  ParseRun runFile(const std::string& name) {
    return runBoundedParse((std::filesystem::path(kCorpusDir) / name).string(), renderer, cssParser);
  }
};

TEST_F(ChapterHtmlCorpusBehavior, SixtyFourNestedDivsBuildAPageWithTheirText) {
  const ParseRun run = runFile("deep_nesting_64_divs.html");
  EXPECT_TRUE(run.built);
  EXPECT_EQ(run.pages, 1u);
}

// Documents current tolerance: once </html> has been seen, trailing garbage
// after it is ignored instead of failing the chapter.
TEST_F(ChapterHtmlCorpusBehavior, GarbageAfterClosingHtmlTagIsIgnored) {
  const ParseRun run = runFile("trunc_trailing_garbage.html");
  EXPECT_TRUE(run.built);
  EXPECT_EQ(run.pages, 1u);
}

TEST_F(ChapterHtmlCorpusBehavior, OversizedSingleWordStillBuildsPages) {
  const ParseRun run = runFile("huge_word_1k.html");
  EXPECT_TRUE(run.built);
  EXPECT_GE(run.pages, 1u);
}

TEST_F(ChapterHtmlCorpusBehavior, TruncatedChapterIsRejectedNotPartiallyEmitted) {
  const ParseRun run = runFile("trunc_after_text.html");
  EXPECT_FALSE(run.built);
  // Small truncated chapters are dropped whole: no pages leak out before the
  // error is detected.
  EXPECT_EQ(run.pages, 0u);
}

}  // namespace
