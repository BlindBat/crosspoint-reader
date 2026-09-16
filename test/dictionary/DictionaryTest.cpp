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

// ---------------------------------------------------------------------------
// .ifo edge cases (only idxoffsetbits and sametypesequence, first 2KB)
// ---------------------------------------------------------------------------

namespace {

class IfoTest : public DictionaryTest {
 protected:
  // Install the small plain dictionary plus the given .ifo text and open it.
  bool openWithIfo(const std::string& ifo) {
    installSmall(false, /*withSyn=*/false);
    writeText("en", "small.ifo", ifo);
    return dict.open("en");
  }

  Dictionary dict;
};

TEST_F(IfoTest, MissingIfoOpensAsPlainText) {
  installSmall(false, /*withSyn=*/false);
  ASSERT_TRUE(dict.open("en"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, EmptyIfoOpensAsPlainText) {
  ASSERT_TRUE(openWithIfo(""));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, IfoThatIsADirectoryIsIgnored) {
  installSmall(false, /*withSyn=*/false);
  fs::create_directories(dictFile("en", "small.ifo"));
  ASSERT_TRUE(dict.open("en"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, ThirtyTwoBitOffsetsAccepted) {
  EXPECT_TRUE(openWithIfo("StarDict's dict ifo file\nversion=3.0.0\nidxoffsetbits=32\n"));
}

TEST_F(IfoTest, SixtyFourBitOffsetsRejectedEvenWithSpacesAroundValue) {
  EXPECT_FALSE(openWithIfo("version=3.0.0\nidxoffsetbits= 64\nbookname=x\n"));
}

TEST_F(IfoTest, KeysBeyondTheFirst2KBAreNotSeen) {
  // Pin: the scan window is 2047 bytes; a late idxoffsetbits=64 is not
  // enforced, and a late sametypesequence=h leaves the plain-text path.
  const std::string padding(2100, '#');
  ASSERT_TRUE(openWithIfo("version=3.0.0\n" + padding + "\nidxoffsetbits=64\nsametypesequence=h\n"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, HtmlFlagRequiresExactlyH) {
  ASSERT_TRUE(openWithIfo("sametypesequence=h"));  // at EOF, no newline
  EXPECT_TRUE(dict.definitionsAreHtml());

  Dictionary crlf;
  writeText("en", "small.ifo", "sametypesequence=h\r\nbookname=x\n");
  ASSERT_TRUE(crlf.open("en"));
  EXPECT_TRUE(crlf.definitionsAreHtml());

  Dictionary upper;
  writeText("en", "small.ifo", "sametypesequence=H\n");
  ASSERT_TRUE(upper.open("en"));
  EXPECT_FALSE(upper.definitionsAreHtml());

  Dictionary multi;
  writeText("en", "small.ifo", "sametypesequence=hm\n");
  ASSERT_TRUE(multi.open("en"));
  EXPECT_FALSE(multi.definitionsAreHtml());

  Dictionary empty;
  writeText("en", "small.ifo", "sametypesequence=\n");
  ASSERT_TRUE(empty.open("en"));
  EXPECT_FALSE(empty.definitionsAreHtml());

  Dictionary spaced;
  writeText("en", "small.ifo", "sametypesequence= h\n");
  ASSERT_TRUE(spaced.open("en"));
  EXPECT_FALSE(spaced.definitionsAreHtml());
}

TEST_F(IfoTest, HtmlFlagIsResetBetweenOpens) {
  ASSERT_TRUE(openWithIfo("sametypesequence=h\n"));
  ASSERT_TRUE(dict.definitionsAreHtml());
  writeText("en", "small.ifo", "sametypesequence=m\n");
  ASSERT_TRUE(dict.open("en"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, BinaryGarbageIfoIsHarmless) {
  std::string junk(1500, '\0');
  for (size_t i = 0; i < junk.size(); i++) junk[i] = static_cast<char>((i * 31) & 0xFF);
  ASSERT_TRUE(openWithIfo(junk));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, KeyWithoutEqualsBorrowsTheNextLinesValue) {
  // Pinned (deviation from the contract's per-key reading): readIfoFacts finds
  // the '=' with strchr from the key, which does not stop at the end of the
  // key's line, so a valueless "idxoffsetbits" followed by "wordcount=64"
  // reads as 64-bit offsets and the dictionary is refused.
  EXPECT_FALSE(openWithIfo("idxoffsetbits\nwordcount=64\n"));
  // With no '=' anywhere after it the key is simply ignored.
  EXPECT_TRUE(openWithIfo("idxoffsetbits\nwordcount 64\n"));
}

TEST_F(IfoTest, ValuelessSameTypeSequenceBorrowsTheNextLinesValue) {
  // Same mechanism on the HTML flag: "sametypesequence" with no '=' picks up
  // the next line's "=h".
  ASSERT_TRUE(openWithIfo("sametypesequence\nbookname=h\n"));
  EXPECT_TRUE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, DuplicateKeysUseTheFirstOccurrence) {
  EXPECT_TRUE(openWithIfo("idxoffsetbits=32\nidxoffsetbits=64\n"));
  EXPECT_FALSE(openWithIfo("idxoffsetbits=64\nidxoffsetbits=32\n"));

  ASSERT_TRUE(openWithIfo("sametypesequence=m\nsametypesequence=h\n"));
  EXPECT_FALSE(dict.definitionsAreHtml());
  ASSERT_TRUE(openWithIfo("sametypesequence=h\nsametypesequence=m\n"));
  EXPECT_TRUE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, AbsurdOffsetBitsValuesAreNotSixtyFour) {
  // strtol base 10: only a value that parses to exactly 64 is rejected.
  EXPECT_TRUE(openWithIfo("idxoffsetbits=6400\n"));
  EXPECT_TRUE(openWithIfo("idxoffsetbits=-64\n"));
  EXPECT_TRUE(openWithIfo("idxoffsetbits=0x40\n"));                      // hex is not parsed
  EXPECT_TRUE(openWithIfo("idxoffsetbits=\n"));                          // no value at all
  EXPECT_TRUE(openWithIfo("idxoffsetbits=999999999999999999999999\n"));  // saturates
  EXPECT_TRUE(openWithIfo("idxoffsetbits=sixtyfour\n"));
}

TEST_F(IfoTest, OffsetBitsValueIsParsedWithATrailingSuffix) {
  // Pinned: strtol stops at the first non-digit, so "64bit" is still 64.
  EXPECT_FALSE(openWithIfo("idxoffsetbits=64bit\n"));
  EXPECT_FALSE(openWithIfo("idxoffsetbits=\t64\n"));
  EXPECT_FALSE(openWithIfo("idxoffsetbits=064\n"));
}

TEST_F(IfoTest, KeyMatchesAsASubstringOfALongerKey) {
  // Pinned: strstr has no key-boundary rule, so a vendor key that merely ends
  // with "idxoffsetbits" is honoured.
  EXPECT_FALSE(openWithIfo("xidxoffsetbits=64\n"));
  ASSERT_TRUE(openWithIfo("my_sametypesequence=h\n"));
  EXPECT_TRUE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, AnEmbeddedNulHidesEverythingAfterIt) {
  // Pinned: the 2KB window is NUL-terminated before scanning, so a NUL early
  // in the file hides the keys behind it.
  const std::string hidden = std::string("version=3.0.0\n") + '\0' + "idxoffsetbits=64\nsametypesequence=h\n";
  ASSERT_TRUE(openWithIfo(hidden));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

TEST_F(IfoTest, KeysOnTheSameLineAreBothRead) {
  ASSERT_TRUE(openWithIfo("sametypesequence=h idxoffsetbits=32\n"));
  EXPECT_FALSE(dict.definitionsAreHtml());  // "h " -> eq[2] is a space, not a terminator
  EXPECT_FALSE(openWithIfo("bookname=x idxoffsetbits=64\n"));
}

TEST_F(IfoTest, AFullRealisticIfoWithNeitherKeyOpensAsPlainText) {
  ASSERT_TRUE(
      openWithIfo("StarDict's dict ifo file\nversion=2.4.2\nbookname=Test\nwordcount=11\nidxfilesize=200\n"
                  "author=nobody\ndescription=fixture\ndate=2026.01.01\n"));
  EXPECT_FALSE(dict.definitionsAreHtml());
}

// ---------------------------------------------------------------------------
// Stem variants (through lookup: exact miss -> no .syn -> stems in order)
// ---------------------------------------------------------------------------

std::string be32(uint32_t v) {
  std::string s(4, '\0');
  s[0] = static_cast<char>(v >> 24);
  s[1] = static_cast<char>(v >> 16);
  s[2] = static_cast<char>(v >> 8);
  s[3] = static_cast<char>(v);
  return s;
}

class StemTest : public DictionaryTest {
 protected:
  // Build /dictionaries/st/st.idx + st.dict where each headword's definition
  // is the headword itself. Words are sorted here, as StarDict requires.
  void buildDict(std::vector<std::string> words) {
    std::sort(words.begin(), words.end());
    std::string idx, data;
    for (const auto& w : words) {
      idx += w;
      idx.push_back('\0');
      idx += be32(static_cast<uint32_t>(data.size()));
      idx += be32(static_cast<uint32_t>(w.size()));
      data += w;
    }
    writeText("st", "st.idx", idx);
    writeText("st", "st.dict", data);
    ASSERT_TRUE(dict.open("st"));
    ASSERT_TRUE(dict.buildIndex());
  }

  std::string headwordFor(const char* query) {
    const Outcome o = doLookup(dict, query);
    if (!o.hit) return "<miss:" + std::to_string(static_cast<int>(o.result)) + ">";
    EXPECT_EQ(o.def, o.headword);  // definition is the headword by construction
    return o.headword;
  }

  Dictionary dict;
};

TEST_F(StemTest, PossessiveSuffixes) {
  buildDict({"cat", "dog"});
  EXPECT_EQ(headwordFor("cat's"), "cat");
  EXPECT_EQ(headwordFor("dog\xE2\x80\x99s"), "dog");  // U+2019 apostrophe
}

TEST_F(StemTest, PluralSuffixes) {
  buildDict({"story", "box", "dog", "city"});
  EXPECT_EQ(headwordFor("stories"), "story");
  EXPECT_EQ(headwordFor("cities"), "city");
  EXPECT_EQ(headwordFor("boxes"), "box");
  EXPECT_EQ(headwordFor("dogs"), "dog");
}

TEST_F(StemTest, PastTenseSuffixes) {
  buildDict({"walk", "love", "stop"});
  EXPECT_EQ(headwordFor("walked"), "walk");
  EXPECT_EQ(headwordFor("loved"), "love");
  EXPECT_EQ(headwordFor("stopped"), "stop");
}

TEST_F(StemTest, ProgressiveSuffixes) {
  buildDict({"walk", "make", "run"});
  EXPECT_EQ(headwordFor("walking"), "walk");
  EXPECT_EQ(headwordFor("making"), "make");
  EXPECT_EQ(headwordFor("running"), "run");
}

TEST_F(StemTest, VariantOrderIsEsBeforeS) {
  // "hopes" tries -es ("hop") before -s ("hope"); with both present the
  // first variant wins.
  buildDict({"hop", "hope"});
  EXPECT_EQ(headwordFor("hopes"), "hop");
}

TEST_F(StemTest, EdVariantOrderIsStripTwoBeforeUndouble) {
  // "stopped" yields "stopp", "stoppe", "stop" in that order; with both
  // "stopp" and "stop" present the earlier variant wins.
  buildDict({"stopp", "stop"});
  EXPECT_EQ(headwordFor("stopped"), "stopp");
}

TEST_F(StemTest, ExactMatchBeatsStemming) {
  buildDict({"walk", "walked"});
  EXPECT_EQ(headwordFor("walked"), "walked");
}

TEST_F(StemTest, SuffixAloneHasNoVariants) {
  // endsWith() demands a non-empty remainder: "ed", "ing", "s" are looked up
  // as-is and miss cleanly.
  buildDict({"walk"});
  for (const char* q : {"ed", "ing", "es", "ies", "s", "'s"}) {
    const Outcome o = doLookup(dict, q);
    EXPECT_FALSE(o.hit) << q;
    EXPECT_EQ(o.result, LookupResult::NotFound) << q;
  }
}

TEST_F(StemTest, UnstemmableWordIsCleanMiss) {
  buildDict({"walk"});
  const Outcome o = doLookup(dict, "talked");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_F(StemTest, UppercaseQueryIsStemmedAfterLowercasing) {
  buildDict({"walk"});
  EXPECT_EQ(headwordFor("WALKING"), "walk");
}

// ---------------------------------------------------------------------------
// Sidecar build yield cadence (every 64KB of source consumed)
// ---------------------------------------------------------------------------

class YieldTest : public DictionaryTest {
 protected:
  // 16-byte .idx entries ("wNNNNNN\0" + offset + size) until totalBytes.
  std::string buildIdx(size_t totalBytes) {
    std::string idx;
    idx.reserve(totalBytes);
    char word[16];
    for (uint32_t i = 0; idx.size() + 16 <= totalBytes; i++) {
      std::snprintf(word, sizeof(word), "w%06u", static_cast<unsigned>(i));
      idx += word;
      idx.push_back('\0');
      idx += be32(0) + be32(1);
    }
    return idx;
  }

  // Same layout for .syn entries (word + 4-byte ordinal).
  std::string buildSyn(size_t totalBytes) {
    std::string syn;
    syn.reserve(totalBytes);
    char word[16];
    for (uint32_t i = 0; syn.size() + 12 <= totalBytes; i++) {
      std::snprintf(word, sizeof(word), "s%06u", static_cast<unsigned>(i));
      syn += word;
      syn.push_back('\0');
      syn += be32(0);
    }
    return syn;
  }

  struct Counter {
    int calls = 0;
  };
  static void countYield(void* ctx) { static_cast<Counter*>(ctx)->calls++; }

  Dictionary dict;
  Counter counter;
};

TEST_F(YieldTest, NoYieldBelow64KB) {
  writeText("y", "y.idx", buildIdx(60 * 1024));
  writeText("y", "y.dict", "x");
  ASSERT_TRUE(dict.open("y"));
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  EXPECT_EQ(counter.calls, 0);
}

TEST_F(YieldTest, YieldsOncePer64KBConsumed) {
  // 200000 bytes read in 4KB chunks: the counter trips after chunks 16, 32
  // and 48 (64KB each), never for the 3392-byte tail.
  writeText("y", "y.idx", buildIdx(200000));
  writeText("y", "y.dict", "x");
  ASSERT_TRUE(dict.open("y"));
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  EXPECT_EQ(counter.calls, 3);
}

TEST_F(YieldTest, ExactMultipleYieldsOnTheLastChunk) {
  writeText("y", "y.idx", buildIdx(128 * 1024));
  writeText("y", "y.dict", "x");
  ASSERT_TRUE(dict.open("y"));
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  EXPECT_EQ(counter.calls, 2);
}

TEST_F(YieldTest, SynPassYieldsOnItsOwnCadence) {
  // 70KB .idx (1 yield) + 140KB .syn (2 yields); the byte counter restarts
  // per sidecar pass rather than carrying the .idx remainder over.
  writeText("y", "y.idx", buildIdx(70 * 1024));
  writeText("y", "y.syn", buildSyn(140 * 1024));
  writeText("y", "y.dict", "x");
  ASSERT_TRUE(dict.open("y"));
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  EXPECT_EQ(counter.calls, 3);
}

TEST_F(YieldTest, FreshSidecarsMeanNoYields) {
  writeText("y", "y.idx", buildIdx(200000));
  writeText("y", "y.dict", "x");
  ASSERT_TRUE(dict.open("y"));
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  counter.calls = 0;
  ASSERT_TRUE(dict.buildIndex(&YieldTest::countYield, &counter));
  EXPECT_EQ(counter.calls, 0);
}

// ---------------------------------------------------------------------------
// Hand-built .idx / .syn / .dict byte cases: empty files, 255-byte and
// oversize headwords, embedded NULs, non-UTF8 bytes, unusable .sidx
// ---------------------------------------------------------------------------

std::string le32(uint32_t v) {
  std::string s(4, '\0');
  s[0] = static_cast<char>(v & 0xFF);
  s[1] = static_cast<char>((v >> 8) & 0xFF);
  s[2] = static_cast<char>((v >> 16) & 0xFF);
  s[3] = static_cast<char>((v >> 24) & 0xFF);
  return s;
}

class MalformedDictTest : public DictionaryTest {
 protected:
  // One .idx entry: headword NUL, BE32 data offset, BE32 data size.
  static std::string idxEntry(const std::string& word, uint32_t offset, uint32_t size) {
    return word + std::string(1, '\0') + be32(offset) + be32(size);
  }

  // One .syn entry: synonym NUL, BE32 ordinal into the .idx.
  static std::string synEntry(const std::string& word, uint32_t ordinal) {
    return word + std::string(1, '\0') + be32(ordinal);
  }

  // A sampled-offset sidecar header with a chosen sample count.
  static std::string sidecarHeader(uint32_t magic, uint32_t sourceSize, uint32_t sampleCount, uint32_t entryCount) {
    return le32(magic) + le32(2 /*version*/) + le32(256 /*interval*/) + le32(sampleCount) + le32(sourceSize) +
           le32(entryCount);
  }

  // Install /dictionaries/md/md.{idx,dict}(+ .syn) and open the dictionary.
  void installRaw(const std::string& idx, const std::string& data, const std::string* syn = nullptr) {
    writeText("md", "md.idx", idx);
    writeText("md", "md.dict", data);
    if (syn) writeText("md", "md.syn", *syn);
    ASSERT_TRUE(dict.open("md"));
  }

  Dictionary dict;
};

TEST_F(MalformedDictTest, EmptyIndexIndexesCleanlyAndEveryLookupMisses) {
  installRaw("", "payload");
  EXPECT_TRUE(dict.needsIndex());
  EXPECT_TRUE(dict.buildIndex());
  EXPECT_FALSE(dict.needsIndex());  // a zero-entry sidecar is still a valid one

  const Outcome o = doLookup(dict, "cat");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_F(MalformedDictTest, EmptyDataFileIsAReadError) {
  installRaw(idxEntry("cat", 0, 3), "");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "cat");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);
}

TEST_F(MalformedDictTest, BothIndexAndDataEmpty) {
  installRaw("", "");
  ASSERT_TRUE(dict.buildIndex());
  EXPECT_FALSE(doLookup(dict, "anything").hit);
}

TEST_F(MalformedDictTest, HeadwordOf255BytesMatchesExactly) {
  // The word buffer holds 256 bytes, so 255 is the longest exact headword.
  const std::string word(255, 'a');
  installRaw(idxEntry(word, 0, 5), "hello");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, word.c_str());
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.headword, word);
  EXPECT_EQ(o.def, "hello");
}

TEST_F(MalformedDictTest, HeadwordOver255BytesIsTruncatedForComparison) {
  // Pinned (and documented in the contract): the stored headword is cut to
  // 255 bytes, so only that prefix can ever match.
  const std::string word(300, 'b');
  installRaw(idxEntry(word, 0, 3), "def");
  ASSERT_TRUE(dict.buildIndex());

  EXPECT_FALSE(doLookup(dict, word.c_str()).hit);

  const Outcome prefix = doLookup(dict, std::string(255, 'b').c_str());
  EXPECT_TRUE(prefix.hit);
  EXPECT_EQ(prefix.headword.size(), 255u);
  EXPECT_EQ(prefix.def, "def");
}

TEST_F(MalformedDictTest, OversizeHeadwordKeepsTheScanInSync) {
  // Reading past a 300-byte headword must land exactly on its BE32 pair, or
  // every later entry would be misparsed.
  installRaw(idxEntry(std::string(300, 'a'), 0, 3) + idxEntry("zoo", 3, 4), "abczebr");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "zoo");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, "zebr");
}

TEST_F(MalformedDictTest, HighBytesInHeadwordsAreMatchedByteForByte) {
  // Latin-1 "café" is not valid UTF-8; cleanWord keeps every byte >= 0x80 and
  // the comparison is byte-wise, so it still resolves.
  const std::string latin1 = "caf\xE9";
  installRaw(idxEntry(latin1, 0, 6), "coffee");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, latin1.c_str());
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.headword, latin1);
  EXPECT_EQ(o.def, "coffee");
}

TEST_F(MalformedDictTest, LoneContinuationBytesAreOrdinaryHeadwordBytes) {
  const std::string broken =
      "\xFF\xFE"
      "x";
  installRaw(idxEntry(broken, 0, 2), "ok");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, broken.c_str());
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, "ok");
  EXPECT_FALSE(doLookup(dict, "\xFF\xFE").hit);  // prefix only: clean miss
}

TEST_F(MalformedDictTest, DefinitionWithEmbeddedNulsIsReturnedWhole) {
  // Multi-type StarDict entries embed NUL separators; the definition is a
  // sized read, so it is not cut at the first one.
  const std::string data = std::string("m\0eaning\0extra", 14);
  installRaw(idxEntry("multi", 0, 14), data);
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "multi");
  ASSERT_TRUE(o.hit);
  EXPECT_EQ(o.def.size(), 14u);
  EXPECT_EQ(o.def, data);
}

TEST_F(MalformedDictTest, NonUtf8DefinitionBytesAreReturnedVerbatim) {
  const std::string data = "\x80\xFE\xC3(\xED\xA0\x80";
  installRaw(idxEntry("junk", 0, static_cast<uint32_t>(data.size())), data);
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "junk");
  ASSERT_TRUE(o.hit);
  EXPECT_EQ(o.def, data);
}

