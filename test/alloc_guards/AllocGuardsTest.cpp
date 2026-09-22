#include <Memory.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "AllocCounter.h"
#include "CssParser.h"
#include "lib/Epub/Epub/TokenBoundary.h"
#include "lib/Epub/Epub/hyphenation/HyphenationCommon.h"
#include "lib/Epub/Epub/hyphenation/Hyphenator.h"

// Performance-regression guards centered on HEAP ALLOCATION COUNTS.
//
// On the ESP32-C3 target (~380KB usable RAM, no MMU) per-word and per-element heap churn is the
// dominant fragmentation and performance hazard, so allocation counts are the honest host-side
// proxy for device behavior. Wall-clock time is only asserted as coarse order-of-magnitude
// ceilings and only when CROSSPOINT_PERF_TIME=1 is set (never in default/CI runs).
//
// All budgets below state the measured baseline, the measurement platform, and the date so they
// can be re-baselined deliberately instead of being bumped blindly when they fire.

namespace fs = std::filesystem;

// ForOverwriteReturnsNullForAnUnsatisfiableRequest relies on a failing allocation returning
// nullptr. AddressSanitizer aborts on an oversized request unless it is told it may return null;
// that setting only affects allocation failure, not memory-error detection.
extern "C" const char* __asan_default_options() { return "allocator_may_return_null=1"; }

