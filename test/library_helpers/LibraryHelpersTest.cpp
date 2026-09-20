// Library helpers host suite: FsHelpers ordering / extension / path primitives,
// NextBookFinder end-of-book suggestions (FR-039), BookCacheUtils dispatch,
// ButtonNavigator list stepping (FR-036), the home menu index map (FR-029) and
// UITheme's cover-path / file-icon helpers (FR-032 icons).

#include <BookStubLog.h>
#include <CrossPointSettings.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformHost.h"
#include "activities/home/HomeMenuMap.h"
#include "components/UIThemeUtils.h"
#include "util/BookCacheUtils.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"

namespace {
// Fake clock consumed by ButtonNavigator through the platform seam.
unsigned long& g_millis = platform_host::clock();
}  // namespace

namespace {

using Button = MappedInputManager::Button;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

bool nl(const char* a, const char* b) { return FsHelpers::naturalLess(a, b); }

// ---------------------------------------------------------------------------
// FsHelpers::naturalLess
// ---------------------------------------------------------------------------

TEST(NaturalLess, ComparesNumericRunsByValueNotLexically) {
  EXPECT_TRUE(nl("2", "10"));
  EXPECT_FALSE(nl("10", "2"));
  EXPECT_TRUE(nl("Chapter 2.epub", "Chapter 10.epub"));
  EXPECT_FALSE(nl("Chapter 10.epub", "Chapter 2.epub"));
}

TEST(NaturalLess, SameLengthNumbersCompareDigitByDigit) {
  EXPECT_TRUE(nl("19", "21"));
  EXPECT_FALSE(nl("21", "19"));
  EXPECT_TRUE(nl("file19.txt", "file21.txt"));
}

TEST(NaturalLess, IsCaseInsensitive) {
  EXPECT_TRUE(nl("abc", "ABD"));
  EXPECT_FALSE(nl("ABD", "abc"));
  // Equal ignoring case: neither orders before the other.
  EXPECT_FALSE(nl("ABC", "abc"));
  EXPECT_FALSE(nl("abc", "ABC"));
}

TEST(NaturalLess, LeadingZerosDoNotAffectNumericValue) {
  EXPECT_FALSE(nl("007", "7"));
  EXPECT_FALSE(nl("7", "007"));
  EXPECT_TRUE(nl("007", "8"));
  EXPECT_FALSE(nl("0", "00"));
  EXPECT_FALSE(nl("00", "0"));
}

TEST(NaturalLess, ContinuesAfterEqualNumbers) {
  EXPECT_TRUE(nl("file2a", "file2b"));
  EXPECT_FALSE(nl("file2b", "file2a"));
  EXPECT_TRUE(nl("a", "a0"));
  EXPECT_FALSE(nl("a0", "a"));
}

TEST(NaturalLess, PrefixOrdersBeforeLongerString) {
  EXPECT_TRUE(nl("abc", "abcd"));
  EXPECT_FALSE(nl("abcd", "abc"));
  EXPECT_TRUE(nl("", "a"));
  EXPECT_FALSE(nl("a", ""));
  EXPECT_FALSE(nl("", ""));
}

TEST(NaturalLess, DigitVersusLetterFallsBackToCharacterOrder) {
  // '1' (0x31) sorts before 'b' (0x62) in the plain character branch.
  EXPECT_TRUE(nl("a1", "ab"));
  EXPECT_FALSE(nl("ab", "a1"));
}

TEST(NaturalLess, HighBitUtf8BytesOrderAfterAscii) {
  // 0xC3 is passed through tolower as an unsigned value, never as a negative char.
  EXPECT_TRUE(nl("z", "\xC3\xA9"));
  EXPECT_FALSE(nl("\xC3\xA9", "z"));
  EXPECT_FALSE(nl("\xC3\xA9", "\xC3\xA9"));
}

TEST(NaturalLess, VeryLongDigitRunsCompareByLengthWithoutOverflow) {
  const std::string forty(40, '9');
  const std::string fortyOne = "1" + std::string(40, '0');
  EXPECT_TRUE(FsHelpers::naturalLess(forty, fortyOne));
  EXPECT_FALSE(FsHelpers::naturalLess(fortyOne, forty));
}

TEST(NaturalLess, LongMixedStringsTerminate) {
  std::string a, b;
  for (int i = 0; i < 300; ++i) {
    a += "x" + std::to_string(i);
    b += "x" + std::to_string(i);
  }
  b += "!";
  EXPECT_TRUE(FsHelpers::naturalLess(a, b));
  EXPECT_FALSE(FsHelpers::naturalLess(b, a));
}

TEST(NaturalLess, StripsLeadingZeroRunsBeforeComparingTheRest) {
  // The zero run is consumed outright, so what follows decides the order.
  EXPECT_TRUE(nl("0a", "00b"));
  EXPECT_FALSE(nl("00b", "0a"));
  EXPECT_TRUE(nl("0", "01"));
  EXPECT_FALSE(nl("01", "0"));
  EXPECT_FALSE(nl("a007b", "a7b"));
  EXPECT_FALSE(nl("a7b", "a007b"));
}

TEST(NaturalLess, DigitRunLengthDecidesBeforeAnyTrailingText) {
  EXPECT_FALSE(nl("10a", "9b"));
  EXPECT_TRUE(nl("9b", "10a"));
  EXPECT_TRUE(nl("a9z", "a10a"));
  EXPECT_FALSE(nl("a10a", "a9z"));
}

TEST(NaturalLess, MultipleNumericRunsAreComparedLeftToRight) {
  EXPECT_TRUE(nl("v1-part9.epub", "v1-part10.epub"));
  EXPECT_TRUE(nl("v2-part1.epub", "v10-part1.epub"));
  EXPECT_FALSE(nl("v10-part1.epub", "v2-part9.epub"));
}

TEST(NaturalLess, CaseFoldingAppliesInsideMixedNames) {
  EXPECT_TRUE(nl("The Hobbit 2.epub", "the hobbit 10.epub"));
  EXPECT_FALSE(nl("the hobbit 10.epub", "The Hobbit 2.epub"));
  EXPECT_FALSE(nl("Book 01.EPUB", "book 1.epub"));
  EXPECT_FALSE(nl("book 1.epub", "Book 01.EPUB"));
}

TEST(NaturalLess, NonAsciiBytesCompareBytewiseWithNoUnicodeCollation) {
  // "Emile" spelled with U+00C9 (C3 89) lands after every ASCII letter, not next to 'E'.
  EXPECT_FALSE(nl("\xC3\x89mile.epub", "Zoe.epub"));
  EXPECT_TRUE(nl("Zoe.epub", "\xC3\x89mile.epub"));
  // Two accented names still separate on their trailing bytes.
  EXPECT_TRUE(nl("\xC3\xA0.epub", "\xC3\xA9.epub"));
  EXPECT_FALSE(nl("\xC3\xA9.epub", "\xC3\xA0.epub"));
}

TEST(NaturalLess, IsAStrictWeakOrderingOverARepresentativeCorpus) {
  // sortFileList hands this comparator to std::sort, which has undefined behaviour
  // on an invalid ordering -- so the three axioms are pinned directly.
  static const std::vector<std::string> corpus = {"",
                                                  " ",
                                                  "-",
                                                  ".",
                                                  "0",
                                                  "00",
                                                  "1",
                                                  "01",
                                                  "007",
                                                  "7",
                                                  "8",
                                                  "10",
                                                  "1a",
                                                  "a",
                                                  "A",
                                                  "a0",
                                                  "a1",
                                                  "a01",
                                                  "a10",
                                                  "a1b",
                                                  "a9z",
                                                  "a10a",
                                                  "ab",
                                                  "b",
                                                  "z",
                                                  "\xC3\xA9",
                                                  "Book 1.epub",
                                                  "book 01.epub",
                                                  "Book 2.epub",
                                                  "Book 10.epub",
                                                  "v1-part9",
                                                  "v1-part10",
                                                  "v2-part1",
                                                  "v10-part1"};
  const auto lt = [](const std::string& a, const std::string& b) { return FsHelpers::naturalLess(a, b); };
  const auto equiv = [&lt](const std::string& a, const std::string& b) { return !lt(a, b) && !lt(b, a); };

  for (const auto& a : corpus) {
    ASSERT_FALSE(lt(a, a)) << "not irreflexive at '" << a << "'";
    for (const auto& b : corpus) {
      if (lt(a, b)) {
        ASSERT_FALSE(lt(b, a)) << "not asymmetric: '" << a << "' / '" << b << "'";
      }
      for (const auto& c : corpus) {
        if (lt(a, b) && lt(b, c)) {
          ASSERT_TRUE(lt(a, c)) << "not transitive: '" << a << "' < '" << b << "' < '" << c << "'";
        }
        if (equiv(a, b) && equiv(b, c)) {
          ASSERT_TRUE(equiv(a, c)) << "equivalence not transitive: '" << a << "' ~ '" << b << "' ~ '" << c << "'";
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// FsHelpers::sortFileList (FR-032 ordering)
// ---------------------------------------------------------------------------

TEST(SortFileList, DirectoriesFirstThenNaturalOrderWithinEachGroup) {
  std::vector<std::string> list = {"b.epub", "zeta/", "10.txt", "2.txt", "Alpha/", "a.epub"};
  FsHelpers::sortFileList(list);
  const std::vector<std::string> expected = {"Alpha/", "zeta/", "2.txt", "10.txt", "a.epub", "b.epub"};
  EXPECT_EQ(list, expected);
}

TEST(SortFileList, DirectoryOrderingIsCaseInsensitiveAndNumeric) {
  std::vector<std::string> list = {"Vol 10/", "vol 2/", "VOL 1/"};
  FsHelpers::sortFileList(list);
  const std::vector<std::string> expected = {"VOL 1/", "vol 2/", "Vol 10/"};
  EXPECT_EQ(list, expected);
}

TEST(SortFileList, EmptyAndSingleElementListsAreUntouched) {
  std::vector<std::string> empty;
  FsHelpers::sortFileList(empty);
  EXPECT_TRUE(empty.empty());
  std::vector<std::string> one = {"only.epub"};
  FsHelpers::sortFileList(one);
  ASSERT_EQ(one.size(), 1u);
  EXPECT_EQ(one[0], "only.epub");
}

TEST(SortFileList, MatchesTheOrderTheFileBrowserDraws) {
  std::vector<std::string> list = {"notes.txt", "Zebra/",   "cover.png",  "Book 10.epub", "book 2.epub",
                                   "a.md",      "archive/", "10 Series/", "2 Series/",    "Book 1.epub"};
  FsHelpers::sortFileList(list);
  const std::vector<std::string> expected = {"2 Series/",   "10 Series/",  "archive/",     "Zebra/",    "a.md",
                                             "Book 1.epub", "book 2.epub", "Book 10.epub", "cover.png", "notes.txt"};
  EXPECT_EQ(list, expected);
}

TEST(SortFileList, IsIdempotent) {
  std::vector<std::string> list = {"b/", "10.txt", "A/", "2.txt", "a.epub"};
  FsHelpers::sortFileList(list);
  const std::vector<std::string> once = list;
  FsHelpers::sortFileList(list);
  EXPECT_EQ(list, once);
}

TEST(SortFileList, KeepsEveryEquivalentName) {
  // "a1", "a01" and "A1" all compare equal; none may be dropped, merged or rewritten.
  std::vector<std::string> list = {"a1", "a01", "A1"};
  FsHelpers::sortFileList(list);
  std::vector<std::string> seen = list;
  std::sort(seen.begin(), seen.end());
  const std::vector<std::string> expected = {"A1", "a01", "a1"};
  EXPECT_EQ(seen, expected);
}

TEST(SortFileList, EmptyEntryName) {
  // An empty entry name is not a directory: it sorts with the files, ahead of every
  // non-empty one, and reading its last character must not be attempted.
  std::vector<std::string> list = {"", "b.txt", "dir/", "a.txt"};
  FsHelpers::sortFileList(list);
  const std::vector<std::string> expected = {"dir/", "", "a.txt", "b.txt"};
  EXPECT_EQ(list, expected);
}

TEST(SortFileList, OnlyEmptyEntryNames) {
  // Every comparison in the sort touches two empty names; none may be reordered or lost.
  std::vector<std::string> list = {"", "", ""};
  FsHelpers::sortFileList(list);
  EXPECT_EQ(list.size(), 3u);
  for (const std::string& entry : list) {
    EXPECT_TRUE(entry.empty());
  }
}

// ---------------------------------------------------------------------------
// FsHelpers extension predicates (FR-032 file types)
// ---------------------------------------------------------------------------

TEST(FileExtension, CheckFileExtensionIsCaseInsensitiveSuffixMatch) {
  EXPECT_TRUE(FsHelpers::checkFileExtension("Book.EPUB"sv, ".epub"));
  EXPECT_TRUE(FsHelpers::checkFileExtension("book.epub"sv, ".EPUB"));
  EXPECT_FALSE(FsHelpers::checkFileExtension("book.epubx"sv, ".epub"));
  EXPECT_FALSE(FsHelpers::checkFileExtension("bookepub"sv, ".epub"));
}

TEST(FileExtension, ExactLengthMatchesAndShorterNamesDoNot) {
  EXPECT_TRUE(FsHelpers::checkFileExtension(".epub"sv, ".epub"));
  EXPECT_FALSE(FsHelpers::checkFileExtension("pub"sv, ".epub"));
  EXPECT_FALSE(FsHelpers::checkFileExtension(""sv, ".epub"));
}

TEST(FileExtension, EmptyExtensionMatchesEverything) {
  EXPECT_TRUE(FsHelpers::checkFileExtension("anything"sv, ""));
  EXPECT_TRUE(FsHelpers::checkFileExtension(""sv, ""));
}

TEST(FileExtension, ImagePredicates) {
  EXPECT_TRUE(FsHelpers::hasJpgExtension("a.jpg"sv));
  EXPECT_TRUE(FsHelpers::hasJpgExtension("a.JPEG"sv));
  EXPECT_FALSE(FsHelpers::hasJpgExtension("a.jpe"sv));
  EXPECT_TRUE(FsHelpers::hasPngExtension("cover.PNG"sv));
  EXPECT_FALSE(FsHelpers::hasPngExtension("cover.png.bak"sv));
  EXPECT_TRUE(FsHelpers::hasBmpExtension("thumb.bmp"sv));
  EXPECT_TRUE(FsHelpers::hasGifExtension("anim.Gif"sv));
  EXPECT_FALSE(FsHelpers::hasGifExtension("anim.giff"sv));
}

TEST(FileExtension, BookPredicates) {
  EXPECT_TRUE(FsHelpers::hasEpubExtension("Novel.Epub"sv));
  EXPECT_FALSE(FsHelpers::hasEpubExtension("Novel.epub3"sv));
  EXPECT_TRUE(FsHelpers::hasFb2Extension("story.FB2"sv));
  EXPECT_FALSE(FsHelpers::hasFb2Extension("story.fb2.zip"sv));
  EXPECT_TRUE(FsHelpers::hasXtcExtension("comic.xtc"sv));
  EXPECT_TRUE(FsHelpers::hasXtcExtension("comic.XTCH"sv));
  EXPECT_FALSE(FsHelpers::hasXtcExtension("comic.xtchx"sv));
  EXPECT_FALSE(FsHelpers::hasXtcExtension("comic.xt"sv));
}

TEST(FileExtension, TextAndStylesheetPredicates) {
  EXPECT_TRUE(FsHelpers::hasTxtExtension("notes.TXT"sv));
  EXPECT_FALSE(FsHelpers::hasTxtExtension("notes.text"sv));
  EXPECT_TRUE(FsHelpers::hasMarkdownExtension("readme.md"sv));
  EXPECT_FALSE(FsHelpers::hasMarkdownExtension("readme.markdown"sv));
  EXPECT_TRUE(FsHelpers::hasCssExtension("style.Css"sv));
  EXPECT_FALSE(FsHelpers::hasCssExtension("style.scss"sv));
}

TEST(FileExtension, ExtensionMustBeASuffixNotASubstring) {
  EXPECT_FALSE(FsHelpers::hasEpubExtension("my.epub.backup"sv));
  EXPECT_FALSE(FsHelpers::hasTxtExtension(".txt.gz"sv));
  EXPECT_TRUE(FsHelpers::hasTxtExtension("my.epub.txt"sv));
  EXPECT_TRUE(FsHelpers::hasEpubExtension("my.txt.epub"sv));
}

TEST(FileExtension, DotfilesNamedLikeAnExtensionStillMatch) {
  EXPECT_TRUE(FsHelpers::hasEpubExtension(".epub"sv));
  EXPECT_TRUE(FsHelpers::hasMarkdownExtension(".md"sv));
  EXPECT_TRUE(FsHelpers::hasXtcExtension(".xtch"sv));
}

TEST(FileExtension, ExtensionLongerThanTheNameNeverMatches) {
  EXPECT_FALSE(FsHelpers::checkFileExtension("ub"sv, ".epub"));
  EXPECT_FALSE(FsHelpers::hasEpubExtension("b"sv));
  EXPECT_FALSE(FsHelpers::hasMarkdownExtension("d"sv));
}

TEST(FileExtension, NonAsciiNamesAreMatchedOnTheirAsciiSuffix) {
  EXPECT_TRUE(FsHelpers::hasEpubExtension("caf\xC3\xA9.EPUB"sv));
  EXPECT_FALSE(FsHelpers::hasEpubExtension("caf\xC3\xA9"sv));
}

TEST(FileExtension, ViewsAreNotAssumedNullTerminated) {
  // checkFileExtension works on the view length alone, so a substring view of a
  // longer buffer must not see the bytes past its end.
  const std::string buffer = "book.epubXXXX";
  EXPECT_TRUE(FsHelpers::hasEpubExtension(std::string_view{buffer}.substr(0, 9)));
  EXPECT_FALSE(FsHelpers::hasEpubExtension(std::string_view{buffer}.substr(0, 8)));
}

// ---------------------------------------------------------------------------
// FsHelpers::extractFolderPath
// ---------------------------------------------------------------------------

TEST(ExtractFolderPath, StripsFinalComponent) {
  EXPECT_EQ(FsHelpers::extractFolderPath("/books/a.epub"), "/books");
  EXPECT_EQ(FsHelpers::extractFolderPath("/books/sub/a.epub"), "/books/sub");
  EXPECT_EQ(FsHelpers::extractFolderPath("/books/"), "/books");
}

TEST(ExtractFolderPath, RootAndBareNamesResolveToRoot) {
  EXPECT_EQ(FsHelpers::extractFolderPath("/a.epub"), "/");
  EXPECT_EQ(FsHelpers::extractFolderPath("a.epub"), "/");
  EXPECT_EQ(FsHelpers::extractFolderPath("/"), "/");
  EXPECT_EQ(FsHelpers::extractFolderPath(""), "/");
}

TEST(ExtractFolderPath, RelativePathsKeepTheirLeadingComponent) {
  EXPECT_EQ(FsHelpers::extractFolderPath("books/sub/a.epub"), "books/sub");
  EXPECT_EQ(FsHelpers::extractFolderPath("books/a.epub"), "books");
}

TEST(ExtractFolderPath, RepeatedSeparatorsAreNotCollapsed) {
  // Pins today's behaviour: only the final separator is cut, doubles survive.
  EXPECT_EQ(FsHelpers::extractFolderPath("/books//a.epub"), "/books/");
  EXPECT_EQ(FsHelpers::extractFolderPath("//"), "/");
}

// ---------------------------------------------------------------------------
// FsHelpers::sanitizePathComponentForFat32
// ---------------------------------------------------------------------------

std::string sanitize(const std::string& in, const size_t maxLen) {
  std::vector<char> out(maxLen, 'X');  // exact-size buffer: ASan catches any overrun
  FsHelpers::sanitizePathComponentForFat32(in.c_str(), out.data(), maxLen);
  return std::string(out.data());
}

TEST(SanitizeFat32, ReplacesReservedCharactersAndSpaces) {
  EXPECT_EQ(sanitize("a\\b/c:d*e?f\"g<h>i|j k", 64), "a-b-c-d-e-f-g-h-i-j-k");
}

TEST(SanitizeFat32, ReplacesControlCharsButKeepsDelAndHighBitBytes) {
  EXPECT_EQ(sanitize("a\x01\t\x1f"
                     "b",
                     64),
            "a---b");
  EXPECT_EQ(sanitize("a\x7f"
                     "b",
                     64),
            "a\x7f"
            "b");
  EXPECT_EQ(sanitize("caf\xC3\xA9.epub", 64), "caf\xC3\xA9.epub");
}

TEST(SanitizeFat32, KeepsDotsDashesAndUnderscores) {
  EXPECT_EQ(sanitize("my.book-v2_final.epub", 64), "my.book-v2_final.epub");
}

TEST(SanitizeFat32, TruncatesToMaxLenMinusOneAndTerminates) {
  EXPECT_EQ(sanitize("abcdefgh", 4), "abc");
  EXPECT_EQ(sanitize("abc", 4), "abc");
  EXPECT_EQ(sanitize("ab", 4), "ab");
}

TEST(SanitizeFat32, MaxLenOneWritesOnlyTerminator) { EXPECT_EQ(sanitize("abc", 1), ""); }

TEST(SanitizeFat32, MaxLenZeroLeavesOutputUntouched) {
  char sentinel = 'X';
  FsHelpers::sanitizePathComponentForFat32("abc", &sentinel, 0);
  EXPECT_EQ(sentinel, 'X');
}

TEST(SanitizeFat32, AnInputOfOnlyReservedCharactersBecomesAllDashes) {
  EXPECT_EQ(sanitize("   ", 8), "---");
  EXPECT_EQ(sanitize("<>:\"/\\|?*", 32), "---------");
}

TEST(SanitizeFat32, TrailingDotsSurviveButTrailingSpacesBecomeDashes) {
  // FAT32 forbids trailing dots and spaces; the helper only rewrites the spaces,
  // so a trailing '.' is passed through unchanged.
  EXPECT_EQ(sanitize("name.", 16), "name.");
  EXPECT_EQ(sanitize("name...", 16), "name...");
  EXPECT_EQ(sanitize("name ", 16), "name-");
  EXPECT_EQ(sanitize("name. ", 16), "name.-");
  EXPECT_EQ(sanitize(" lead", 16), "-lead");
}

TEST(SanitizeFat32, TruncationDropsAnIncompleteMultiByteSequence) {
  // maxLen is a byte budget, not a code-point budget, so "cafe" with U+00E9 loses
  // its continuation byte; the lone 0xC3 lead goes with it, because SdFat rejects
  // a name it cannot decode and the caller's mkdir would fail.
  EXPECT_EQ(sanitize("caf\xC3\xA9", 5), "caf");
  EXPECT_EQ(sanitize("caf\xC3\xA9", 6), "caf\xC3\xA9");
  // Already-broken input is cut at the bad byte wherever it sits.
  EXPECT_EQ(sanitize("ab\xE2\x82"
                     "cd",
                     64),
            "ab");
  EXPECT_EQ(sanitize("\x80"
                     "ab",
                     64),
            "");
}

TEST(SanitizeFat32, ReservedCharactersAreReplacedRightUpToTheTruncationPoint) {
  EXPECT_EQ(sanitize("a b c d e", 6), "a-b-c");
  EXPECT_EQ(sanitize("a/b", 3), "a-");
}

TEST(SanitizeFat32, ExactFitBufferKeepsEveryByte) {
  EXPECT_EQ(sanitize("abcdef", 7), "abcdef");
  EXPECT_EQ(sanitize("abcdef", 6), "abcde");
}

TEST(SanitizeFat32, TypicalBookTitleStaysReadable) {
  EXPECT_EQ(sanitize("The Hobbit: There & Back Again.epub", 64), "The-Hobbit--There-&-Back-Again.epub");
}

TEST(SanitizeFat32, EmptyInputProducesEmptyOutput) { EXPECT_EQ(sanitize("", 8), ""); }

// ---------------------------------------------------------------------------
// UIThemeUtils (UITheme::getCoverThumbPath / getFileIcon)
// ---------------------------------------------------------------------------

TEST(CoverThumbPath, SubstitutesHeightPlaceholder) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("/.crosspoint/epub_1/cover_[HEIGHT].bmp", 120),
            "/.crosspoint/epub_1/cover_120.bmp");
}

TEST(CoverThumbPath, PathWithoutPlaceholderIsUnchanged) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("/.crosspoint/epub_1/cover.bmp", 120), "/.crosspoint/epub_1/cover.bmp");
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("", 120), "");
}

TEST(CoverThumbPath, OnlyFirstPlaceholderIsReplaced) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("[HEIGHT]/[HEIGHT].bmp", 64), "64/[HEIGHT].bmp");
}