TEST_F(MalformedDictTest, EmptyHeadwordEntriesAreScannedPast) {
  installRaw(idxEntry("", 0, 3) + idxEntry("cat", 0, 3), "def");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "cat");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, "def");
}

TEST_F(MalformedDictTest, AnIndexOfNothingButNulsIsACleanMiss) {
  installRaw(std::string(64, '\0'), "def");
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "cat");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
}

TEST_F(MalformedDictTest, IndexOfRandomBytesNeitherCrashesNorHits) {
  // 4KB of a fixed non-random-but-arbitrary byte pattern: the NULs in it make
  // the scanner see garbage "headwords" with garbage offsets, and every probe
  // must still come back as a bounded, definition-free miss.
  std::string junk(4096, '\0');
  for (size_t i = 0; i < junk.size(); i++) junk[i] = static_cast<char>((i * 137 + 11) & 0xFF);
  installRaw(junk, "def");
  ASSERT_TRUE(dict.buildIndex());

  for (const char* word : {"cat", "zebra", "a", "\xC3\xA9"}) {
    const Outcome o = doLookup(dict, word);
    EXPECT_FALSE(o.hit) << word;
    EXPECT_EQ(o.result, LookupResult::NotFound) << word;
    EXPECT_TRUE(o.def.empty()) << word;
  }
}

