#include <FsHelpers.h>
#include <gtest/gtest.h>

#include <string_view>

namespace {

// A bare string literal converts equally well to std::string_view and to the
// stub String, so route through the string_view overload explicitly (that is
// the overload the firmware's directory-listing code uses).
bool has(std::string_view name) { return FsHelpers::hasFb2Extension(name); }

TEST(HasFb2Extension, MatchesLowercaseExtension) { EXPECT_TRUE(has("book.fb2")); }

TEST(HasFb2Extension, IsCaseInsensitive) {
  EXPECT_TRUE(has("BOOK.FB2"));
  EXPECT_TRUE(has("book.Fb2"));
  EXPECT_TRUE(has("book.fB2"));
}

// Documents current behavior: matching is a pure suffix check, so the common
// zipped variant .fb2.zip is NOT recognized as an FB2 file.
TEST(HasFb2Extension, ZippedFb2IsNotRecognized) { EXPECT_FALSE(has("book.fb2.zip")); }

TEST(HasFb2Extension, RejectsNoExtensionAndBareStem) {
  EXPECT_FALSE(has("book"));
  EXPECT_FALSE(has("fb2"));
  EXPECT_FALSE(has("bookfb2"));
  EXPECT_FALSE(has(""));
}

// Documents current behavior: a bare ".fb2" (hidden-file style name) counts
// as having the extension -- the suffix check has no stem requirement.
TEST(HasFb2Extension, HiddenFileStyleBareExtensionMatches) {
  EXPECT_TRUE(has(".fb2"));
  EXPECT_TRUE(has(".hidden.fb2"));
}

TEST(HasFb2Extension, WorksOnFullPaths) {
  EXPECT_TRUE(has("/sdcard/books/war-and-peace.fb2"));
  EXPECT_FALSE(has("/sdcard/books.fb2/readme.txt"));
}

TEST(HasFb2Extension, TrailingCharactersAfterExtensionDoNotMatch) {
  EXPECT_FALSE(has("book.fb2~"));
  EXPECT_FALSE(has("book.fb2 "));
  EXPECT_FALSE(has("book.fb20"));
}

TEST(HasFb2Extension, DoesNotMatchOtherReaderFormats) {
  EXPECT_FALSE(has("book.epub"));
  EXPECT_FALSE(has("book.txt"));
}

}  // namespace
