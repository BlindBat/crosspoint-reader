// Host tests for src/util/Dictionary.cpp — the StarDict reader that looks up
// words in an untrusted, user-installed .idx / .syn / .dict(.dz) set via a
// sampled-offset sidecar and a bounded linear scan.
//
// Fixtures come from scripts/generate_test_dict.py (committed generator +
// committed outputs in test/dict_common/resources). Each test installs the
// fixture files it needs into its own sandbox directory; the HalStorage stub
// remaps the firmware's device-absolute paths ("/dictionaries/...",
// "/.crosspoint/dict.tmp") under that sandbox.

#include <Arduino.h>  // host stub: dictstub::HeapLimitScope drives the heap guards
#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "src/util/Dictionary.h"
#include "test/support/AllocCounter.h"

namespace {

namespace fs = std::filesystem;

using LookupResult = Dictionary::LookupResult;
using IndexResult = Dictionary::IndexResult;

std::string resPath(const std::string& name) { return std::string(DICT_RESOURCES_DIR) + "/" + name; }

// Payload formulas — must match scripts/generate_test_dict.py.
std::string definitionText(const std::string& word) { return word + ": definition text for " + word + ".\n"; }

std::string boundaryDefinition() {
  std::string out;
  out.reserve(5004);
  char buf[16];
  for (int i = 0; i < 556; i++) {
    std::snprintf(buf, sizeof(buf), "%08d-", i);
    out += buf;
  }
  return out;
}

// Result of one Dictionary::lookup call, gathered so tests can assert on the
// hit flag, the definition, the matched headword and the reported reason.
struct Outcome {
  bool hit = false;
  std::string def;
  std::string headword;
  LookupResult result = LookupResult::NotFound;
};

Outcome doLookup(Dictionary& dict, const char* word) {
  Outcome o;
  o.hit = dict.lookup(word, o.def, o.headword, &o.result);
  return o;
}

class DictionaryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string(info->test_suite_name()) + "_" + info->name();
    for (char& c : name) {
      if (c == '/') c = '_';
    }
    root = fs::absolute("sandbox_" + name);
    fs::remove_all(root);
    fs::create_directories(root / "dictionaries");
    fs::create_directories(root / ".crosspoint");
    dictstub::storageRoot = root.string();
  }

  void TearDown() override {
    dictstub::storageRoot.clear();
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
      fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ec);
    }
    fs::remove_all(root, ec);
  }

  // Copy a resource fixture into /dictionaries/<folder>/<asName>.
  void install(const std::string& folder, const std::string& resource, const std::string& asName) {
    fs::create_directories(root / "dictionaries" / folder);
    fs::copy_file(resPath(resource), root / "dictionaries" / folder / asName, fs::copy_options::overwrite_existing);
  }

  void writeText(const std::string& folder, const std::string& name, const std::string& content) {
    fs::create_directories(root / "dictionaries" / folder);
    std::ofstream out(root / "dictionaries" / folder / name, std::ios::binary);
    out << content;
  }

  fs::path dictFile(const std::string& folder, const std::string& name) const {
    return root / "dictionaries" / folder / name;
  }

  // Install the small fixture dictionary; data via .dict.dz when dz, else .dict.
  void installSmall(bool dz, bool withSyn = true) {
    install("en", "small.idx", "small.idx");
    install("en", dz ? "small.dict.dz" : "small.dict", dz ? "small.dict.dz" : "small.dict");
    if (withSyn) install("en", "small.syn", "small.syn");
  }

  fs::path root;
};

// ---------------------------------------------------------------------------
// open(): file validation
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, OpenFailsForMissingFolder) {
  Dictionary dict;
  EXPECT_FALSE(dict.open("nope"));
  EXPECT_FALSE(dict.isOpen());
}

TEST_F(DictionaryTest, OpenFailsWithoutIdx) {
  install("en", "small.dict", "small.dict");
  Dictionary dict;
  EXPECT_FALSE(dict.open("en"));
}