TEST(CoverThumbPath, PlaceholderMatchIsCaseSensitive) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("cover_[height].bmp", 90), "cover_[height].bmp");
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("cover_[HEIGHT.bmp", 90), "cover_[HEIGHT.bmp");
}

TEST(CoverThumbPath, ZeroAndNegativeHeightsAreSubstitutedVerbatim) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("c_[HEIGHT].bmp", 0), "c_0.bmp");
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("c_[HEIGHT].bmp", -1), "c_-1.bmp");
}

TEST(CoverThumbPath, PlaceholderAtEitherEndOfThePath) {
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("[HEIGHT]", 48), "48");
  EXPECT_EQ(UIThemeUtils::getCoverThumbPath("/covers/[HEIGHT]", 48), "/covers/48");
}

TEST(FileIcon, DirectoriesCarryTrailingSlash) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("Books/"), Folder);
  EXPECT_EQ(UIThemeUtils::getFileIcon("/"), Folder);
  EXPECT_EQ(UIThemeUtils::getFileIcon("odd.epub/"), Folder);
}

TEST(FileIcon, BookFormats) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.epub"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.FB2"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.xtc"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.xtch"), Book);
}

TEST(FileIcon, TextFormats) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.txt"), Text);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.MD"), Text);
}

TEST(FileIcon, ImageFormatsAndFallback) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.bmp"), Image);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.png"), Image);
  // Only the browser-visible image types get the Image icon.
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.jpg"), File);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.pdf"), File);
  EXPECT_EQ(UIThemeUtils::getFileIcon("noext"), File);
}