namespace {

std::vector<uint32_t> codepointValues(const std::vector<CodepointInfo>& cps) {
  std::vector<uint32_t> values;
  values.reserve(cps.size());
  for (const auto& cp : cps) {
    values.push_back(cp.value);
  }
  return values;
}

// Words steady-state layout sees constantly: plain alphabetic tokens, punctuation-wrapped tokens,
// precomposed and combining diacritics, Cyrillic, digits, and one long compound. None contains an
// explicit hyphen or an apostrophe, so with no language patterns loaded and fallback disabled
// every word takes the common no-break path and must return no break points.
const std::vector<std::string>& noBreakWords() {
  static const std::vector<std::string> kWords = {
      "the",          "reading",        "firmware,",
      "(device)",
      "«typography»",  // «typography»
      "“quoted”",      // “quoted”
      "café.",         // café. (precomposed é)
      "café",          // café (combining acute, composed by collectCodepoints)
      "читання",       // читання
      "2026",         "reference[12],", "Donaudampfschifffahrtsgesellschaft",
  };
  return kWords;
}

// English words that all produce Liang pattern breaks; used to pin the allocation budget of the
// productive hyphenation path (the returned break vectors themselves must allocate).
const std::vector<std::string>& englishHyphenatableWords() {
  static const std::vector<std::string> kWords = {
      "hyphenation", "representative", "considerable", "typography",  "attention",
      "developer",   "wonderful",      "university",   "information", "particular",
  };
  return kWords;
}

// Release builds may elide a new/delete pair whose pointer never escapes (C++14 allocation
// elision), which would silently make every allocation budget below read zero. Routing the
// pointer through a volatile store makes the allocation observable again.
volatile void* g_escapeSink = nullptr;

void escape(void* pointer) { g_escapeSink = pointer; }

bool wallTimeCeilingsEnabled() {
  const char* flag = std::getenv("CROSSPOINT_PERF_TIME");
  return flag != nullptr && flag[0] == '1' && flag[1] == '\0';
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Hyphenator::breakOffsets — the hottest per-word call during section layout.
// ---------------------------------------------------------------------------------------------

// Permanent guard for commit "perf: Reduce per-word allocations in hyphenation": breakOffsets
// reuses a function-local codepoint buffer, so after the buffer reached its steady-state capacity
// the common no-break path must not touch the heap at all. Before that commit this measured
// exactly 1 allocation per call (a fresh codepoint vector per word).
TEST(HyphenationAllocGuard, NoBreakWordPathIsAllocationFreeAfterWarmUp) {
  Hyphenator::setPreferredLanguage("zz");  // Unknown tag -> no pattern hyphenator registered.
  const auto& words = noBreakWords();

  size_t totalBreaks = 0;
  // Warm-up pass: grows the reused codepoint buffer to its steady-state capacity, which is
  // bounded by the longest word in the list.
  for (const auto& word : words) {
    totalBreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
  }

  size_t allocations = 0;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    for (int pass = 0; pass < 3; ++pass) {
      for (const auto& word : words) {
        totalBreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
      }
    }
    allocations = scope.count();
    bytes = scope.bytes();
  }

  // Sanity: these words really take the no-break path (all returned vectors are empty), so the
  // zero-allocation expectation below is not measuring some unrelated early-out.
  EXPECT_EQ(totalBreaks, 0u);

  // Budget: ZERO. Baseline measured 2026-09-13, macOS arm64 host (AppleClang/libc++, Release):
  // 0 allocations / 0 bytes across 3 passes x 12 words.
  EXPECT_EQ(allocations, 0u);
  EXPECT_EQ(bytes, 0u);
}

// The productive (pattern-hit) path must allocate only for the vectors it returns, and the count
// must be flat across passes — any growth between identical passes means per-call buffer churn
// crept back in.
TEST(HyphenationAllocGuard, EnglishPatternPathAllocationsStayFlatAcrossPasses) {
  Hyphenator::setPreferredLanguage("en");
  const auto& words = englishHyphenatableWords();

  size_t warmupBreaks = 0;
  for (const auto& word : words) {
    warmupBreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
  }
  // Sanity: patterns are loaded and productive for every word in the list.
  ASSERT_GE(warmupBreaks, words.size());

  size_t passAAllocations = 0;
  size_t passABreaks = 0;
  {
    alloc_counter::CountingScope scope;
    for (const auto& word : words) {
      passABreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
    }
    passAAllocations = scope.count();
  }

  size_t passBAllocations = 0;
  size_t passBBreaks = 0;
  {
    alloc_counter::CountingScope scope;
    for (const auto& word : words) {
      passBBreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
    }
    passBAllocations = scope.count();
  }

  std::cout << "[measured] english pass allocations=" << passAAllocations << "/" << passBAllocations
            << " breaks=" << passABreaks << "/" << passBBreaks << "\n";

  // Identical passes must behave identically: same breaks, same allocation count.
  EXPECT_EQ(passABreaks, passBBreaks);
  EXPECT_EQ(passAAllocations, passBAllocations);

  // Budget with headroom. Baseline measured 2026-09-13, macOS arm64 host (AppleClang/libc++,
  // Release): 30 allocations per pass for these 10 words / 21 breaks (pattern index vectors +
  // returned BreakInfo vectors only, ~3 per word). Budget = 36 (baseline +20% for STL
  // growth-policy drift). A per-word scratch buffer regression adds >= 10 on top of baseline.
  constexpr size_t kEnglishPassAllocationBudget = 36;
  EXPECT_LE(passAAllocations, kEnglishPassAllocationBudget)
      << "steady-state allocations per pass regressed (measured " << passAAllocations << ")";
}

// ---------------------------------------------------------------------------------------------
// trimSurroundingPunctuationAndFootnote — bulk-erase path.
// ---------------------------------------------------------------------------------------------

TEST(TrimPunctuationGuard, BulkLeadingEraseKeepsWordAndOriginalByteOffsets) {
  // « (2 UTF-8 bytes) + “ (3 bytes) precede the word, so 'w' sits at byte offset 5.
  auto cps = collectCodepoints("«“word”»,");
  trimSurroundingPunctuationAndFootnote(cps);

  EXPECT_EQ(codepointValues(cps), (std::vector<uint32_t>{'w', 'o', 'r', 'd'}));
  ASSERT_FALSE(cps.empty());
  // The bulk erase must shift the surviving elements without touching their byte offsets, which
  // still index into the ORIGINAL word for break-point mapping.
  EXPECT_EQ(cps.front().byteOffset, 5u);
  EXPECT_EQ(cps.back().byteOffset, 8u);
}

TEST(TrimPunctuationGuard, AllPunctuationWordBecomesEmpty) {
  auto cps = collectCodepoints("«...»");
  trimSurroundingPunctuationAndFootnote(cps);
  EXPECT_TRUE(cps.empty());
}

TEST(TrimPunctuationGuard, FootnoteReferenceAndTrailingPunctuationAreDropped) {
  auto cps = collectCodepoints("reference[12],");
  trimSurroundingPunctuationAndFootnote(cps);
  EXPECT_EQ(codepointValues(cps), (std::vector<uint32_t>{'r', 'e', 'f', 'e', 'r', 'e', 'n', 'c', 'e'}));
}

// Pins the "no allocation behavior change" property of the bulk-erase rewrite: trimming only
// erases in place, so it must never allocate regardless of input shape.
TEST(TrimPunctuationGuard, TrimmingNeverAllocates) {
  std::vector<std::vector<CodepointInfo>> inputs;
  inputs.push_back(collectCodepoints("«“word”»,"));
  inputs.push_back(collectCodepoints("(hello),"));
  inputs.push_back(collectCodepoints("«...»"));
  inputs.push_back(collectCodepoints("reference[12]."));
  inputs.push_back(collectCodepoints("plain"));

  size_t allocations = 0;
  {
    alloc_counter::CountingScope scope;
    for (auto& cps : inputs) {
      trimSurroundingPunctuationAndFootnote(cps);
    }
    allocations = scope.count();
  }

  // Budget: ZERO. Baseline measured 2026-09-13, macOS arm64 host: 0 allocations for all shapes
  // (leading bulk erase, trailing pops, footnote erase, full clear, untouched word).
  EXPECT_EQ(allocations, 0u);

  // The measured calls still produced the right words (guard is not measuring dead code).
  EXPECT_EQ(codepointValues(inputs[0]), (std::vector<uint32_t>{'w', 'o', 'r', 'd'}));
  EXPECT_EQ(codepointValues(inputs[1]), (std::vector<uint32_t>{'h', 'e', 'l', 'l', 'o'}));
  EXPECT_TRUE(inputs[2].empty());
}

// ---------------------------------------------------------------------------------------------
// TokenBoundary — per-boundary line-break predicates used for every token gap during layout.
// ---------------------------------------------------------------------------------------------

// The three predicates are pure scalar classifiers; pin that they stay allocation-free so nobody
// reintroduces string/map lookups into the per-gap layout path.
TEST(TokenBoundaryAllocGuard, BoundaryPredicatesAreAllocationFree) {
  bool sawBreakable = false;
  bool sawNonBreakable = false;
  bool sawJustifiable = false;
  size_t allocations = 0;
  {
    alloc_counter::CountingScope scope;
    for (uint32_t cp = 0; cp <= 0x3000; ++cp) {
      const bool breaks = TokenBoundary::allowsBreakAfterExplicitHyphen(cp);
      sawBreakable = sawBreakable || breaks;
      sawNonBreakable = sawNonBreakable || !breaks;
    }
    for (int continues = 0; continues <= 1; ++continues) {
      for (int noSpaceBefore = 0; noSpaceBefore <= 1; ++noSpaceBefore) {
        for (int isSpaceToken = 0; isSpaceToken <= 1; ++isSpaceToken) {
          sawJustifiable =
              sawJustifiable || TokenBoundary::isJustifiableGap(continues != 0, noSpaceBefore != 0, isSpaceToken != 0);
          (void)TokenBoundary::allowsBreak(continues != 0, noSpaceBefore != 0);
        }
      }
    }
    allocations = scope.count();
  }

  // Budget: ZERO. Baseline measured 2026-09-13, macOS arm64 host: 0 allocations.
  EXPECT_EQ(allocations, 0u);
  // Classification really ran across both classes.
  EXPECT_TRUE(sawBreakable);
  EXPECT_TRUE(sawNonBreakable);
  EXPECT_TRUE(sawJustifiable);
}

// ---------------------------------------------------------------------------------------------
// CssParser — stylesheet parse (once per book) and style resolution (once per HTML element).
// ---------------------------------------------------------------------------------------------

namespace {

// Modeled on a typical EPUB stylesheet: element rules, class rules, element.class rules, grouped
// selectors, plus constructs the parser deliberately skips (descendant selectors, @media).
constexpr const char* kRepresentativeStylesheet =
    "body { margin: 0; padding: 0; text-align: justify; }\n"
    "p { margin-top: 0; margin-bottom: 0; text-indent: 1.2em; text-align: justify; }\n"
    "h1, h2, h3 { text-align: center; font-weight: bold; margin-top: 2em; margin-bottom: 1em; }\n"
    "blockquote { margin-left: 1em; margin-right: 1em; font-style: italic; }\n"
    ".chapter-title { font-size: 1.5em; text-align: center; margin-top: 3em; }\n"
    ".epigraph { font-style: italic; margin-left: 15%; text-align: right; }\n"
    ".footnote { font-size: 0.8em; }\n"
    ".small-caps { font-variant: small-caps; }\n"
    ".center { text-align: center; }\n"
    ".right { text-align: right; }\n"
    "p.first { text-indent: 0; }\n"
    "p.noindent { text-indent: 0; margin-top: 0.5em; }\n"
    "div.stanza { margin-left: 2em; margin-top: 1em; }\n"
    "span.italic { font-style: italic; }\n"
    "span.bold { font-weight: bold; }\n"
    "div > p { text-indent: 0; }\n"
    "p:first-child { margin-top: 1em; }\n"
    "@media screen { p { color: black; } }\n"
    "hr { margin-top: 1em; margin-bottom: 1em; }\n"
    "em { font-style: italic; }\n"
    "strong { font-weight: bold; }\n";

class CssAllocGuardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_alloc_guards_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
  }