TEST_F(DictionaryTest, OpenFailsWithoutDictData) {
  install("en", "small.idx", "small.idx");
  Dictionary dict;
  EXPECT_FALSE(dict.open("en"));
}

TEST_F(DictionaryTest, OpenSucceedsWithPlainDict) {
  installSmall(false);
  Dictionary dict;
  EXPECT_TRUE(dict.open("en"));
  EXPECT_TRUE(dict.isOpen());
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(DictionaryTest, OpenSucceedsWithOnlyDictDz) {
  installSmall(true);
  Dictionary dict;
  EXPECT_TRUE(dict.open("en"));
}

TEST_F(DictionaryTest, OpenRejects64BitIndexOffsets) {
  installSmall(false);
  writeText("en", "small.ifo", "StarDict's dict ifo file\nversion=3.0.0\nidxoffsetbits=64\n");
  Dictionary dict;
  EXPECT_FALSE(dict.open("en"));
}

TEST_F(DictionaryTest, HtmlDefinitionsFlagFromIfo) {
  installSmall(false);
  writeText("en", "small.ifo", "StarDict's dict ifo file\nversion=3.0.0\nsametypesequence=h\n");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  EXPECT_TRUE(dict.definitionsAreHtml());
}

TEST_F(DictionaryTest, MultiTypeSequenceIsNotHtml) {
  installSmall(false);
  writeText("en", "small.ifo", "StarDict's dict ifo file\nsametypesequence=hx\n");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(DictionaryTest, AmbiguousFolderWithTwoStemsFails) {
  install("en", "small.idx", "small.idx");
  install("en", "small.dict", "small.dict");
  install("en", "big.idx", "other.idx");
  Dictionary dict;
  EXPECT_FALSE(dict.open("en"));
}

// ---------------------------------------------------------------------------
// needsIndex() / buildIndex() lifecycle
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, IndexLifecycle) {
  installSmall(false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  EXPECT_TRUE(dict.needsIndex());

  IndexResult ir = IndexResult::ReadError;
  EXPECT_TRUE(dict.buildIndex(nullptr, nullptr, &ir));
  EXPECT_EQ(ir, IndexResult::Ok);
  EXPECT_FALSE(dict.needsIndex());
  EXPECT_TRUE(fs::exists(dictFile("en", "small.qidx")));
  EXPECT_TRUE(fs::exists(dictFile("en", "small.sidx")));

  // A different-size .idx makes the sidecar stale again.
  fs::copy_file(resPath("small_truncated.idx"), dictFile("en", "small.idx"), fs::copy_options::overwrite_existing);
  EXPECT_TRUE(dict.needsIndex());
}

TEST_F(DictionaryTest, CorruptSidecarTriggersReindexAndLookupStillWorks) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());
  ASSERT_FALSE(dict.needsIndex());

  // Stomp the sidecar header: openSession must ignore it (magic mismatch) and
  // fall back to a full scan, while needsIndex() reports it stale.
  {
    std::ofstream out(dictFile("en", "small.qidx"), std::ios::binary);
    out << "GARBAGEGARBAGEGARBAGE";
  }
  EXPECT_TRUE(dict.needsIndex());
  const Outcome o = doLookup(dict, "banana");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("banana"));
}

TEST_F(DictionaryTest, BuildIndexIsNoopWhenIdxVanished) {
  // Pin: a vanished .idx is "not stale" (nothing to rebuild from), so
  // buildIndex succeeds as a no-op rather than reporting an error.
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  fs::remove(dictFile("en", "small.idx"));
  IndexResult ir = IndexResult::ReadError;
  EXPECT_TRUE(dict.buildIndex(nullptr, nullptr, &ir));
  EXPECT_EQ(ir, IndexResult::Ok);
}