TEST(FileIcon, TheLastExtensionWins) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.txt.epub"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.epub.txt"), Text);
  EXPECT_EQ(UIThemeUtils::getFileIcon("a.png.bmp"), Image);
}

TEST(FileIcon, DotfilesNamedLikeAnExtensionGetThatIcon) {
  EXPECT_EQ(UIThemeUtils::getFileIcon(".epub"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon(".md"), Text);
  EXPECT_EQ(UIThemeUtils::getFileIcon(".gitignore"), File);
}

TEST(FileIcon, NonAsciiNamesKeepTheirExtensionIcon) {
  EXPECT_EQ(UIThemeUtils::getFileIcon("caf\xC3\xA9.EPUB"), Book);
  EXPECT_EQ(UIThemeUtils::getFileIcon("caf\xC3\xA9"), File);
}

TEST(FileIcon, EmptyName) {
  // A corrupt recent.json entry with "path": "" reaches getFileIcon through
  // RecentBooksActivity; it must fall through to the generic icon, not read back().
  EXPECT_EQ(UIThemeUtils::getFileIcon(""), File);
}

// ---------------------------------------------------------------------------
// HomeMenuMap (FR-029 menu order)
// ---------------------------------------------------------------------------

TEST(HomeMenuMap, IndexWithoutOpdsServers) {
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::FILE_BROWSER, false), 0);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::RECENTS, false), 1);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::FILE_TRANSFER, false), 2);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::SETTINGS_MENU, false), 3);
}

