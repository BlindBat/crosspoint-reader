// Corpus-driven robustness tests: every file in test/corpus/json/ is fed
// through StreamingJsonParser. Universal invariants hold for any input;
// per-file pins document what the parser currently does with each class of
// malformed input (regenerate corpus with scripts/generate_test_corpus.py).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "lib/JsonParser/StreamingJsonParser.h"
#include "test/corpus/CorpusTestSupport.h"

namespace {

enum class EventType : uint8_t {
  KEY,
  STRING,
  NUMBER,
  BOOL_TRUE,
  BOOL_FALSE,
  NULL_VAL,
  OBJECT_START,
  OBJECT_END,
  ARRAY_START,
  ARRAY_END,
};

struct Event {
  EventType type;
  std::string value;
};

struct EventLog {
  std::vector<Event> events;

  size_t count(EventType type) const {
    size_t n = 0;
    for (const Event& e : events) {
      if (e.type == type) ++n;
    }
    return n;
  }
};

void onKey(void* ctx, const char* key, size_t len) {
  static_cast<EventLog*>(ctx)->events.push_back({EventType::KEY, std::string(key, len)});
}
void onString(void* ctx, const char* value, size_t len) {
  static_cast<EventLog*>(ctx)->events.push_back({EventType::STRING, std::string(value, len)});
}
void onNumber(void* ctx, const char* value, size_t len) {
  static_cast<EventLog*>(ctx)->events.push_back({EventType::NUMBER, std::string(value, len)});
}
void onBool(void* ctx, bool value) {
  static_cast<EventLog*>(ctx)->events.push_back({value ? EventType::BOOL_TRUE : EventType::BOOL_FALSE, {}});
}
void onNull(void* ctx) { static_cast<EventLog*>(ctx)->events.push_back({EventType::NULL_VAL, {}}); }
void onObjectStart(void* ctx) { static_cast<EventLog*>(ctx)->events.push_back({EventType::OBJECT_START, {}}); }
void onObjectEnd(void* ctx) { static_cast<EventLog*>(ctx)->events.push_back({EventType::OBJECT_END, {}}); }
void onArrayStart(void* ctx) { static_cast<EventLog*>(ctx)->events.push_back({EventType::ARRAY_START, {}}); }
void onArrayEnd(void* ctx) { static_cast<EventLog*>(ctx)->events.push_back({EventType::ARRAY_END, {}}); }

JsonCallbacks makeCallbacks(EventLog* log) {
  return {log, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd};
}

const std::string kCorpusDir = CORPUS_JSON_DIR;

class StreamingJsonParserCorpusTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string input = corpus::readCorpusFile(kCorpusDir, GetParam());
  EventLog log;
  StreamingJsonParser parser{makeCallbacks(&log)};

  void feedAll() { parser.feed(input.data(), input.size()); }
};

// Invariants that must hold for ANY input, including corpus files added later.
TEST_P(StreamingJsonParserCorpusTest, SurvivesArbitraryBytesWithBoundedOutput) {
  feedAll();

  // Every callback consumes at least one input byte; a value-delimiting byte
  // can additionally fire its own structural event, so 2N is a hard ceiling.
  EXPECT_LE(log.events.size(), 2 * input.size());

  // Emitted tokens can never exceed the fixed token buffer.
  for (const Event& e : log.events) {
    EXPECT_LT(e.value.size(), StreamingJsonParser::TOKEN_BUF_SIZE);
  }

  // Once the error latch is set, further input must be ignored entirely.
  if (parser.hasError()) {
    const size_t eventsAtError = log.events.size();
    const char* valid = R"({"after":"error"})";
    parser.feed(valid, strlen(valid));
    EXPECT_EQ(log.events.size(), eventsAtError);
  }

  // reset() must fully recover the parser no matter what the corpus file did.
  parser.reset();
  log.events.clear();
  const char* canonical = R"({"k":"v","n":7})";
  parser.feed(canonical, strlen(canonical));
  EXPECT_FALSE(parser.hasError());
  ASSERT_EQ(log.events.size(), 6u);
  EXPECT_EQ(log.events[0].type, EventType::OBJECT_START);
  EXPECT_EQ(log.events[1].value, "k");
  EXPECT_EQ(log.events[2].value, "v");
  EXPECT_EQ(log.events[4].value, "7");
  EXPECT_EQ(log.events[5].type, EventType::OBJECT_END);
}