TEST_F(DictionaryTest, BuildIndexReportsReadErrorWhenSidecarUnwritable) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  // Read-only folder: the .idx still opens for read but the .qidx can't be
  // created next to it.
  const std::string folder = (root / "dictionaries" / "en").string();
  ::chmod(folder.c_str(), 0555);
  IndexResult ir = IndexResult::Ok;
  EXPECT_FALSE(dict.buildIndex(nullptr, nullptr, &ir));
  EXPECT_EQ(ir, IndexResult::ReadError);
  ::chmod(folder.c_str(), 0755);
}

TEST_F(DictionaryTest, BuildIndexAllocationIsBoundedOnBigIndex) {
  // The index scan streams: 701 entries must not cost more than the fixed 4KB
  // scan buffer plus small change, no matter how many entries the file holds.
  install("en", "big.idx", "big.idx");
  install("en", "big.dict", "big.dict");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  bool ok = false;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    ok = dict.buildIndex();
    bytes = scope.bytes();
  }
  EXPECT_TRUE(ok);
  EXPECT_LT(bytes, 32u * 1024u);
}

// ---------------------------------------------------------------------------
// lookup(): parameterized over the data backend (.dict vs .dict.dz)
// ---------------------------------------------------------------------------

class SmallDictLookupTest : public DictionaryTest, public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    DictionaryTest::SetUp();
    installSmall(GetParam());
    ASSERT_TRUE(dict.open("en"));
    ASSERT_TRUE(dict.buildIndex());
  }

  Dictionary dict;
};

TEST_P(SmallDictLookupTest, ExactMatch) {
  const Outcome o = doLookup(dict, "banana");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.result, LookupResult::Found);
  EXPECT_EQ(o.def, definitionText("banana"));
  EXPECT_EQ(o.headword, "banana");
}

TEST_P(SmallDictLookupTest, FirstEntryMatchesWithStoredCase) {
  // The query is lowercased by cleanWord; the index stores "Apple". The hit
  // must report the headword exactly as stored.
  const Outcome o = doLookup(dict, "apple");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("Apple"));
  EXPECT_EQ(o.headword, "Apple");
}

TEST_P(SmallDictLookupTest, LastEntry) {
  const Outcome o = doLookup(dict, "zebra");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("zebra"));
}

TEST_P(SmallDictLookupTest, UppercaseQueryMatches) {
  const Outcome o = doLookup(dict, "WALK");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("walk"));
}

TEST_P(SmallDictLookupTest, MissBeforeFirstEntry) {
  const Outcome o = doLookup(dict, "aaa");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_P(SmallDictLookupTest, MissBetweenEntries) {
  const Outcome o = doLookup(dict, "grape");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_P(SmallDictLookupTest, MissAfterLastEntry) {
  const Outcome o = doLookup(dict, "zzzz");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_P(SmallDictLookupTest, PunctuationAndCurlyQuotesStripped) {
  // EPUB text arrives with attached punctuation: "dog." and “dog” (U+201C/1D).
  EXPECT_TRUE(doLookup(dict, "dog.").hit);
  EXPECT_TRUE(doLookup(dict, "\xE2\x80\x9C\x64og\xE2\x80\x9D").hit);
}

TEST_P(SmallDictLookupTest, StemmingFallbacks) {
  EXPECT_EQ(doLookup(dict, "walked").def, definitionText("walk"));           // -ed
  EXPECT_EQ(doLookup(dict, "stories").def, definitionText("story"));         // -ies -> y
  EXPECT_EQ(doLookup(dict, "boxes").def, definitionText("box"));             // -es
  EXPECT_EQ(doLookup(dict, "running").def, definitionText("run"));           // doubled consonant + -ing
  EXPECT_EQ(doLookup(dict, "loved").def, definitionText("love"));            // -d
  EXPECT_EQ(doLookup(dict, "stopped").def, definitionText("stop"));          // doubled consonant + -ed
  EXPECT_EQ(doLookup(dict, "walk's").def, definitionText("walk"));           // -'s
  EXPECT_EQ(doLookup(dict, "dog\xE2\x80\x99s").def, definitionText("dog"));  // curly -’s
}

TEST_P(SmallDictLookupTest, SynonymResolvesToHeadword) {
  const Outcome o = doLookup(dict, "colour");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("color"));
  EXPECT_EQ(o.headword, "color");
}

TEST_P(SmallDictLookupTest, SynonymOrdinalsResolveAcrossTheIndex) {
  EXPECT_EQ(doLookup(dict, "pup").headword, "dog");
  EXPECT_EQ(doLookup(dict, "sprint").headword, "run");
}

TEST_P(SmallDictLookupTest, SynonymTakesPrecedenceOverStemmer) {
  // The fixture maps "dogs" to zebra; the -s stemmer would find "dog". The
  // dictionary-authored synonym must win.
  const Outcome o = doLookup(dict, "dogs");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.headword, "zebra");
  EXPECT_EQ(o.def, definitionText("zebra"));
}

TEST_P(SmallDictLookupTest, GarbageInputsAreCleanMisses) {
  EXPECT_FALSE(doLookup(dict, "").hit);
  EXPECT_FALSE(doLookup(dict, "...").hit);
  EXPECT_FALSE(doLookup(dict, nullptr).hit);
}

INSTANTIATE_TEST_SUITE_P(Backends, SmallDictLookupTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? std::string("DictZip") : std::string("Plain");
                         });