TEST(HomeMenuMap, IndexWithOpdsServersInsertsOpdsAfterRecents) {
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::FILE_BROWSER, true), 0);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::RECENTS, true), 1);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::OPDS_BROWSER, true), 2);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::FILE_TRANSFER, true), 3);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::SETTINGS_MENU, true), 4);
}

TEST(HomeMenuMap, UnavailableOrUnknownItemsFallBackToFirstRow) {
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::OPDS_BROWSER, false), 0);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::NONE, false), 0);
  EXPECT_EQ(HomeMenuMap::menuItemToIndex(HomeMenuItem::NONE, true), 0);
}

TEST(HomeMenuMap, ItemFromIndexWithoutOpdsServers) {
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(0, false), HomeMenuItem::FILE_BROWSER);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(1, false), HomeMenuItem::RECENTS);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(2, false), HomeMenuItem::FILE_TRANSFER);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(3, false), HomeMenuItem::SETTINGS_MENU);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(4, false), HomeMenuItem::NONE);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(-1, false), HomeMenuItem::NONE);
}

TEST(HomeMenuMap, ItemFromIndexWithOpdsServers) {
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(0, true), HomeMenuItem::FILE_BROWSER);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(1, true), HomeMenuItem::RECENTS);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(2, true), HomeMenuItem::OPDS_BROWSER);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(3, true), HomeMenuItem::FILE_TRANSFER);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(4, true), HomeMenuItem::SETTINGS_MENU);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(5, true), HomeMenuItem::NONE);
}

TEST(HomeMenuMap, IndicesPastTheLastRowAreNone) {
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(5, false), HomeMenuItem::NONE);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(99, false), HomeMenuItem::NONE);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(6, true), HomeMenuItem::NONE);
  EXPECT_EQ(HomeMenuMap::indexToMenuItem(-7, true), HomeMenuItem::NONE);
}