  void TearDown() override { fs::remove_all(directory_); }

  std::string cachePath() const { return directory_.string(); }

  // Writes the stylesheet and opens it for reading, all outside any counting scope.
  bool openStylesheet(const std::string& css, HalFile& source) const {
    const fs::path sourcePath = directory_ / "input.css";
    std::ofstream output(sourcePath, std::ios::binary);
    output.write(css.data(), static_cast<std::streamsize>(css.size()));
    output.close();
    return HalStorage::getInstance().openFileForRead("TST", sourcePath.string(), source);
  }

  fs::path directory_;
};

}  // namespace

TEST_F(CssAllocGuardTest, RepresentativeStylesheetParseStaysWithinAllocationBudget) {
  CssParser parser(cachePath());
  HalFile source;
  ASSERT_TRUE(openStylesheet(kRepresentativeStylesheet, source));

  size_t allocations = 0;
  size_t bytes = 0;
  CssParser::ParseResult result = CssParser::ParseResult::Error;
  {
    alloc_counter::CountingScope scope;
    result = parser.loadFromStream(source);
    allocations = scope.count();
    bytes = scope.bytes();
  }

  std::cout << "[measured] css parse allocations=" << allocations << " bytes=" << bytes
            << " rules=" << parser.ruleCount() << "\n";

  // The budget only means something if the sheet actually parsed and cascades correctly.
  // 18 rules: the h1,h2,h3 group expands to 3; "div > p", "p:first-child" and the @media block
  // are unsupported and skipped; .footnote (font-size only) and .small-caps (font-variant only)
  // carry no supported declarations and are dropped.
  EXPECT_EQ(result, CssParser::ParseResult::Complete);
  EXPECT_EQ(parser.ruleCount(), 18u);
  const CssStyle style = parser.resolveStyle("p", "first");
  EXPECT_EQ(style.textAlign, CssTextAlign::Justify);
  ASSERT_TRUE(style.hasTextIndent());
  EXPECT_FLOAT_EQ(style.textIndent.value, 0.0f);

  // Budget with headroom. Baseline measured 2026-09-13, macOS arm64 host (AppleClang/libc++,
  // Release): 3 allocations / 6784 bytes for the whole sheet — the parser tokenizes with
  // string_views and stores rules in pooled arrays, so only the pools themselves allocate.
  // Budget = 4 (~+33%, the smallest useful headroom above such a tiny baseline; one extra pool
  // growth step still fits). Per-rule churn would land at 18+, per-declaration churn at 60+.
  constexpr size_t kCssParseAllocationBudget = 4;
  EXPECT_LE(allocations, kCssParseAllocationBudget)
      << "stylesheet parse allocations regressed (measured " << allocations << ")";
}