// ---------------------------------------------------------------------------
// lookup() without a built sidecar
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, LookupWithoutSidecarScansFromStart) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  // No buildIndex(): locate() must fall back to scanning the .idx from byte 0.
  const Outcome o = doLookup(dict, "zebra");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("zebra"));
}

TEST_F(DictionaryTest, MissWithUnindexedSynIsReadErrorNotMiss) {
  // Pin: when a .syn exists but its .sidx hasn't been built, the synonym probe
  // never reaches a verdict, so a miss is reported as ReadError (unfinished
  // search), not NotFound. The intended flow builds the index first.
  installSmall(false, /*withSyn=*/true);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  const Outcome o = doLookup(dict, "grape");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

TEST_F(DictionaryTest, StaleSidecarIsIgnoredByLookup) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());
  // Swap in a different-size .idx: the sidecar's sourceFileSize no longer
  // matches, so openSession must ignore it and scan from the start.
  fs::copy_file(resPath("small_truncated.idx"), dictFile("en", "small.idx"), fs::copy_options::overwrite_existing);
  EXPECT_TRUE(dict.needsIndex());
  const Outcome o = doLookup(dict, "banana");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("banana"));
}

// ---------------------------------------------------------------------------
// Big dictionary: multi-sample bisect + a definition spanning many dz chunks
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, BigDictBisectAcrossSamples) {
  install("en", "big.idx", "big.idx");
  install("en", "big.dict.dz", "big.dict.dz");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());  // 701 entries -> samples at 0, 256, 512

  EXPECT_EQ(doLookup(dict, "w000").def, definitionText("w000"));  // first sample region
  EXPECT_EQ(doLookup(dict, "w255").def, definitionText("w255"));  // last entry before sample 1
  EXPECT_EQ(doLookup(dict, "w256").def, definitionText("w256"));  // first entry of sample 1
  EXPECT_EQ(doLookup(dict, "w399").def, definitionText("w399"));  // middle
  EXPECT_EQ(doLookup(dict, "w511").def, definitionText("w511"));  // sample 2 boundary
  EXPECT_EQ(doLookup(dict, "w699").def, definitionText("w699"));  // last entry

  const Outcome miss = doLookup(dict, "w700");
  EXPECT_FALSE(miss.hit);
  EXPECT_EQ(miss.result, LookupResult::NotFound);
}