TEST(HomeMenuMap, RowCountShrinksByOneWithoutOpdsServers) {
  // FR-029: exactly four fixed rows, five once a server is stored.
  int withoutOpds = 0;
  int withOpds = 0;
  for (int i = 0; i < 16; ++i) {
    if (HomeMenuMap::indexToMenuItem(i, false) != HomeMenuItem::NONE) ++withoutOpds;
    if (HomeMenuMap::indexToMenuItem(i, true) != HomeMenuItem::NONE) ++withOpds;
  }
  EXPECT_EQ(withoutOpds, 4);
  EXPECT_EQ(withOpds, 5);
}

TEST(HomeMenuMap, RoundTripsEveryVisibleItem) {
  for (const bool opds : {false, true}) {
    for (const HomeMenuItem item : {HomeMenuItem::FILE_BROWSER, HomeMenuItem::RECENTS, HomeMenuItem::OPDS_BROWSER,
                                    HomeMenuItem::FILE_TRANSFER, HomeMenuItem::SETTINGS_MENU}) {
      if (item == HomeMenuItem::OPDS_BROWSER && !opds) continue;
      EXPECT_EQ(HomeMenuMap::indexToMenuItem(HomeMenuMap::menuItemToIndex(item, opds), opds), item);
    }
  }
}

// ---------------------------------------------------------------------------
// ButtonNavigator index math (FR-036)
// ---------------------------------------------------------------------------

TEST(ButtonNavigatorIndex, NextWrapsAndHandlesEmptyList) {
  EXPECT_EQ(ButtonNavigator::nextIndex(0, 5), 1);
  EXPECT_EQ(ButtonNavigator::nextIndex(4, 5), 0);
  EXPECT_EQ(ButtonNavigator::nextIndex(0, 1), 0);
  EXPECT_EQ(ButtonNavigator::nextIndex(3, 0), 0);
  EXPECT_EQ(ButtonNavigator::nextIndex(3, -1), 0);
}

TEST(ButtonNavigatorIndex, PreviousWrapsAndHandlesEmptyList) {
  EXPECT_EQ(ButtonNavigator::previousIndex(0, 5), 4);
  EXPECT_EQ(ButtonNavigator::previousIndex(3, 5), 2);
  EXPECT_EQ(ButtonNavigator::previousIndex(0, 1), 0);
  EXPECT_EQ(ButtonNavigator::previousIndex(3, 0), 0);
}

TEST(ButtonNavigatorIndex, NextPageJumpsToFirstRowOfNextPageAndWraps) {
  EXPECT_EQ(ButtonNavigator::nextPageIndex(0, 10, 4), 4);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(2, 10, 4), 4);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(4, 10, 4), 8);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(8, 10, 4), 0);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(9, 10, 4), 0);
}

TEST(ButtonNavigatorIndex, PreviousPageJumpsToFirstRowOfPreviousPageAndWraps) {
  EXPECT_EQ(ButtonNavigator::previousPageIndex(9, 10, 4), 4);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(5, 10, 4), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(0, 10, 4), 8);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(3, 10, 4), 8);
}

TEST(ButtonNavigatorIndex, PageStepFallsBackToRowStepWhenEverythingFitsOnOnePage) {
  EXPECT_EQ(ButtonNavigator::nextPageIndex(1, 3, 5), 2);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(2, 3, 5), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(0, 3, 5), 2);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(4, 5, 5), 0);
}

TEST(ButtonNavigatorIndex, PageStepWithNonPositiveInputsReturnsZero) {
  EXPECT_EQ(ButtonNavigator::nextPageIndex(3, 0, 4), 0);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(3, 10, 0), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(3, 0, 4), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(3, 10, -2), 0);
}

TEST(ButtonNavigatorIndex, IndicesOutsideTheListAreFoldedByTheModulo) {
  EXPECT_EQ(ButtonNavigator::nextIndex(7, 5), 3);
  EXPECT_EQ(ButtonNavigator::nextIndex(-1, 5), 0);
  EXPECT_EQ(ButtonNavigator::previousIndex(7, 5), 1);
  EXPECT_EQ(ButtonNavigator::previousIndex(-1, 5), 3);
}

TEST(ButtonNavigatorIndex, PageStepOnAnExactMultipleOfThePageSize) {
  EXPECT_EQ(ButtonNavigator::nextPageIndex(3, 8, 4), 4);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(4, 8, 4), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(4, 8, 4), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(0, 8, 4), 4);
}

TEST(ButtonNavigatorIndex, OnePerPageStepsOneRowAtATime) {
  EXPECT_EQ(ButtonNavigator::nextPageIndex(0, 10, 1), 1);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(9, 10, 1), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(0, 10, 1), 9);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(5, 10, 1), 4);
}

TEST(ButtonNavigatorIndex, PageStepFromAnIndexOutsideTheList) {
  // Pins the arithmetic for a stale selection: past the end wraps to the top,
  // before the start is treated as page zero.
  EXPECT_EQ(ButtonNavigator::nextPageIndex(12, 10, 4), 0);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(12, 10, 4), 8);
  EXPECT_EQ(ButtonNavigator::nextPageIndex(-1, 10, 4), 4);
  EXPECT_EQ(ButtonNavigator::previousPageIndex(-1, 10, 4), 8);
}

TEST(ButtonNavigatorIndex, NavigationListsAreTheLogicalNavButtons) {
  const auto next = ButtonNavigator::getNextButtons();
  const auto previous = ButtonNavigator::getPreviousButtons();
  ASSERT_EQ(next.size(), 1u);
  ASSERT_EQ(previous.size(), 1u);
  EXPECT_EQ(next[0], Button::NavNext);
  EXPECT_EQ(previous[0], Button::NavPrevious);
}

// ---------------------------------------------------------------------------
// ButtonNavigator input dispatch (FR-036 hold-to-page)
// ---------------------------------------------------------------------------

class ButtonNavigatorInputTest : public ::testing::Test {
 protected:
  MappedInputManager input;
  int fired = 0;

  void SetUp() override {
    ButtonNavigator::setMappedInputManager(input);
    g_millis = 10000;  // well past any interval so the first hold can fire
  }

  std::function<void()> counter() {
    return [this] { ++fired; };
  }
};

TEST_F(ButtonNavigatorInputTest, PressFiresOncePerPressEdgeOfAMatchingButton) {
  ButtonNavigator nav;
  nav.onNextPress(counter());
  EXPECT_EQ(fired, 0);

  input.pressedEdge = {Button::NavNext};
  nav.onNextPress(counter());
  EXPECT_EQ(fired, 1);
  nav.onPreviousPress(counter());
  EXPECT_EQ(fired, 1);

  input.pressedEdge = {Button::NavPrevious};
  nav.onPreviousPress(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, PressMatchesAnyButtonInTheList) {
  ButtonNavigator nav;
  input.pressedEdge = {Button::Confirm};
  nav.onPress({Button::NavNext, Button::Confirm}, counter());
  EXPECT_EQ(fired, 1);
  nav.onPress({Button::NavNext, Button::Back}, counter());
  EXPECT_EQ(fired, 1);
}

TEST_F(ButtonNavigatorInputTest, ReleaseFiresWhenNoContinuousNavigationHappened) {
  ButtonNavigator nav;
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 0);
  input.releasedEdge = {Button::NavNext};
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 1);
  nav.onPreviousRelease(counter());
  EXPECT_EQ(fired, 1);
}