// resolveStyle runs once per HTML element while a chapter is parsed; it must stay allocation-free
// (string_view tokenization over pooled rule storage, merged into a value-type CssStyle).
TEST_F(CssAllocGuardTest, ResolveStyleLookupsAreAllocationFree) {
  CssParser parser(cachePath());
  HalFile source;
  ASSERT_TRUE(openStylesheet(kRepresentativeStylesheet, source));
  ASSERT_EQ(parser.loadFromStream(source), CssParser::ParseResult::Complete);

  CssStyle resolved;
  size_t allocations = 0;
  {
    alloc_counter::CountingScope scope;
    for (int i = 0; i < 100; ++i) {
      resolved = parser.resolveStyle("p", "first epigraph");
      (void)parser.resolveStyle("h1", "chapter-title");
      (void)parser.resolveStyle("span", "bold");
      (void)parser.resolveStyle("div", "");
    }
    allocations = scope.count();
  }

  // Budget: ZERO. Baseline measured 2026-09-13, macOS arm64 host: 0 allocations for 400 lookups.
  EXPECT_EQ(allocations, 0u);

  // The measured lookups resolved real styles (cascade: p < .epigraph < p.first).
  EXPECT_EQ(resolved.fontStyle, CssFontStyle::Italic);
  ASSERT_TRUE(resolved.hasTextIndent());
  EXPECT_FLOAT_EQ(resolved.textIndent.value, 0.0f);
}

