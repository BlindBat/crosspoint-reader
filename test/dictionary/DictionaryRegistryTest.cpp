// Host tests for src/util/DictionaryRegistry.cpp — the /dictionaries and
// /.dictionaries folder scan (discover) and the settings-name resolver
// (resolveBasePath). Folder layouts are built per test in a sandbox that the
// HalStorage stub maps the device-absolute roots onto.

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "src/util/DictionaryRegistry.h"

namespace {

namespace fs = std::filesystem;

class DictionaryRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root = fs::absolute(std::string("sandbox_reg_") + info->name());
    fs::remove_all(root);
    fs::create_directories(root);
    dictstub::storageRoot = root.string();
  }

  void TearDown() override {
    dictstub::storageRoot.clear();
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // Create an empty file at /<dictRoot>/<folder>/<name> (folders created on demand).
  void touch(const std::string& dictRoot, const std::string& folder, const std::string& name) {
    fs::create_directories(root / dictRoot / folder);
    std::ofstream out(root / dictRoot / folder / name, std::ios::binary);
    out << "x";
  }

  void mkdir(const std::string& rel) { fs::create_directories(root / rel); }

  // A complete dictionary: <stem>.idx plus <stem>.dict under /<dictRoot>/<folder>.
  void dictionary(const std::string& folder, const std::string& stem, const std::string& dictRoot = "dictionaries",
                  const char* dataExt = ".dict") {
    touch(dictRoot, folder, stem + ".idx");
    touch(dictRoot, folder, stem + dataExt);
  }

  static std::vector<std::string> names(const std::vector<DictionaryEntry>& entries) {
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto& e : entries) out.push_back(e.name);
    return out;
  }

  static std::vector<DictionaryEntry> discovered() {
    std::vector<DictionaryEntry> out;
    DictionaryRegistry::discover(out);
    return out;
  }

  fs::path root;
};

// ---------------------------------------------------------------------------
// discover(): roots and folder filtering
// ---------------------------------------------------------------------------

TEST_F(DictionaryRegistryTest, NoRootsGivesEmptyList) { EXPECT_TRUE(discovered().empty()); }

TEST_F(DictionaryRegistryTest, DiscoverClearsPreviousContents) {
  std::vector<DictionaryEntry> out;
  out.push_back({"stale", "stale"});
  DictionaryRegistry::discover(out);
  EXPECT_TRUE(out.empty());
}

TEST_F(DictionaryRegistryTest, RootThatIsAFileIsIgnored) {
  std::ofstream(root / "dictionaries", std::ios::binary) << "not a directory";
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, SingleDictionaryReportsFolderAndStem) {
  dictionary("webster", "web1913");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "webster");
  EXPECT_EQ(entries[0].stem, "web1913");
}

TEST_F(DictionaryRegistryTest, DictDzOnlyDataIsAccepted) {
  dictionary("gcide", "gcide", "dictionaries", ".dict.dz");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].stem, "gcide");
}

TEST_F(DictionaryRegistryTest, IdxWithoutDataFileIsNotListed) {
  touch("dictionaries", "broken", "broken.idx");
  touch("dictionaries", "broken", "broken.ifo");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, DataFileMustMatchTheIdxStem) {
  touch("dictionaries", "mismatch", "a.idx");
  touch("dictionaries", "mismatch", "b.dict");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, FolderWithTwoIdxStemsIsAmbiguousAndSkipped) {
  dictionary("two", "first");
  dictionary("two", "second");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, AppleDoubleIdxDoesNotMakeAFolderAmbiguous) {
  dictionary("mac", "words");
  touch("dictionaries", "mac", "._words.idx");
  touch("dictionaries", "mac", "._words.dict");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].stem, "words");
}

TEST_F(DictionaryRegistryTest, AppleDoubleIdxAloneIsNotADictionary) {
  touch("dictionaries", "ghost", "._words.idx");
  touch("dictionaries", "ghost", "words.dict");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, DotFoldersAreSkipped) {
  dictionary(".hidden", "h");
  dictionary("shown", "s");
  EXPECT_EQ(names(discovered()), std::vector<std::string>{"shown"});
}

TEST_F(DictionaryRegistryTest, PlainFilesDirectlyUnderTheRootAreIgnored) {
  std::ofstream(root / "dictionaries" / "loose.idx", std::ios::binary) << "x";
  std::ofstream(root / "dictionaries" / "loose.dict", std::ios::binary) << "x";
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, NonIdxFilesAndUppercaseExtensionAreIgnored) {
  touch("dictionaries", "misc", "readme.txt");
  touch("dictionaries", "misc", "words.idx.gz");
  touch("dictionaries", "misc", "WORDS.IDX");  // extension match is case-sensitive
  touch("dictionaries", "misc", "WORDS.dict");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, FileNamedJustIdxHasNoStem) {
  touch("dictionaries", "bare", ".idx");
  touch("dictionaries", "bare", ".dict");
  EXPECT_TRUE(discovered().empty());
}