TEST_F(ButtonNavigatorInputTest, ContinuousRequiresHoldBeyondStartAndIntervalBetweenSteps) {
  ButtonNavigator nav(500, 500);
  input.held = {Button::NavNext};

  input.heldTimeMs = 500;  // not strictly beyond continuousStartMs
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 0);

  input.heldTimeMs = 501;
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 1);

  nav.onNextContinuous(counter());  // same millis: interval not elapsed
  EXPECT_EQ(fired, 1);
  g_millis = 10500;  // exactly the interval: still not elapsed
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 1);
  g_millis = 10501;
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, ContinuousHonoursCustomTimings) {
  ButtonNavigator nav(/*continuousIntervalMs=*/100, /*continuousStartMs=*/200);
  input.held = {Button::NavPrevious};
  input.heldTimeMs = 200;
  nav.onPreviousContinuous(counter());
  EXPECT_EQ(fired, 0);
  input.heldTimeMs = 201;
  nav.onPreviousContinuous(counter());
  EXPECT_EQ(fired, 1);
  g_millis += 101;
  nav.onPreviousContinuous(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, ContinuousDoesNotFireWhileButtonIsNotHeld) {
  ButtonNavigator nav;
  input.heldTimeMs = 5000;
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 0);
  input.held = {Button::NavPrevious};
  nav.onNextContinuous(counter());
  EXPECT_EQ(fired, 0);
}

