// Pure OPDS UI helpers extracted from OpdsServerListActivity (download folder
// normalisation, FR-162) and OpdsBookBrowserActivity (OpenSearch query
// encoding and the pagination rows, FR-163 / contracts/opds-feed.md).

#include <OpdsUiUtils.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// --- normalizeDownloadFolder (FR-162: default SD root, normalised to /path) ---

TEST(OpdsFolderNormalizeTest, EmptyInputMeansSdRoot) { EXPECT_EQ(opds_ui::normalizeDownloadFolder(""), ""); }

TEST(OpdsFolderNormalizeTest, BareSlashMeansSdRoot) { EXPECT_EQ(opds_ui::normalizeDownloadFolder("/"), ""); }

TEST(OpdsFolderNormalizeTest, WhitespaceOnlyMeansSdRoot) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("   "), "");
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("\t\t"), "");
  EXPECT_EQ(opds_ui::normalizeDownloadFolder(" \t "), "");
}

TEST(OpdsFolderNormalizeTest, PaddedSlashMeansSdRoot) { EXPECT_EQ(opds_ui::normalizeDownloadFolder("  /  "), ""); }

TEST(OpdsFolderNormalizeTest, AddsLeadingSlash) { EXPECT_EQ(opds_ui::normalizeDownloadFolder("books"), "/books"); }

TEST(OpdsFolderNormalizeTest, KeepsAnExistingLeadingSlash) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("/books"), "/books");
}

TEST(OpdsFolderNormalizeTest, StripsTrailingSlashes) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("/books/"), "/books");
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("/books///"), "/books");
}

TEST(OpdsFolderNormalizeTest, TrimsSpacesAndTabsOnBothSides) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("  \t/books \t "), "/books");
}

TEST(OpdsFolderNormalizeTest, KeepsInteriorSpaces) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("  my books  "), "/my books");
}

TEST(OpdsFolderNormalizeTest, NormalisesNestedPaths) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("books/epub/"), "/books/epub");
}

TEST(OpdsFolderNormalizeTest, LeavesInteriorDoubleSlashesAlone) {
  // Only the trailing run is collapsed; interior duplication is the user's.
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("//books//epub//"), "//books//epub");
}

TEST(OpdsFolderNormalizeTest, IsIdempotent) {
  const std::string once = opds_ui::normalizeDownloadFolder(" downloads/ ");
  EXPECT_EQ(opds_ui::normalizeDownloadFolder(once), once);
}

TEST(OpdsFolderNormalizeTest, KeepsNonAsciiBytesIntact) {
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("Bücher/"), "/Bücher");
}

TEST(OpdsFolderNormalizeTest, DoesNotTrimNewlinesOrOtherControlBytes) {
  // Only spaces and tabs are trimmed; pinned so a future change is deliberate.
  EXPECT_EQ(opds_ui::normalizeDownloadFolder("\nbooks"), "/\nbooks");
}

// --- percentEncodeQuery (contracts/opds-feed.md: alnum and -_.~ kept) --------

TEST(OpdsSearchEncodeTest, UnreservedCharactersPassThrough) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("abcXYZ019-_.~"), "abcXYZ019-_.~");
}

TEST(OpdsSearchEncodeTest, EmptyQueryEncodesToEmpty) { EXPECT_EQ(opds_ui::percentEncodeQuery(""), ""); }

TEST(OpdsSearchEncodeTest, SpacesBecomePercent20) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("war and peace"), "war%20and%20peace");
}

TEST(OpdsSearchEncodeTest, ReservedPunctuationIsEncoded) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("a&b=c?d/e+f"), "a%26b%3Dc%3Fd%2Fe%2Bf");
}

TEST(OpdsSearchEncodeTest, Utf8BytesEncodeAsUppercaseHexPerByte) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("é"), "%C3%A9");
  EXPECT_EQ(opds_ui::percentEncodeQuery("日"), "%E6%97%A5");
}

TEST(OpdsSearchEncodeTest, ControlBytesAreEncoded) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("\n\t"), "%0A%09");
  EXPECT_EQ(opds_ui::percentEncodeQuery(std::string("\x00", 1)), "%00");
}

TEST(OpdsSearchEncodeTest, HighBytesUseUppercaseHex) {
  EXPECT_EQ(opds_ui::percentEncodeQuery(std::string("\xFF", 1)), "%FF");
}

TEST(OpdsSearchEncodeTest, AlreadyEncodedInputIsEncodedAgain) {
  EXPECT_EQ(opds_ui::percentEncodeQuery("%20"), "%2520");
}

// --- buildSearchUrl -----------------------------------------------------------

TEST(OpdsSearchUrlTest, SubstitutesThePlaceholder) {
  EXPECT_EQ(opds_ui::buildSearchUrl("https://cat/search?q={searchTerms}", "dune"), "https://cat/search?q=dune");
}

TEST(OpdsSearchUrlTest, SubstitutesOnlyTheFirstPlaceholder) {
  EXPECT_EQ(opds_ui::buildSearchUrl("/s?a={searchTerms}&b={searchTerms}", "x"), "/s?a=x&b={searchTerms}");
}

TEST(OpdsSearchUrlTest, TemplateWithoutThePlaceholderIsUnchanged) {
  EXPECT_EQ(opds_ui::buildSearchUrl("https://cat/search?q=fixed", "dune"), "https://cat/search?q=fixed");
}