// Pins the CURRENT outcome for each committed corpus file. `errorExpected`
// mirrors hasError(); the parser is deliberately lenient, so most malformed
// files do NOT set the error latch — only nesting overflow and broken
// true/false/null literals do.
struct PinnedOutcome {
  bool errorExpected;
};

const std::map<std::string, PinnedOutcome>& pinnedOutcomes() {
  static const std::map<std::string, PinnedOutcome> outcomes = {
      {"empty_zero_bytes.json", {false}},
      {"empty_whitespace.json", {false}},
      {"trunc_after_lbrace.json", {false}},
      {"trunc_after_key.json", {false}},
      {"trunc_after_colon.json", {false}},
      {"trunc_after_comma.json", {false}},
      {"trunc_mid_array.json", {false}},
      {"trunc_mid_literal.json", {false}},
      {"trunc_mid_number.json", {false}},
      {"unterminated_string.json", {false}},
      {"unterminated_string_escape.json", {false}},
      {"deep_nesting_32_arrays.json", {false}},
      {"deep_nesting_64_arrays.json", {true}},
      {"deep_nesting_64_objects.json", {true}},
      {"huge_number_600_digits.json", {false}},
      {"huge_number_exponent.json", {false}},
      {"huge_string_600_chars.json", {false}},
      {"enc_nul_in_string.json", {false}},
      {"enc_nul_between_tokens.json", {false}},
      {"enc_invalid_utf8_string.json", {false}},
      {"enc_bom_utf8.json", {false}},
      {"enc_bom_utf16le.json", {false}},
      {"mismatch_extra_closers.json", {false}},
      {"mismatch_bracket_types.json", {false}},
      {"mismatch_bare_garbage.json", {false}},
      {"release_trunc_mid_assets.json", {false}},
      {"release_huge_asset_size.json", {false}},
      {"release_wrong_types.json", {false}},
  };
  return outcomes;
}

TEST_P(StreamingJsonParserCorpusTest, ErrorLatchMatchesPinnedOutcome) {
  const auto it = pinnedOutcomes().find(GetParam());
  if (it == pinnedOutcomes().end()) {
    GTEST_SKIP() << "no pinned outcome yet for " << GetParam() << "; add one to pinnedOutcomes()";
  }
  feedAll();
  EXPECT_EQ(parser.hasError(), it->second.errorExpected);
}

INSTANTIATE_TEST_SUITE_P(JsonCorpus, StreamingJsonParserCorpusTest,
                         ::testing::ValuesIn(corpus::listCorpusFiles(kCorpusDir)), corpus::NameFromFilename{});

// -- Behavior pins for individual file classes ------------------------------

class JsonCorpusBehavior : public ::testing::Test {
 protected:
  EventLog log;
  StreamingJsonParser parser{makeCallbacks(&log)};

  void feedFile(const std::string& name) {
    const std::string input = corpus::readCorpusFile(kCorpusDir, name);
    ASSERT_FALSE(input.empty() && name.find("empty_zero") == std::string::npos) << "corpus file missing: " << name;
    parser.feed(input.data(), input.size());
  }
};

TEST_F(JsonCorpusBehavior, TruncationAfterCommaKeepsEventsEmittedSoFar) {
  feedFile("trunc_after_comma.json");
  ASSERT_EQ(log.events.size(), 3u);
  EXPECT_EQ(log.events[0].type, EventType::OBJECT_START);
  EXPECT_EQ(log.events[1].value, "a");
  EXPECT_EQ(log.events[2].value, "1");
  EXPECT_FALSE(parser.hasError());
}