TEST_F(ButtonNavigatorInputTest, ReleaseIsSwallowedAfterContinuousNavigationThenRearmed) {
  ButtonNavigator nav;
  input.held = {Button::NavNext};
  input.heldTimeMs = 1000;
  nav.onNextContinuous(counter());
  ASSERT_EQ(fired, 1);

  // The release that ends a hold-to-page run must not add an extra row step.
  input.clearFrame();
  input.releasedEdge = {Button::NavNext};
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 1);

  // A later plain release (no continuous step in between) fires again.
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, OnNextCombinesPressAndContinuousPaths) {
  ButtonNavigator nav;
  input.pressedEdge = {Button::NavNext};
  nav.onNext(counter());
  EXPECT_EQ(fired, 1);

  input.clearFrame();
  input.held = {Button::NavNext};
  input.heldTimeMs = 1000;
  g_millis += 1000;
  nav.onNext(counter());
  EXPECT_EQ(fired, 2);

  input.clearFrame();
  nav.onNext(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, OnPressAndContinuousUsesTheGivenButtonList) {
  ButtonNavigator nav;
  input.pressedEdge = {Button::Right};
  nav.onPressAndContinuous({Button::Right}, counter());
  EXPECT_EQ(fired, 1);
  nav.onPressAndContinuous({Button::Left}, counter());
  EXPECT_EQ(fired, 1);
}

TEST_F(ButtonNavigatorInputTest, TwoMatchingButtonsInOneFrameStillFireOnce) {
  ButtonNavigator nav;
  input.pressedEdge = {Button::NavNext, Button::Confirm};
  nav.onPress({Button::NavNext, Button::Confirm}, counter());
  EXPECT_EQ(fired, 1);

  input.clearFrame();
  input.held = {Button::NavNext, Button::Confirm};
  input.heldTimeMs = 1000;
  nav.onContinuous({Button::NavNext, Button::Confirm}, counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, ReleaseUsesTheGivenButtonList) {
  ButtonNavigator nav;
  input.releasedEdge = {Button::Back};
  nav.onRelease({Button::Confirm}, counter());
  EXPECT_EQ(fired, 0);
  nav.onRelease({Button::Back, Button::Confirm}, counter());
  EXPECT_EQ(fired, 1);
}

TEST_F(ButtonNavigatorInputTest, OnPreviousCombinesPressAndContinuousPaths) {
  ButtonNavigator nav;
  input.pressedEdge = {Button::NavPrevious};
  nav.onPrevious(counter());
  EXPECT_EQ(fired, 1);

  input.clearFrame();
  input.held = {Button::NavPrevious};
  input.heldTimeMs = 1000;
  g_millis += 1000;
  nav.onPrevious(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, TheContinuousLatchIsSharedAcrossButtonLists) {
  // lastContinuousNavTime is per-navigator, not per-button: any release clears it.
  ButtonNavigator nav;
  input.held = {Button::NavNext};
  input.heldTimeMs = 1000;
  nav.onNextContinuous(counter());
  ASSERT_EQ(fired, 1);

  input.clearFrame();
  input.releasedEdge = {Button::Confirm};
  nav.onRelease({Button::Confirm}, counter());
  EXPECT_EQ(fired, 1);

  input.releasedEdge = {Button::NavNext};
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 2);
}

TEST_F(ButtonNavigatorInputTest, EachNavigatorKeepsItsOwnContinuousTiming) {
  ButtonNavigator fast(/*continuousIntervalMs=*/10, /*continuousStartMs=*/10);
  ButtonNavigator slow(/*continuousIntervalMs=*/1000, /*continuousStartMs=*/1000);
  input.held = {Button::NavNext};
  input.heldTimeMs = 100;

  fast.onNextContinuous(counter());
  EXPECT_EQ(fired, 1);
  slow.onNextContinuous(counter());
  EXPECT_EQ(fired, 1);
}

TEST_F(ButtonNavigatorInputTest, AnEmptyButtonListNeverFires) {
  // Activities build the list from settings; an empty one must be inert, not a match-all.
  ButtonNavigator nav;
  input.pressedEdge = {Button::NavNext};
  input.releasedEdge = {Button::NavNext};
  input.held = {Button::NavNext};
  input.heldTimeMs = 5000;
  nav.onPress({}, counter());
  nav.onRelease({}, counter());
  nav.onContinuous({}, counter());
  EXPECT_EQ(fired, 0);
}

TEST_F(ButtonNavigatorInputTest, AContinuousStepRecordedAtMillisZeroLosesTheReleaseLatch) {
  // lastContinuousNavTime doubles as the "no continuous step yet" sentinel, so a step
  // whose timestamp is exactly 0 rearms the release and adds one extra row step.
  ButtonNavigator nav(/*continuousIntervalMs=*/10, /*continuousStartMs=*/10);
  g_millis = 1000;
  input.held = {Button::NavNext};
  input.heldTimeMs = 100;
  nav.onNextContinuous(counter());
  ASSERT_EQ(fired, 1);

  g_millis = 0;  // millis() has wrapped; the next step stamps the sentinel value
  nav.onNextContinuous(counter());
  ASSERT_EQ(fired, 2);

  input.clearFrame();
  input.releasedEdge = {Button::NavNext};
  nav.onNextRelease(counter());
  EXPECT_EQ(fired, 3);
}

// ---------------------------------------------------------------------------
// BookCacheUtils
// ---------------------------------------------------------------------------

TEST(BookCacheDirectoryName, MatchesEveryCachePrefix) {
  EXPECT_TRUE(isBookCacheDirectoryName("epub_123"));
  EXPECT_TRUE(isBookCacheDirectoryName("fb2_abc"));
  EXPECT_TRUE(isBookCacheDirectoryName("txt_"));
  EXPECT_TRUE(isBookCacheDirectoryName("xtc_1"));
}

TEST(BookCacheDirectoryName, RejectsOtherNames) {
  EXPECT_FALSE(isBookCacheDirectoryName(nullptr));
  EXPECT_FALSE(isBookCacheDirectoryName(""));
  EXPECT_FALSE(isBookCacheDirectoryName("epub"));
  EXPECT_FALSE(isBookCacheDirectoryName("EPUB_1"));
  EXPECT_FALSE(isBookCacheDirectoryName("md_1"));
  EXPECT_FALSE(isBookCacheDirectoryName("settings.json"));
  EXPECT_FALSE(isBookCacheDirectoryName("_epub_1"));
}

TEST(BookCacheDirectoryName, RejectsNamesShorterThanThePrefix) {
  EXPECT_FALSE(isBookCacheDirectoryName("ep"));
  EXPECT_FALSE(isBookCacheDirectoryName("fb2"));
  EXPECT_FALSE(isBookCacheDirectoryName("txt"));
  EXPECT_FALSE(isBookCacheDirectoryName("xtc"));
  EXPECT_FALSE(isBookCacheDirectoryName("e"));
}

class ClearBookCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { bookStubCalls().clear(); }

  void expectSingleCall(const char* kind, const std::string& path) {
    ASSERT_EQ(bookStubCalls().size(), 1u);
    EXPECT_EQ(bookStubCalls()[0].kind, kind);
    EXPECT_EQ(bookStubCalls()[0].path, path);
    EXPECT_EQ(bookStubCalls()[0].cacheDir, "/.crosspoint");
  }
};

TEST_F(ClearBookCacheTest, EpubDispatchesToEpubWithCrosspointCacheRoot) {
  clearBookCache("/books/a.epub");
  expectSingleCall("epub", "/books/a.epub");
}

TEST_F(ClearBookCacheTest, ExtensionMatchIsCaseInsensitive) {
  clearBookCache("/books/A.EPUB");
  expectSingleCall("epub", "/books/A.EPUB");
}

TEST_F(ClearBookCacheTest, Fb2DispatchesToFb2) {
  clearBookCache("/books/a.fb2");
  expectSingleCall("fb2", "/books/a.fb2");
}

TEST_F(ClearBookCacheTest, XtcAndXtchDispatchToXtc) {
  clearBookCache("/books/a.xtc");
  expectSingleCall("xtc", "/books/a.xtc");
  bookStubCalls().clear();
  clearBookCache("/books/b.xtch");
  expectSingleCall("xtc", "/books/b.xtch");
}

TEST_F(ClearBookCacheTest, TxtDispatchesToTxt) {
  clearBookCache("/books/a.txt");
  expectSingleCall("txt", "/books/a.txt");
}

TEST_F(ClearBookCacheTest, MarkdownDispatchesToTxt) {
  // Markdown opens through the TXT reader and caches under the same txt_ prefix,
  // so clearing its cache must go through Txt as well.
  clearBookCache("/books/a.md");
  expectSingleCall("txt", "/books/a.md");
}

TEST_F(ClearBookCacheTest, MarkdownExtensionMatchIsCaseInsensitive) {
  clearBookCache("/books/README.MD");
  expectSingleCall("txt", "/books/README.MD");
}

TEST_F(ClearBookCacheTest, TheFinalExtensionDecidesTheCacheKind) {
  clearBookCache("/books/a.epub.txt");
  expectSingleCall("txt", "/books/a.epub.txt");
  bookStubCalls().clear();
  clearBookCache("/a.txt/b.epub");
  expectSingleCall("epub", "/a.txt/b.epub");
}

TEST_F(ClearBookCacheTest, BareExtensionNamesStillDispatch) {
  clearBookCache(".epub");
  expectSingleCall("epub", ".epub");
}

TEST_F(ClearBookCacheTest, EveryDispatchIsASingleCallToASingleBookKind) {
  // Each recognised extension must hit exactly one cache owner, never two.
  for (const char* path : {"/b/a.epub", "/b/a.fb2", "/b/a.xtc", "/b/a.xtch", "/b/a.txt"}) {
    bookStubCalls().clear();
    clearBookCache(path);
    EXPECT_EQ(bookStubCalls().size(), 1u) << path;
  }
}

TEST_F(ClearBookCacheTest, ViewerAndUnsupportedTypesAreLeftAlone) {
  for (const char* path : {"/b/a.bmp", "/b/a.png", "/b/a.jpg", "/b/a.css", "/b/a.bin", "/b/noext", "/b/a.xt"}) {
    clearBookCache(path);
  }
  EXPECT_TRUE(bookStubCalls().empty());
}

TEST_F(ClearBookCacheTest, UnknownAndEmptyPathsDoNothing) {
  clearBookCache("/books/a.pdf");
  clearBookCache("/books/cover.bmp");
  clearBookCache("");
  EXPECT_TRUE(bookStubCalls().empty());
}

// ---------------------------------------------------------------------------
// NextBookFinder (FR-039)
// ---------------------------------------------------------------------------

class NextBookFinderTest : public ::testing::Test {
 protected:
  fs::path root;

  void SetUp() override {
    static int counter = 0;
    root = fs::temp_directory_path() /
           ("crosspoint_library_helpers_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::remove_all(root);
    fs::create_directories(root);
    Storage.root = root.string();
    SETTINGS.showHiddenFiles = 0;
  }

  void TearDown() override {
    Storage.root.clear();
    fs::remove_all(root);
  }

  void touch(const std::string& devicePath) {
    const fs::path full = root / devicePath.substr(1);
    fs::create_directories(full.parent_path());
    std::ofstream(full).put('x');
  }

  void mkdirDevice(const std::string& devicePath) { fs::create_directories(root / devicePath.substr(1)); }

  // Shared folder: seven supported books after "Book 1.epub" in natural order,
  // plus viewer-only images, an unsupported file, a directory and a predecessor.
  void makeLibrary() {
    for (const char* name : {"Book 1.epub", "Book 2.epub", "Book 10.epub", "Book 3.fb2", "Book 4.xtc", "Book 5.xtch",
                             "Book 6.txt", "Book 7.md", "Book 8.bmp", "Book 9.png", "Book 11.pdf", "Aardvark.epub"}) {
      touch(std::string("/books/") + name);
    }
    mkdirDevice("/books/Book 12");
  }
};

TEST_F(NextBookFinderTest, ReturnsUpToMaxCountInNaturalOrderAfterCurrent) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/Book 1.epub", 3);
  const std::vector<std::string> expected = {"Book 2.epub", "Book 3.fb2", "Book 4.xtc"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, LargeMaxCountReturnsEverySupportedSuccessor) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/Book 1.epub", 50);
  const std::vector<std::string> expected = {"Book 2.epub", "Book 3.fb2", "Book 4.xtc",  "Book 5.xtch",
                                             "Book 6.txt",  "Book 7.md",  "Book 10.epub"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, MaxCountOneKeepsOnlyTheClosestSuccessor) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/Book 1.epub", 1);
  const std::vector<std::string> expected = {"Book 2.epub"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, ExcludesFilesOrderingAtOrBeforeCurrent) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/Book 6.txt", 10);
  const std::vector<std::string> expected = {"Book 7.md", "Book 10.epub"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, CurrentBookIsExcludedEvenWhenCaseDiffers) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/BOOK 2.EPUB", 2);
  const std::vector<std::string> expected = {"Book 3.fb2", "Book 4.xtc"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, HiddenFilesFollowShowHiddenFilesSetting) {
  // '-' (0x2d) orders before '.' (0x2e), so the dot-file is a genuine successor.
  touch("/h/-current.epub");
  touch("/h/.hidden.epub");
  touch("/h/0next.epub");

  SETTINGS.showHiddenFiles = 0;
  const std::vector<std::string> hidden = {"0next.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/h/-current.epub", 5), hidden);

  SETTINGS.showHiddenFiles = 1;
  const std::vector<std::string> shown = {".hidden.epub", "0next.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/h/-current.epub", 5), shown);
}