// ---------------------------------------------------------------------------------------------
// lib/Memory/Memory.h — makeUniqueNoThrowForOverwrite. AllocCounter.cpp replaces the global
// operator new[], including the non-throwing form the helpers use, so the helper's heap
// footprint can be pinned exactly instead of inferred.
// ---------------------------------------------------------------------------------------------

// Budget: exactly one operator-new[] call of exactly the requested byte count. uint8_t has a
// trivial destructor, so there is no array cookie and the request is the raw buffer size, which
// is the number the 380KB device budget is reasoned about in.
TEST(MemoryAllocGuard, ForOverwriteArrayIsOneAllocationOfExactlyTheRequestedBytes) {
  constexpr size_t kBytes = 4096;
  size_t allocations = 0;
  size_t bytes = 0;
  bool allocated = false;
  {
    alloc_counter::CountingScope scope;
    auto buf = makeUniqueNoThrowForOverwrite<uint8_t[]>(kBytes);
    escape(buf.get());
    allocated = buf != nullptr;
    if (allocated) buf[kBytes - 1] = 0xAB;  // touch the far end inside the measured scope
    allocations = scope.count();
    bytes = scope.bytes();
  }

  EXPECT_TRUE(allocated);
  EXPECT_EQ(allocations, 1u);
  EXPECT_EQ(bytes, kBytes);
}

// The two siblings must be interchangeable from an allocation-budget point of view: skipping
// value-initialisation changes what is written into the block, never how much is requested.
TEST(MemoryAllocGuard, ForOverwriteAndValueInitialisingSiblingRequestTheSameBytes) {
  constexpr size_t kCount = 1024;  // 4096 bytes as uint32_t
  size_t zeroedAllocations = 0;
  size_t zeroedBytes = 0;
  size_t rawAllocations = 0;
  size_t rawBytes = 0;
  {
    alloc_counter::CountingScope scope;
    auto zeroed = makeUniqueNoThrow<uint32_t[]>(kCount);
    escape(zeroed.get());
    zeroedAllocations = scope.count();
    zeroedBytes = scope.bytes();
  }
  {
    alloc_counter::CountingScope scope;
    auto raw = makeUniqueNoThrowForOverwrite<uint32_t[]>(kCount);
    escape(raw.get());
    rawAllocations = scope.count();
    rawBytes = scope.bytes();
  }

  EXPECT_EQ(rawAllocations, zeroedAllocations);
  EXPECT_EQ(rawBytes, zeroedBytes);
  EXPECT_EQ(rawBytes, kCount * sizeof(uint32_t));
}

// Destroying the unique_ptr must free the block and allocate nothing of its own; a repeated
// allocate/destroy cycle therefore costs exactly one allocation per iteration.
TEST(MemoryAllocGuard, ForOverwriteScopeExitFreesWithoutAllocating) {
  constexpr int kIterations = 8;
  constexpr size_t kBytes = 512;
  size_t allocations = 0;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    for (int i = 0; i < kIterations; ++i) {
      auto buf = makeUniqueNoThrowForOverwrite<uint8_t[]>(kBytes);
      escape(buf.get());
      buf[0] = static_cast<uint8_t>(i);
    }
    allocations = scope.count();
    bytes = scope.bytes();
  }

  EXPECT_EQ(allocations, static_cast<size_t>(kIterations));
  EXPECT_EQ(bytes, kIterations * kBytes);
}

// An unsatisfiable request must come back as nullptr (the device's OOM contract) rather than
// aborting or wrapping around to a short buffer.
TEST(MemoryAllocGuard, ForOverwriteReturnsNullForAnUnsatisfiableRequest) {
  auto absurd = makeUniqueNoThrowForOverwrite<uint8_t[]>(SIZE_MAX / 2);
  EXPECT_EQ(absurd, nullptr);
  auto overflowing = makeUniqueNoThrowForOverwrite<uint32_t[]>(SIZE_MAX / 2 + 1);
  EXPECT_EQ(overflowing, nullptr);
}