TEST(OpdsSearchUrlTest, EmptyTemplateStaysEmpty) { EXPECT_EQ(opds_ui::buildSearchUrl("", "dune"), ""); }

TEST(OpdsSearchUrlTest, EmptyQueryLeavesAnEmptySubstitution) {
  // performSearch() refuses an empty query before this point; pinned anyway.
  EXPECT_EQ(opds_ui::buildSearchUrl("/s?q={searchTerms}&n=1", ""), "/s?q=&n=1");
}

TEST(OpdsSearchUrlTest, EncodesTheQueryBeforeSubstituting) {
  EXPECT_EQ(opds_ui::buildSearchUrl("/s?q={searchTerms}", "war & peace"), "/s?q=war%20%26%20peace");
}

TEST(OpdsSearchUrlTest, APlaceholderInsideTheQueryIsNotReprocessed) {
  EXPECT_EQ(opds_ui::buildSearchUrl("/s?q={searchTerms}", "{searchTerms}"), "/s?q=%7BsearchTerms%7D");
}

TEST(OpdsSearchUrlTest, PlaceholderAtTheVeryStartIsSubstituted) {
  EXPECT_EQ(opds_ui::buildSearchUrl("{searchTerms}", "a b"), "a%20b");
}

TEST(OpdsSearchUrlTest, PartialPlaceholderIsNotMatched) {
  EXPECT_EQ(opds_ui::buildSearchUrl("/s?q={searchTerm}", "x"), "/s?q={searchTerm}");
}

// --- addPaginationRows (FR-163) ----------------------------------------------

std::vector<OpdsEntry> twoBooks() {
  std::vector<OpdsEntry> entries;
  entries.push_back(OpdsEntry{OpdsEntryType::BOOK, "Book A", "Author A", "/a.epub", "id-a"});
  entries.push_back(OpdsEntry{OpdsEntryType::BOOK, "Book B", "Author B", "/b.epub", "id-b"});
  return entries;
}

TEST(OpdsPaginationTest, AddsBothRowsAroundTheFeed) {
  std::vector<OpdsEntry> entries = twoBooks();
  opds_ui::addPaginationRows(entries, "/prev", "/next", "<< Previous Page", "Next Page >>");

  ASSERT_EQ(entries.size(), 4u);
  EXPECT_EQ(entries.front().title, "<< Previous Page");
  EXPECT_EQ(entries.front().href, "/prev");
  EXPECT_EQ(entries.back().title, "Next Page >>");
  EXPECT_EQ(entries.back().href, "/next");
  EXPECT_EQ(entries[1].title, "Book A");
  EXPECT_EQ(entries[2].title, "Book B");
}

TEST(OpdsPaginationTest, PaginationRowsAreNavigationEntriesWithNoAuthorOrId) {
  std::vector<OpdsEntry> entries = twoBooks();
  opds_ui::addPaginationRows(entries, "/prev", "/next", "P", "N");

  ASSERT_EQ(entries.size(), 4u);
  EXPECT_EQ(entries.front().type, OpdsEntryType::NAVIGATION);
  EXPECT_TRUE(entries.front().author.empty());
  EXPECT_TRUE(entries.front().id.empty());
  EXPECT_EQ(entries.back().type, OpdsEntryType::NAVIGATION);
  EXPECT_TRUE(entries.back().author.empty());
  EXPECT_TRUE(entries.back().id.empty());
}

TEST(OpdsPaginationTest, AddsOnlyThePreviousRow) {
  std::vector<OpdsEntry> entries = twoBooks();
  opds_ui::addPaginationRows(entries, "/prev", "", "P", "N");

  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries.front().title, "P");
  EXPECT_EQ(entries.back().title, "Book B");
}

TEST(OpdsPaginationTest, AddsOnlyTheNextRow) {
  std::vector<OpdsEntry> entries = twoBooks();
  opds_ui::addPaginationRows(entries, "", "/next", "P", "N");

  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries.front().title, "Book A");
  EXPECT_EQ(entries.back().title, "N");
}

TEST(OpdsPaginationTest, AddsNothingWhenBothUrlsAreEmpty) {
  std::vector<OpdsEntry> entries = twoBooks();
  opds_ui::addPaginationRows(entries, "", "", "P", "N");

  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries.front().title, "Book A");
  EXPECT_EQ(entries.back().title, "Book B");
}

TEST(OpdsPaginationTest, WorksOnAnEmptyFeed) {
  std::vector<OpdsEntry> entries;
  opds_ui::addPaginationRows(entries, "/prev", "/next", "P", "N");

  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].href, "/prev");
  EXPECT_EQ(entries[1].href, "/next");
}

TEST(OpdsPaginationTest, EmptyFeedWithNoLinksStaysEmpty) {
  std::vector<OpdsEntry> entries;
  opds_ui::addPaginationRows(entries, "", "", "P", "N");
  EXPECT_TRUE(entries.empty());
}

TEST(OpdsPaginationTest, ReservesForBothRowsUpFront) {
  // One reserve() for the whole operation: the insert() at the front must not
  // reallocate and then have push_back() reallocate again.
  std::vector<OpdsEntry> entries = twoBooks();
  entries.shrink_to_fit();
  opds_ui::addPaginationRows(entries, "/prev", "/next", "P", "N");
  EXPECT_GE(entries.capacity(), 4u);
  EXPECT_EQ(entries.size(), 4u);
}

}  // namespace