TEST_F(NextBookFinderTest, BookInRootFolderWithOrWithoutLeadingSlash) {
  touch("/z1.epub");
  touch("/z2.epub");
  const std::vector<std::string> expected = {"z2.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/z1.epub", 5), expected);
  EXPECT_EQ(NextBookFinder::findNextBooks("z1.epub", 5), expected);
}

TEST_F(NextBookFinderTest, DegenerateInputsReturnEmpty) {
  makeLibrary();
  EXPECT_TRUE(NextBookFinder::findNextBooks("/books/Book 1.epub", 0).empty());
  EXPECT_TRUE(NextBookFinder::findNextBooks("", 3).empty());
  EXPECT_TRUE(NextBookFinder::findNextBooks("/missing/Book 1.epub", 3).empty());
  // The "folder" component is a regular file, not a directory.
  EXPECT_TRUE(NextBookFinder::findNextBooks("/books/Book 1.epub/inner.epub", 3).empty());
}

TEST_F(NextBookFinderTest, EmptyFolderReturnsEmpty) {
  mkdirDevice("/empty");
  EXPECT_TRUE(NextBookFinder::findNextBooks("/empty/a.epub", 3).empty());
}

TEST_F(NextBookFinderTest, MaxCountEqualToTheSuccessorCountReturnsThemAll) {
  makeLibrary();
  const auto result = NextBookFinder::findNextBooks("/books/Book 1.epub", 7);
  const std::vector<std::string> expected = {"Book 2.epub", "Book 3.fb2", "Book 4.xtc",  "Book 5.xtch",
                                             "Book 6.txt",  "Book 7.md",  "Book 10.epub"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, ThreeSuggestionsIsWhatTheEndOfBookScreenAsksFor) {
  // FR-039: up to three books from the same folder, strictly after the current one.
  makeLibrary();
  EXPECT_EQ(NextBookFinder::findNextBooks("/books/Book 1.epub", 3).size(), 3u);
  EXPECT_EQ(NextBookFinder::findNextBooks("/books/Book 7.md", 3).size(), 1u);
  EXPECT_TRUE(NextBookFinder::findNextBooks("/books/Book 10.epub", 3).empty());
}

TEST_F(NextBookFinderTest, LeadingZeroVariantsOfTheCurrentNameAreNotSuggested) {
  touch("/z/Book 1.epub");
  touch("/z/Book 01.epub");
  touch("/z/Book 2.epub");
  // "Book 01" and "Book 1" compare equal, so neither is a successor of the other.
  const std::vector<std::string> expected = {"Book 2.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/z/Book 01.epub", 5), expected);
  EXPECT_EQ(NextBookFinder::findNextBooks("/z/Book 1.epub", 5), expected);
}

TEST_F(NextBookFinderTest, UppercaseExtensionsAreRecognised) {
  touch("/u/a.epub");
  touch("/u/b.EPUB");
  touch("/u/c.FB2");
  touch("/u/d.XTCH");
  touch("/u/e.TXT");
  touch("/u/f.MD");
  touch("/u/g.PNG");
  const std::vector<std::string> expected = {"b.EPUB", "c.FB2", "d.XTCH", "e.TXT", "f.MD"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/u/a.epub", 10), expected);
}

TEST_F(NextBookFinderTest, DirectoriesAreNeverSuggestedEvenWhenTheyWouldSortFirst) {
  touch("/d/a.epub");
  mkdirDevice("/d/b-folder");
  mkdirDevice("/d/b-folder.epub");
  touch("/d/c.epub");
  const std::vector<std::string> expected = {"c.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/d/a.epub", 5), expected);
}

TEST_F(NextBookFinderTest, HiddenDirectoriesAreIgnoredEvenWithShowHiddenFilesOn) {
  // '-' orders before '.', so the dot-entry is a genuine successor -- it is dropped
  // because it is a directory, not because it is hidden.
  touch("/hd/-current.epub");
  mkdirDevice("/hd/.cache.epub");
  touch("/hd/b.epub");
  SETTINGS.showHiddenFiles = 1;
  const std::vector<std::string> expected = {"b.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/hd/-current.epub", 5), expected);
}

TEST_F(NextBookFinderTest, SubfolderContentsAreNotSearched) {
  touch("/s/a.epub");
  touch("/s/deeper/z.epub");
  EXPECT_TRUE(NextBookFinder::findNextBooks("/s/a.epub", 5).empty());
}

TEST_F(NextBookFinderTest, LargeFolderStaysBoundedAndPicksTheLowestSuccessors) {
  // 200 candidates, three slots: the result must be the three lowest-ordering
  // names regardless of the order readdir hands them over.
  touch("/big/book 000.epub");
  for (int i = 1; i <= 200; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "/big/book %d.epub", i);
    touch(name);
  }
  const auto result = NextBookFinder::findNextBooks("/big/book 000.epub", 3);
  const std::vector<std::string> expected = {"book 1.epub", "book 2.epub", "book 3.epub"};
  EXPECT_EQ(result, expected);
}

TEST_F(NextBookFinderTest, APathEndingInASeparatorTreatsEveryFileAsASuccessor) {
  // Pins today's behaviour: the name after the last '/' is empty, and the empty
  // string orders before every non-empty name.
  touch("/t/a.epub");
  touch("/t/b.epub");
  const std::vector<std::string> expected = {"a.epub", "b.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/t/", 5), expected);
}

TEST_F(NextBookFinderTest, ABookThatIsAlreadyLastYieldsNothing) {
  makeLibrary();
  EXPECT_TRUE(NextBookFinder::findNextBooks("/books/Book 10.epub", 3).empty());
  EXPECT_TRUE(NextBookFinder::findNextBooks("/books/zzz.epub", 3).empty());
}

TEST_F(NextBookFinderTest, ANameAtTheFilesystemComponentLimitIsReturnedWhole) {
  // Entries are copied through a fixed 500-byte name buffer; a 254-byte name (the
  // longest a FAT32/APFS component allows) must come back untruncated.
  const std::string longName = std::string(249, 'y') + ".epub";
  ASSERT_EQ(longName.size(), 254u);
  touch("/ln/a.epub");
  touch("/ln/" + longName);
  const auto result = NextBookFinder::findNextBooks("/ln/a.epub", 5);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0], longName);
}

TEST_F(NextBookFinderTest, NonAsciiAndPunctuationHeavyNamesSurviveVerbatim) {
  touch("/nm/a.epub");
  touch("/nm/caf\xC3\xA9 (2nd ed.) [v2]!.epub");
  touch("/nm/~tilde$dollar#hash.txt");
  const std::vector<std::string> expected = {"caf\xC3\xA9 (2nd ed.) [v2]!.epub", "~tilde$dollar#hash.txt"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/nm/a.epub", 5), expected);
}

TEST_F(NextBookFinderTest, ADotfileThatIsNothingButAnExtensionIsStillABook) {
  // '-' orders before '.', so both dot-entries are genuine successors once hidden
  // files are shown; the extension check must not need a stem in front of it.
  touch("/dx/-current.epub");
  touch("/dx/.epub");
  touch("/dx/.md");
  SETTINGS.showHiddenFiles = 1;
  const std::vector<std::string> expected = {".epub", ".md"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/dx/-current.epub", 5), expected);
}

TEST_F(NextBookFinderTest, NamesDifferingOnlyByAnEmbeddedSpaceStayDistinct) {
  touch("/sp/a.epub");
  touch("/sp/b .epub");
  touch("/sp/b.epub");
  const std::vector<std::string> expected = {"b .epub", "b.epub"};
  EXPECT_EQ(NextBookFinder::findNextBooks("/sp/a.epub", 5), expected);
}

}  // namespace