// The count check must happen before the new-expression, not inside the allocation function:
// on libstdc++ an unrepresentable count makes the new-expression itself throw
// bad_array_new_length, which under -fno-exceptions aborts. Zero operator-new[] calls is the
// portable evidence that the guard ran first; both array overloads carry it.
TEST(MemoryAllocGuard, OverflowingArrayCountsAreRejectedWithoutReachingTheAllocator) {
  constexpr size_t kOverflowing = PTRDIFF_MAX / sizeof(uint64_t) + 1;
  size_t allocations = 0;
  bool bothNull = false;
  {
    alloc_counter::CountingScope scope;
    auto zeroed = makeUniqueNoThrow<uint64_t[]>(kOverflowing);
    auto raw = makeUniqueNoThrowForOverwrite<uint64_t[]>(kOverflowing);
    bothNull = zeroed == nullptr && raw == nullptr;
    allocations = scope.count();
  }

  EXPECT_TRUE(bothNull);
  EXPECT_EQ(allocations, 0u);
}

// ---------------------------------------------------------------------------------------------
// Coarse wall-time ceilings — DISABLED BY DEFAULT. Set CROSSPOINT_PERF_TIME=1 to enable.
// Ceilings are set orders of magnitude above measured times so they only catch complexity-class
// regressions (e.g. reintroducing the quadratic leading-punctuation erase), never CI jitter.
// ---------------------------------------------------------------------------------------------

TEST(WallTimeCeilingGuard, HyphenationThroughputCoarseCeiling) {
  if (!wallTimeCeilingsEnabled()) {
    GTEST_SKIP() << "wall-time ceilings are opt-in; set CROSSPOINT_PERF_TIME=1 to run";
  }

  Hyphenator::setPreferredLanguage("en");
  const auto& words = englishHyphenatableWords();

  size_t totalBreaks = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int pass = 0; pass < 2000; ++pass) {  // 20k breakOffsets calls ~ a few large chapters
    for (const auto& word : words) {
      totalBreaks += Hyphenator::breakOffsets(word, /*includeFallback=*/false).size();
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  EXPECT_GT(totalBreaks, 0u);
  // Measured 2026-09-13, macOS arm64 host: ~7 ms. Ceiling ~285x above baseline.
  EXPECT_LT(elapsed.count(), 2000) << "20k hyphenation calls took " << elapsed.count() << " ms";
}

TEST(WallTimeCeilingGuard, TrimLeadingPunctuationCoarseCeiling) {
  if (!wallTimeCeilingsEnabled()) {
    GTEST_SKIP() << "wall-time ceilings are opt-in; set CROSSPOINT_PERF_TIME=1 to run";
  }

  // 300k leading punctuation codepoints ahead of one letter. The single-shift bulk erase handles
  // this in ~1 ms; the pre-optimization erase(begin()) loop shifted the whole tail once per
  // punctuation codepoint (O(n^2), tens of seconds at this size).
  std::vector<CodepointInfo> cps;
  constexpr size_t kLeadingPunctuation = 300000;
  cps.reserve(kLeadingPunctuation + 1);
  for (size_t i = 0; i < kLeadingPunctuation; ++i) {
    cps.push_back({'.', i});
  }
  cps.push_back({'a', kLeadingPunctuation});

  const auto start = std::chrono::steady_clock::now();
  trimSurroundingPunctuationAndFootnote(cps);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  EXPECT_EQ(codepointValues(cps), (std::vector<uint32_t>{'a'}));
  // Measured 2026-09-13, macOS arm64 host: <1 ms. Ceiling >1000x above baseline.
  EXPECT_LT(elapsed.count(), 1000) << "bulk punctuation trim took " << elapsed.count() << " ms";
}

// Self-check of the live/peak tracking the chapter-memory tests rely on. The volatile sink stops the
// compiler from eliding the new/delete pairs, which it may do for replaceable allocation functions.
TEST(AllocCounterSelfCheck, LiveReturnsToZeroAndPeakKeepsTheHighWaterMark) {
  static char* volatile sink = nullptr;
  static char* volatile early = nullptr;
  early = new char[64];  // allocated before counting: freeing it must not move liveBytes()
  size_t liveAfterFree = 1;
  size_t peak = 0;
  size_t liveAfterEarlyFree = 1;
  {
    alloc_counter::CountingScope scope;
    sink = new char[100];
    delete[] sink;
    liveAfterFree = alloc_counter::liveBytes();
    peak = alloc_counter::peakBytes();
    delete[] early;
    liveAfterEarlyFree = alloc_counter::liveBytes();
  }
  EXPECT_EQ(liveAfterFree, 0u);
  EXPECT_GE(peak, 100u);
  EXPECT_EQ(liveAfterEarlyFree, 0u);
}