TEST_F(MalformedDictTest, EmptySynFileDisablesSynonymsWithoutAReadError) {
  const std::string emptySyn;
  installRaw(idxEntry("cat", 0, 3), "def", &emptySyn);
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "zzz");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);
  EXPECT_TRUE(doLookup(dict, "cat").hit);
}

TEST_F(MalformedDictTest, UnusableSidxDeclinesSynonymsAndMissesBecomeReadErrors) {
  // A .sidx with a valid header but zero samples would make every miss scan
  // the whole .syn a byte at a time, so the synonym path is declined — and a
  // declined probe is reported as a read error, not a genuine miss.
  const std::string syn = synEntry("dog", 0);
  installRaw(idxEntry("cat", 0, 3), "def", &syn);
  ASSERT_TRUE(dict.buildIndex());
  writeText("md", "md.sidx", sidecarHeader(0x58444953 /*SIDX*/, static_cast<uint32_t>(syn.size()), 0 /*samples*/, 1));

  const Outcome o = doLookup(dict, "zzz");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::ReadError);

  // The exact-match path is untouched by the broken synonym sidecar.
  EXPECT_TRUE(doLookup(dict, "cat").hit);
}

TEST_F(MalformedDictTest, SidxBuiltFromADifferentSynSizeIsIgnored) {
  const std::string syn = synEntry("dog", 0);
  installRaw(idxEntry("cat", 0, 3), "def", &syn);
  ASSERT_TRUE(dict.buildIndex());
  // Same sample count, wrong source size: the header is rejected, which
  // leaves zero samples and therefore declines synonyms.
  writeText("md", "md.sidx", sidecarHeader(0x58444953, 999, 1, 1) + le32(0));

  EXPECT_TRUE(dict.needsIndex());
  EXPECT_EQ(doLookup(dict, "zzz").result, LookupResult::ReadError);
}

