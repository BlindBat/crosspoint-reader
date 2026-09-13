// Corpus-driven robustness tests: every file in test/corpus/xml/ is streamed
// through the vendored expat build (compiled with the firmware's flags:
// XML_GE=0, XML_CONTEXT_BYTES=1024) using the XmlParserUtils teardown helper.
// Universal invariants hold for any input; a pinned table records whether
// expat currently accepts or rejects each committed file. Regenerate the
// corpus with scripts/generate_test_corpus.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>

#include "XmlParserUtils.h"
#include "test/corpus/CorpusTestSupport.h"

namespace {

const std::string kCorpusDir = CORPUS_XML_DIR;
constexpr size_t kChunkSize = 1024;  // matches ChapterHtmlSlimParser's PARSE_BUFFER_SIZE

struct ParseStats {
  size_t startCount = 0;
  size_t endCount = 0;
  size_t depth = 0;
  size_t maxDepth = 0;
  size_t characterBytes = 0;
};

void XMLCALL onStart(void* userData, const XML_Char*, const XML_Char**) {
  auto* stats = static_cast<ParseStats*>(userData);
  ++stats->startCount;
  ++stats->depth;
  stats->maxDepth = std::max(stats->maxDepth, stats->depth);
}

void XMLCALL onEnd(void* userData, const XML_Char*) {
  auto* stats = static_cast<ParseStats*>(userData);
  ++stats->endCount;
  if (stats->depth > 0) --stats->depth;
}

void XMLCALL onCharacters(void* userData, const XML_Char*, int len) {
  static_cast<ParseStats*>(userData)->characterBytes += static_cast<size_t>(len);
}

struct ParseOutcome {
  bool accepted = false;
  XML_Error errorCode = XML_ERROR_NONE;
  ParseStats stats;
};

// Streams `input` through expat in ChapterHtmlSlimParser-sized chunks. The
// loop is bounded by construction (one iteration per chunk), so a corpus file
// can never hang the test.
ParseOutcome parseCorpusBytes(const std::string& input) {
  ParseOutcome outcome;
  XML_Parser parser = XML_ParserCreate(nullptr);
  EXPECT_NE(parser, nullptr);
  XML_SetUserData(parser, &outcome.stats);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onCharacters);

  outcome.accepted = true;
  size_t offset = 0;
  do {
    const size_t len = std::min(kChunkSize, input.size() - offset);
    const bool isFinal = offset + len == input.size();
    if (XML_Parse(parser, input.data() + offset, static_cast<int>(len), isFinal) == XML_STATUS_ERROR) {
      outcome.accepted = false;
      outcome.errorCode = XML_GetErrorCode(parser);
      break;
    }
    offset += len;
  } while (offset < input.size());

  destroyXmlParser(parser);
  EXPECT_EQ(parser, nullptr) << "destroyXmlParser must null the handle";
  return outcome;
}

class XmlCorpusTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string input = corpus::readCorpusFile(kCorpusDir, GetParam());
};

// Invariants that must hold for ANY input, including corpus files added later.
TEST_P(XmlCorpusTest, SurvivesArbitraryBytesWithBalancedCallbacks) {
  const ParseOutcome outcome = parseCorpusBytes(input);

  // An end-element callback can only follow its start-element callback.
  EXPECT_LE(outcome.stats.endCount, outcome.stats.startCount);

  // A rejected parse must carry a real error code; an accepted one must not.
  if (outcome.accepted) {
    EXPECT_EQ(outcome.errorCode, XML_ERROR_NONE);
    EXPECT_EQ(outcome.stats.startCount, outcome.stats.endCount);
  } else {
    EXPECT_NE(outcome.errorCode, XML_ERROR_NONE);
  }

  // Parsing the same bytes twice must be deterministic.
  const ParseOutcome again = parseCorpusBytes(input);
  EXPECT_EQ(again.accepted, outcome.accepted);
  EXPECT_EQ(again.errorCode, outcome.errorCode);
  EXPECT_EQ(again.stats.startCount, outcome.stats.startCount);
}