TEST_F(DictionaryRegistryTest, SingleCharacterStemIsTheShortestAccepted) {
  dictionary("tiny", "a");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].stem, "a");
}

TEST_F(DictionaryRegistryTest, SubdirectoryNamedLikeAnIdxIsIgnored) {
  dictionary("nested", "real");
  mkdir("dictionaries/nested/decoy.idx");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].stem, "real");
}

TEST_F(DictionaryRegistryTest, StemKeepsInnerDots) {
  dictionary("dotted", "en-US.v2");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].stem, "en-US.v2");
}

TEST_F(DictionaryRegistryTest, HiddenRootIsScanned) {
  dictionary("secret", "s", ".dictionaries");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "secret");
}

TEST_F(DictionaryRegistryTest, BothRootsAreMergedAndSortedTogether) {
  dictionary("zulu", "z");
  dictionary("alpha", "a", ".dictionaries");
  dictionary("mike", "m", ".dictionaries");
  EXPECT_EQ(names(discovered()), (std::vector<std::string>{"alpha", "mike", "zulu"}));
}

TEST_F(DictionaryRegistryTest, SameFolderNameInBothRootsIsListedTwice) {
  // Pin: discover() does not de-duplicate across roots; resolveBasePath()
  // then always picks the /dictionaries copy (see PrefersVisibleRoot).
  dictionary("dup", "v");
  dictionary("dup", "h", ".dictionaries");
  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].name, "dup");
  EXPECT_EQ(entries[1].name, "dup");
}

TEST_F(DictionaryRegistryTest, SortIsCaseInsensitiveByFolderName) {
  dictionary("banana", "b");
  dictionary("Apple", "a");
  dictionary("cherry", "c");
  dictionary("apricot", "d");
  EXPECT_EQ(names(discovered()), (std::vector<std::string>{"Apple", "apricot", "banana", "cherry"}));
}

TEST_F(DictionaryRegistryTest, AmbiguousAndBrokenFoldersDoNotHideGoodOnes) {
  dictionary("good", "g");
  dictionary("two", "x");
  dictionary("two", "y");
  touch("dictionaries", "noidx", "words.dict");
  EXPECT_EQ(names(discovered()), std::vector<std::string>{"good"});
}

// ---------------------------------------------------------------------------
// resolveBasePath(): name validation and root precedence
// ---------------------------------------------------------------------------

TEST_F(DictionaryRegistryTest, ResolveRejectsNullAndEmptyNames) {
  dictionary("en", "en");
  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath(nullptr, base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("", base));
}

TEST_F(DictionaryRegistryTest, ResolveRejectsDotPrefixedNames) {
  dictionary(".hidden", "h");  // exists on disk, still refused
  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath(".hidden", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath(".", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("..", base));
}

TEST_F(DictionaryRegistryTest, ResolveRejectsPathSeparators) {
  dictionary("en", "en");
  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("en/../en", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("en/", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("en\\sub", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("/en", base));
}

TEST_F(DictionaryRegistryTest, ResolveReturnsExtensionlessBasePath) {
  dictionary("webster", "web1913");
  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("webster", base));
  EXPECT_EQ(base, "/dictionaries/webster/web1913");
}

TEST_F(DictionaryRegistryTest, ResolveFallsBackToHiddenRoot) {
  dictionary("secret", "s", ".dictionaries");
  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("secret", base));
  EXPECT_EQ(base, "/.dictionaries/secret/s");
}

TEST_F(DictionaryRegistryTest, ResolvePrefersVisibleRoot) {
  dictionary("dup", "visible");
  dictionary("dup", "hidden", ".dictionaries");
  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("dup", base));
  EXPECT_EQ(base, "/dictionaries/dup/visible");
}

TEST_F(DictionaryRegistryTest, ResolveSkipsAnUnusableVisibleCopy) {
  // The /dictionaries copy is ambiguous; the hidden one is usable.
  dictionary("dup", "x");
  dictionary("dup", "y");
  dictionary("dup", "h", ".dictionaries");
  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("dup", base));
  EXPECT_EQ(base, "/.dictionaries/dup/h");
}