TEST_F(MalformedDictTest, QidxWithAWrongMagicIsIgnoredAndTheScanStartsAtZero) {
  installRaw(idxEntry("ant", 0, 3) + idxEntry("bee", 3, 3), "antbee");
  ASSERT_TRUE(dict.buildIndex());
  writeText("md", "md.qidx", sidecarHeader(0x4B4F4F42 /*"BOOK"*/, 22, 1, 2) + le32(0));

  EXPECT_TRUE(dict.needsIndex());
  const Outcome o = doLookup(dict, "bee");
  EXPECT_TRUE(o.hit);
  EXPECT_EQ(o.def, "bee");
}

TEST_F(MalformedDictTest, SynOrdinalIntoATruncatedIndexIsACleanMiss) {
  // The .syn points at ordinal 1, but the .idx has only one complete entry.
  const std::string syn = synEntry("puss", 1);
  installRaw(idxEntry("cat", 0, 3) + std::string("dog\0\x00\x00", 6), "def", &syn);
  ASSERT_TRUE(dict.buildIndex());

  const Outcome o = doLookup(dict, "puss");
  EXPECT_FALSE(o.hit);
  EXPECT_EQ(o.result, LookupResult::NotFound);  // bounded by the sidecar entry count
  EXPECT_TRUE(o.def.empty());
}

}  // namespace