TEST_F(DictionaryTest, DefinitionSpanningManyChunksIsByteExact) {
  install("en", "big.idx", "big.idx");
  install("en", "big.dict.dz", "big.dict.dz");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "boundary");
  ASSERT_TRUE(o.hit);
  EXPECT_EQ(o.def.size(), 5004u);
  EXPECT_EQ(o.def, boundaryDefinition());  // ~10 dictzip chunks, byte-exact
}

// ---------------------------------------------------------------------------
// Malformed .idx variants
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, UnsortedIndexPinnedBehavior) {
  // Pin: the reader assumes a sorted .idx (StarDict guarantees it). With
  // banana/box swapped, the scan stops at the first headword that compares
  // greater, so the out-of-place "banana" is unreachable — a clean miss, never
  // a crash — while "box" (moved earlier) is still found.
  install("en", "small_unsorted.idx", "small.idx");
  install("en", "small.dict", "small.dict");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  const Outcome miss = doLookup(dict, "banana");
  EXPECT_FALSE(miss.hit);
  EXPECT_EQ(miss.result, LookupResult::NotFound);

  const Outcome hit = doLookup(dict, "box");
  EXPECT_TRUE(hit.hit);
  EXPECT_EQ(hit.def, definitionText("box"));
}

TEST_F(DictionaryTest, TruncatedIndexEntryPinnedBehavior) {
  // The last entry's suffix is cut short. Pin: a truncated tail reads as end
  // of index (documented: EOF and IO error are indistinguishable here), so the
  // damaged entry is a NotFound — not a ReadError — and earlier entries work.
  install("en", "small_truncated.idx", "small.idx");
  install("en", "small.dict", "small.dict");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  const Outcome miss = doLookup(dict, "zebra");
  EXPECT_FALSE(miss.hit);
  EXPECT_EQ(miss.result, LookupResult::NotFound);

  EXPECT_TRUE(doLookup(dict, "banana").hit);
  EXPECT_TRUE(doLookup(dict, "walk").hit);
}

class EvilIdxTest : public DictionaryTest, public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    DictionaryTest::SetUp();
    const bool dz = GetParam();
    install("en", "small_evil.idx", "small.idx");
    install("en", dz ? "small.dict.dz" : "small.dict", dz ? "small.dict.dz" : "small.dict");
    ASSERT_TRUE(dict.open("en"));
    ASSERT_TRUE(dict.buildIndex());
  }

  Dictionary dict;
};

TEST_P(EvilIdxTest, OffsetPastEofIsReadError) {
  // "dog" declares offset 1MB into a ~360-byte data file.
  const Outcome o = doLookup(dict, "dog");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

TEST_P(EvilIdxTest, HugeDeclaredSizeIsReadErrorWithBoundedAllocation) {
  // "love" declares a ~4GB definition. The reader clamps to
  // MAX_DEFINITION_BYTES and then rejects the range against the real data
  // size — without ever allocating anything attacker-sized.
  Outcome o;
  size_t bytes = 0;
  {
    alloc_counter::CountingScope scope;
    o = doLookup(dict, "love");
    bytes = scope.bytes();
  }
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
  EXPECT_LT(bytes, 64u * 1024u);
}

TEST_P(EvilIdxTest, ZeroSizeEntryIsReadError) {
  const Outcome o = doLookup(dict, "run");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

TEST_P(EvilIdxTest, IntactEntriesStillResolve) {
  const Outcome o = doLookup(dict, "banana");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, definitionText("banana"));
}

INSTANTIATE_TEST_SUITE_P(Backends, EvilIdxTest, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? std::string("DictZip") : std::string("Plain");
                         });

// ---------------------------------------------------------------------------
// Malformed .syn variants
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, SynOrdinalPastEntryCountIsCleanMiss) {
  install("en", "small.idx", "small.idx");
  install("en", "small.dict", "small.dict");
  install("en", "small_evil.syn", "small.syn");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  // "ghost" maps to ordinal 9999 of an 11-entry index: bounds-checked miss.
  const Outcome o = doLookup(dict, "ghost");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);

  // The valid synonym in the same damaged file still resolves.
  EXPECT_EQ(doLookup(dict, "colour").headword, "color");
}