TEST_F(JsonCorpusBehavior, TruncationMidNumberNeverEmitsTheUndelimitedNumber) {
  feedFile("trunc_mid_number.json");
  EXPECT_EQ(log.count(EventType::NUMBER), 0u);
  EXPECT_FALSE(parser.hasError());
}

TEST_F(JsonCorpusBehavior, NestingBeyondMaxDepthStopsAtExactly32Containers) {
  feedFile("deep_nesting_64_arrays.json");
  EXPECT_TRUE(parser.hasError());
  EXPECT_EQ(log.count(EventType::ARRAY_START), StreamingJsonParser::MAX_NESTING);
  EXPECT_EQ(log.count(EventType::ARRAY_END), 0u);
}

TEST_F(JsonCorpusBehavior, MaxAllowedNestingDepthParsesCompletely) {
  feedFile("deep_nesting_32_arrays.json");
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(log.count(EventType::ARRAY_START), 32u);
  EXPECT_EQ(log.count(EventType::ARRAY_END), 32u);
  EXPECT_EQ(log.count(EventType::NUMBER), 1u);
}

// Documents a current limitation: a value longer than TOKEN_BUF_SIZE-1 is
// dropped SILENTLY — no callback, no error latch. Consumers cannot tell an
// oversized value from an absent one.
TEST_F(JsonCorpusBehavior, OversizedNumberIsSilentlyDroppedWithoutError) {
  feedFile("huge_number_600_digits.json");
  EXPECT_EQ(log.count(EventType::NUMBER), 0u);
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(log.count(EventType::OBJECT_END), 1u);  // parsing continued past it
}

TEST_F(JsonCorpusBehavior, OversizedStringIsSilentlyDroppedWithoutError) {
  feedFile("huge_string_600_chars.json");
  EXPECT_EQ(log.count(EventType::STRING), 0u);
  EXPECT_FALSE(parser.hasError());
  EXPECT_EQ(log.count(EventType::OBJECT_END), 1u);
}

TEST_F(JsonCorpusBehavior, HugeExponentNumberIsPassedThroughAsRawText) {
  feedFile("huge_number_exponent.json");
  ASSERT_EQ(log.count(EventType::NUMBER), 1u);
  EXPECT_EQ(log.events[2].value, "1e309");  // parser emits text; conversion is the consumer's problem
}

TEST_F(JsonCorpusBehavior, NulBytesInsideStringsAreKeptAsData) {
  feedFile("enc_nul_in_string.json");
  ASSERT_EQ(log.count(EventType::STRING), 1u);
  EXPECT_EQ(log.events[2].value, std::string("a\0b", 3));
  EXPECT_FALSE(parser.hasError());
}

TEST_F(JsonCorpusBehavior, Utf8BomBeforeDocumentIsIgnored) {
  feedFile("enc_bom_utf8.json");
  ASSERT_EQ(log.events.size(), 4u);
  EXPECT_EQ(log.events[1].value, "key");
  EXPECT_EQ(log.events[2].value, "value");
  EXPECT_FALSE(parser.hasError());
}

// Documents a current limitation: unbalanced closers never set the error
// latch — each stray '}' / ']' still fires its end callback.
TEST_F(JsonCorpusBehavior, ExtraClosersFireCallbacksWithoutError) {
  feedFile("mismatch_extra_closers.json");
  EXPECT_EQ(log.count(EventType::OBJECT_END), 3u);
  EXPECT_EQ(log.count(EventType::ARRAY_END), 2u);
  EXPECT_FALSE(parser.hasError());
}

TEST_F(JsonCorpusBehavior, NonJsonGarbageProducesNoEventsAndNoError) {
  feedFile("mismatch_bare_garbage.json");
  EXPECT_EQ(log.events.size(), 0u);
  EXPECT_FALSE(parser.hasError());
}

}  // namespace