// Pins whether the firmware's expat configuration currently accepts or
// rejects each committed corpus file.
const std::map<std::string, bool>& pinnedAcceptance() {
  static const std::map<std::string, bool> accepted = {
      {"empty_zero_bytes.xml", false},
      {"empty_whitespace.xml", false},
      {"trunc_mid_decl.xml", false},
      {"trunc_mid_open_tag.xml", false},
      {"trunc_mid_attr_name.xml", false},
      {"trunc_mid_attr_value.xml", false},
      {"trunc_mid_close_tag.xml", false},
      {"trunc_mid_entity.xml", false},
      {"trunc_no_close.xml", false},
      {"unterminated_comment.xml", false},
      {"unterminated_cdata.xml", false},
      {"deep_nesting_64_wellformed.xml", true},
      {"deep_nesting_256_unclosed.xml", false},
      {"mismatch_tags.xml", false},
      {"mismatch_duplicate_attr.xml", false},
      {"mismatch_two_roots.xml", false},
      {"mismatch_bare_ampersand.xml", false},
      {"mismatch_undefined_entity.xml", false},
      {"enc_invalid_utf8_text.xml", false},
      {"enc_nul_in_text.xml", false},
      {"enc_bom_utf8.xml", true},
      {"enc_bom_utf16le.xml", true},
      {"huge_attr_value_64k.xml", true},
      {"huge_entity_expansion.xml", true},
  };
  return accepted;
}

TEST_P(XmlCorpusTest, AcceptanceMatchesPinnedOutcome) {
  const auto it = pinnedAcceptance().find(GetParam());
  if (it == pinnedAcceptance().end()) {
    GTEST_SKIP() << "no pinned outcome yet for " << GetParam() << "; add one to pinnedAcceptance()";
  }
  const ParseOutcome outcome = parseCorpusBytes(input);
  EXPECT_EQ(outcome.accepted, it->second) << "expat error: " << XML_ErrorString(outcome.errorCode);
}

INSTANTIATE_TEST_SUITE_P(XmlCorpus, XmlCorpusTest, ::testing::ValuesIn(corpus::listCorpusFiles(kCorpusDir)),
                         corpus::NameFromFilename{});

// -- Behavior pins for individual file classes ------------------------------

std::string corpusBytes(const std::string& name) { return corpus::readCorpusFile(kCorpusDir, name); }

TEST(XmlCorpusBehavior, DeeplyNestedWellFormedDocumentReaches64Levels) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("deep_nesting_64_wellformed.xml"));
  EXPECT_TRUE(outcome.accepted);
  EXPECT_EQ(outcome.stats.maxDepth, 64u);
  EXPECT_EQ(outcome.stats.startCount, 64u);
}

TEST(XmlCorpusBehavior, UnclosedDeepNestingSeesEveryOpenTagBeforeRejecting) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("deep_nesting_256_unclosed.xml"));
  EXPECT_FALSE(outcome.accepted);
  EXPECT_EQ(outcome.stats.startCount, 256u);
  EXPECT_EQ(outcome.stats.endCount, 0u);
}

TEST(XmlCorpusBehavior, MismatchedTagsRejectWithTagMismatch) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("mismatch_tags.xml"));
  EXPECT_FALSE(outcome.accepted);
  EXPECT_EQ(outcome.errorCode, XML_ERROR_TAG_MISMATCH);
}

TEST(XmlCorpusBehavior, DuplicateAttributesRejectWithDuplicateAttribute) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("mismatch_duplicate_attr.xml"));
  EXPECT_FALSE(outcome.accepted);
  EXPECT_EQ(outcome.errorCode, XML_ERROR_DUPLICATE_ATTRIBUTE);
}

TEST(XmlCorpusBehavior, Utf16DocumentWithBomIsAccepted) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("enc_bom_utf16le.xml"));
  EXPECT_TRUE(outcome.accepted);
  EXPECT_EQ(outcome.stats.startCount, 1u);
  EXPECT_EQ(outcome.stats.characterBytes, 2u);  // "ok" as UTF-8 after transcoding
}

// With the firmware's XML_GE=0 build, general entities are never expanded:
// the declared reference comes through as its 3-byte literal text "&c;"
// instead of the 1000 characters it would expand to, so entity-amplification
// (billion laughs) cannot allocate on the device.
TEST(XmlCorpusBehavior, EntityExpansionIsInertUnderFirmwareConfig) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("huge_entity_expansion.xml"));
  EXPECT_TRUE(outcome.accepted);
  EXPECT_EQ(outcome.stats.characterBytes, 3u);
}

TEST(XmlCorpusBehavior, HugeAttributeValueIsAcceptedWholesale) {
  const ParseOutcome outcome = parseCorpusBytes(corpusBytes("huge_attr_value_64k.xml"));
  EXPECT_TRUE(outcome.accepted);
  EXPECT_EQ(outcome.stats.startCount, 1u);
}

}  // namespace