TEST_F(DictionaryTest, SynEntryTruncatedMidOrdinalIsCleanMiss) {
  install("en", "small.idx", "small.idx");
  install("en", "small.dict", "small.dict");
  install("en", "small_evil.syn", "small.syn");
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  // The .syn tail is "zz" + 2 of 4 ordinal bytes.
  const Outcome o = doLookup(dict, "zz");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

// ---------------------------------------------------------------------------
// Data-file failures surfaced through lookup()
// ---------------------------------------------------------------------------

TEST_F(DictionaryTest, CorruptDzReportsDecompress) {
  install("en", "small.idx", "small.idx");
  install("en", "small_nora.dz", "small.dict.dz");  // plain gzip, no RA table
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "banana");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::Decompress);
}

TEST_F(DictionaryTest, LowHeapReportsLowMemory) {
  installSmall(true);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());

  dictstub::HeapLimitScope heap(1000);  // below size + 8KB definition headroom
  const Outcome o = doLookup(dict, "banana");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::LowMemory);
}

TEST_F(DictionaryTest, VanishedIdxAtLookupIsReadError) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());
  fs::remove(dictFile("en", "small.idx"));

  // The search never reached a verdict — must be ReadError, not a miss.
  const Outcome o = doLookup(dict, "banana");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

TEST_F(DictionaryTest, VanishedDataFileAtLookupIsReadError) {
  installSmall(false, /*withSyn=*/false);
  Dictionary dict;
  ASSERT_TRUE(dict.open("en"));
  ASSERT_TRUE(dict.buildIndex());
  fs::remove(dictFile("en", "small.dict"));

  const Outcome o = doLookup(dict, "banana");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

// ---------------------------------------------------------------------------
// cleanWord(): static input scrubbing
// ---------------------------------------------------------------------------

TEST(CleanWordTest, LowercasesAsciiAndStripsEdgePunctuation) {
  EXPECT_EQ(Dictionary::cleanWord("Hello!"), "hello");
  EXPECT_EQ(Dictionary::cleanWord("(WORD)"), "word");
  EXPECT_EQ(Dictionary::cleanWord("dog."), "dog");
}

TEST(CleanWordTest, StripsCurlyQuotesAndDashesFromEdges) {
  EXPECT_EQ(Dictionary::cleanWord("\xE2\x80\x9C\x67\x61rage.\xE2\x80\x9D"), "garage");  // “garage.”
  EXPECT_EQ(Dictionary::cleanWord("\xE2\x80\x94\x64\x61sh\xE2\x80\x94"), "dash");       // —dash—
}

TEST(CleanWordTest, KeepsInnerApostrophesAndUtf8) {
  EXPECT_EQ(Dictionary::cleanWord("don\xE2\x80\x99t"), "don\xE2\x80\x99t");  // don’t
  EXPECT_EQ(Dictionary::cleanWord("caf\xC3\xA9"), "caf\xC3\xA9");            // café: no tolower on UTF-8
  EXPECT_EQ(Dictionary::cleanWord("42nd"), "42nd");
}

TEST(CleanWordTest, DegenerateInputs) {
  EXPECT_EQ(Dictionary::cleanWord(nullptr), "");
  EXPECT_EQ(Dictionary::cleanWord(""), "");
  EXPECT_EQ(Dictionary::cleanWord("..."), "");
  EXPECT_EQ(Dictionary::cleanWord("\xE2\x80\x9C\xE2\x80\x9D"), "");  // only curly quotes
  EXPECT_EQ(Dictionary::cleanWord("\xE2\x80\x99s"), "s");            // leading curly apostrophe
}

}  // namespace