TEST_F(DictionaryRegistryTest, ResolveFailsForMissingAmbiguousOrDatalessFolders) {
  dictionary("two", "x");
  dictionary("two", "y");
  touch("dictionaries", "noidx", "w.dict");
  touch("dictionaries", "nodata", "w.idx");
  std::string base = "untouched";
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("missing", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("two", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("noidx", base));
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("nodata", base));
  EXPECT_EQ(base, "untouched");
}

TEST_F(DictionaryRegistryTest, ResolveIgnoresAppleDoubleIdx) {
  dictionary("mac", "words");
  touch("dictionaries", "mac", "._words.idx");
  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("mac", base));
  EXPECT_EQ(base, "/dictionaries/mac/words");
}

// ---------------------------------------------------------------------------
// Name-buffer limits (both scans read entry names into a 128-byte buffer, and
// SdFat's getName reports a name that does not fit as an empty string)
// ---------------------------------------------------------------------------

TEST_F(DictionaryRegistryTest, FolderNameLongerThanTheNameBufferIsNotDiscovered) {
  // Pinned: getName() cannot fit a 200-byte name in the 128-byte buffer, so
  // discover() sees an empty name and probes "/dictionaries/" itself; with no
  // loose index there the folder is simply not listed.
  // resolveBasePath() is handed the name directly and never truncates, so a
  // settings value naming such a folder still resolves — Dictionary::open()
  // is what refuses it, on the 160-byte path budget.
  const std::string longName(200, 'f');
  dictionary(longName, "w");
  EXPECT_TRUE(names(discovered()).empty());

  std::string base;
  EXPECT_TRUE(DictionaryRegistry::resolveBasePath(longName.c_str(), base));
  EXPECT_GT(base.size(), 160u);
}

TEST_F(DictionaryRegistryTest, OverLongFolderNameMakesTheRootItselfLookLikeADictionary) {
  // Bug pinned: the empty name discover() falls back to builds the folder path
  // "/dictionaries/", so a loose <stem>.idx/<stem>.dict pair at the root is
  // adopted as a dictionary whose name is "". The entry is listed but useless —
  // resolveBasePath() refuses an empty name.
  dictionary(std::string(200, 'f'), "w");
  std::ofstream(root / "dictionaries" / "loose.idx", std::ios::binary) << "x";
  std::ofstream(root / "dictionaries" / "loose.dict", std::ios::binary) << "x";

  const auto entries = discovered();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "");
  EXPECT_EQ(entries[0].stem, "loose");

  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath(entries[0].name.c_str(), base));
}

TEST_F(DictionaryRegistryTest, IdxNameLongerThanTheNameBufferIsNotAnIndex) {
  // getName() reports the oversize file name as empty, and an empty name is
  // shorter than ".idx", so the folder holds no index.
  const std::string longStem(200, 's');
  touch("dictionaries", "big", longStem + ".idx");
  touch("dictionaries", "big", longStem + ".dict");
  EXPECT_TRUE(names(discovered()).empty());
}

TEST_F(DictionaryRegistryTest, LongestStemThatStillFitsTheNameBufferIsAccepted) {
  // 123 stem bytes + ".idx" is 127 bytes, which fits the 128-byte buffer with
  // its NUL — one more would be reported as an empty name.
  const std::string stem(123, 's');
  dictionary("fits", stem);
  const auto found = discovered();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].stem, stem);

  std::string base;
  ASSERT_TRUE(DictionaryRegistry::resolveBasePath("fits", base));
  EXPECT_EQ(base, "/dictionaries/fits/" + stem);
}

TEST_F(DictionaryRegistryTest, OneByteOverTheNameBufferIsAlreadyTooLong) {
  // 124 + ".idx" = 128 bytes, one past what the buffer can hold with its NUL.
  dictionary("over", std::string(124, 's'));
  EXPECT_TRUE(names(discovered()).empty());

  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("over", base));
}

TEST_F(DictionaryRegistryTest, AppleDoubleDataFileDoesNotSatisfyTheDataRequirement) {
  touch("dictionaries", "mac", "words.idx");
  touch("dictionaries", "mac", "._words.dict");
  EXPECT_TRUE(names(discovered()).empty());

  std::string base;
  EXPECT_FALSE(DictionaryRegistry::resolveBasePath("mac", base));
}

TEST_F(DictionaryRegistryTest, ResolveRejectsNamesThatWouldEscapeTheRoots) {
  dictionary("en", "w");
  std::string base = "untouched";
  for (const char* name : {"..", "../en", ".", "./en", "en/../en", "\\en", "a/b"}) {
    EXPECT_FALSE(DictionaryRegistry::resolveBasePath(name, base)) << name;
  }
  EXPECT_EQ(base, "untouched");
}

}  // namespace
