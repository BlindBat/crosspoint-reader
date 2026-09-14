#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Fb2/Fb2MetadataParser.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fixturePath;
using fb2test::readAll;

// Byte offset of the n-th (0-based) occurrence of `needle` in `haystack`.
size_t nthOccurrence(const std::string& haystack, const std::string& needle, int n) {
  size_t pos = haystack.find(needle);
  while (n-- > 0 && pos != std::string::npos) {
    pos = haystack.find(needle, pos + 1);
  }
  return pos;
}

TEST(Fb2MetadataParser, ExtractsTitleAuthorLanguageAndCoverId) {
  Fb2MetadataParser parser(fixturePath("basic.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "The Crosspoint Chronicle");
  EXPECT_EQ(parser.getAuthor(), "John Doe");
  EXPECT_EQ(parser.getLanguage(), "en");
  // xlink href "#cover.jpg" is stored with the leading '#' stripped.
  EXPECT_EQ(parser.getCoverBinaryId(), "cover.jpg");
}

TEST(Fb2MetadataParser, SectionOffsetsPointAtTheSectionTags) {
  const std::string raw = readAll(fixturePath("basic.fb2"));
  ASSERT_FALSE(raw.empty());

  Fb2MetadataParser parser(fixturePath("basic.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].title, "Chapter One");
  EXPECT_EQ(sections[1].title, "Chapter Two");

  for (int i = 0; i < 2; i++) {
    const size_t openPos = nthOccurrence(raw, "<section", i);
    const size_t closePos = nthOccurrence(raw, "</section>", i);
    ASSERT_NE(openPos, std::string::npos);
    ASSERT_NE(closePos, std::string::npos);
    EXPECT_EQ(sections[i].fileOffset, openPos) << "section " << i;
    // The parser estimates the end as the closing tag's offset plus
    // strlen("section") + 3 == strlen("</section>").
    EXPECT_EQ(sections[i].fileOffset + sections[i].length, closePos + 10) << "section " << i;
  }
}

TEST(Fb2MetadataParser, TocEntriesMirrorTopLevelSections) {
  Fb2MetadataParser parser(fixturePath("basic.fb2"));
  ASSERT_TRUE(parser.parse());
  const auto& toc = parser.getTocEntries();
  ASSERT_EQ(toc.size(), 2u);
  EXPECT_EQ(toc[0].title, "Chapter One");
  EXPECT_EQ(toc[0].sectionIndex, 0);
  EXPECT_EQ(toc[1].title, "Chapter Two");
  EXPECT_EQ(toc[1].sectionIndex, 1);
}

// Documents a current limitation: only the first <author> is kept (the
// middle name joins in First Middle Last order; the second author is lost).
TEST(Fb2MetadataParser, MultipleAuthorsKeepOnlyTheFirst) {
  Fb2MetadataParser parser(fixturePath("multi-author.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getAuthor(), "John Quincy Smith");
}

TEST(Fb2MetadataParser, CoverHrefWithoutHashIsKeptVerbatim) {
  Fb2MetadataParser parser(fixturePath("multi-author.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getCoverBinaryId(), "cover-nohash.png");
}

TEST(Fb2MetadataParser, MissingTitleInfoFieldsStayEmpty) {
  Fb2MetadataParser parser(fixturePath("no-cover.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Untitled Fields");
  EXPECT_EQ(parser.getAuthor(), "");
  EXPECT_EQ(parser.getLanguage(), "");
  EXPECT_EQ(parser.getCoverBinaryId(), "");
}

TEST(Fb2MetadataParser, UntitledSectionKeepsEmptyTitle) {
  Fb2MetadataParser parser(fixturePath("no-cover.fb2"));
  ASSERT_TRUE(parser.parse());
  ASSERT_EQ(parser.getSections().size(), 1u);
  EXPECT_EQ(parser.getSections()[0].title, "");
  ASSERT_EQ(parser.getTocEntries().size(), 1u);
  EXPECT_EQ(parser.getTocEntries()[0].title, "");
}

TEST(Fb2MetadataParser, NestedSectionsOnlyTopLevelOnesAreTracked) {
  const std::string raw = readAll(fixturePath("nested-sections.fb2"));
  Fb2MetadataParser parser(fixturePath("nested-sections.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);
  // Multi-paragraph titles are joined with a space.
  EXPECT_EQ(sections[0].title, "Part One The Beginning");
  EXPECT_EQ(sections[1].title, "Part Two");

  // First top-level section spans through its nested child: it opens at the
  // first "<section" and closes at the SECOND "</section>" (the inner one
  // closes first).
  EXPECT_EQ(sections[0].fileOffset, nthOccurrence(raw, "<section", 0));
  EXPECT_EQ(sections[0].fileOffset + sections[0].length, nthOccurrence(raw, "</section>", 1) + 10);
  EXPECT_EQ(sections[1].fileOffset, nthOccurrence(raw, "<section", 2));
}

// KNOWN BUG (documents current behavior): the parser re-enters "body" mode for
// the second <body name="notes"> element, so footnote sections are appended to
// the regular section/TOC list and show up as reading chapters.
TEST(Fb2MetadataParser, NotesBodySectionsBecomeRegularSections) {
  Fb2MetadataParser parser(fixturePath("notes-body.fb2"));
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].title, "Story");
  EXPECT_EQ(sections[1].title, "Note 1");
  EXPECT_EQ(sections[2].title, "Note 2");
  EXPECT_EQ(parser.getTocEntries().size(), 3u);
}

TEST(Fb2MetadataParser, TruncatedXmlFailsParse) {
  Fb2MetadataParser parser(fixturePath("malformed-truncated.fb2"));
  EXPECT_FALSE(parser.parse());
}

// Documents current behavior: the parser never validates the root element, so
// any well-formed XML "parses" and falls back to one whole-file section.
TEST(Fb2MetadataParser, WrongRootElementParsesWithWholeFileFallback) {
  const std::string raw = readAll(fixturePath("wrong-root.fb2"));
  Fb2MetadataParser parser(fixturePath("wrong-root.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "");
  ASSERT_EQ(parser.getSections().size(), 1u);
  EXPECT_EQ(parser.getSections()[0].fileOffset, 0u);
  EXPECT_EQ(parser.getSections()[0].length, raw.size());
}

TEST(Fb2MetadataParser, SectionlessBodyFallsBackToOneWholeFileSection) {
  const std::string raw = readAll(fixturePath("no-sections.fb2"));
  Fb2MetadataParser parser(fixturePath("no-sections.fb2"));
  ASSERT_TRUE(parser.parse());
  ASSERT_EQ(parser.getSections().size(), 1u);
  // The book title stands in for the section title.
  EXPECT_EQ(parser.getSections()[0].title, "Plain Stream");
  EXPECT_EQ(parser.getSections()[0].fileOffset, 0u);
  EXPECT_EQ(parser.getSections()[0].length, raw.size());
  ASSERT_EQ(parser.getTocEntries().size(), 1u);
  EXPECT_EQ(parser.getTocEntries()[0].sectionIndex, 0);
}

TEST(Fb2MetadataParser, EmptyFileFailsParse) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/empty.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, ""));
  Fb2MetadataParser parser(path);
  EXPECT_FALSE(parser.parse());
}

TEST(Fb2MetadataParser, MissingFileFailsParse) {
  Fb2MetadataParser parser("/nonexistent/definitely-not-here.fb2");
  EXPECT_FALSE(parser.parse());
}

// windows-1251 covers most Russian FB2s in the wild; the registered
// unknown-encoding handler must map the cp1251 bytes to UTF-8 output.
TEST(Fb2MetadataParser, DeclaredWindows1251EncodingDecodesToUtf8) {
  Fb2MetadataParser parser(fixturePath("cp1251-declared.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Тестовая книга");
  EXPECT_EQ(parser.getAuthor(), "Лев Толстой");
  EXPECT_EQ(parser.getLanguage(), "ru");
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].title, "Глава первая");
  EXPECT_EQ(sections[1].title, "Глава вторая");
}

TEST(Fb2MetadataParser, DeclaredWindows1252EncodingDecodesToUtf8) {
  Fb2MetadataParser parser(fixturePath("cp1252-declared.fb2"));
  ASSERT_TRUE(parser.parse());
  // C1-range curly quotes (0x93/0x94) plus Latin-1 accents.
  EXPECT_EQ(parser.getTitle(), "Café “München”");
  EXPECT_EQ(parser.getLanguage(), "fr");
  ASSERT_EQ(parser.getSections().size(), 1u);
  EXPECT_EQ(parser.getSections()[0].title, "Début");
}

TEST(Fb2MetadataParser, EncodingNameMatchIsCaseInsensitiveAndAcceptsCp1251Alias) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/alias.fb2";
  // "CP1251" alias, uppercase; title bytes are cp1251 for "Ёж" (0xA8 0xE6).
  std::string doc =
      "<?xml version=\"1.0\" encoding=\"CP1251\"?>\n"
      "<FictionBook><description><title-info>"
      "<book-title>\xA8\xE6</book-title>"
      "</title-info></description>"
      "<body><section><p>x</p></section></body></FictionBook>\n";
  ASSERT_TRUE(fb2test::writeAll(path, doc));
  Fb2MetadataParser parser(path);
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Ёж");
}

TEST(Fb2MetadataParser, TrulyUnknownEncodingStillFailsParse) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/koi8.fb2";
  std::string doc =
      "<?xml version=\"1.0\" encoding=\"koi8-r\"?>\n"
      "<FictionBook><body><section><p>x</p></section></body></FictionBook>\n";
  ASSERT_TRUE(fb2test::writeAll(path, doc));
  Fb2MetadataParser parser(path);
  EXPECT_FALSE(parser.parse());
}

TEST(Fb2MetadataParser, Utf8CyrillicTitlesSurviveIntact) {
  Fb2MetadataParser parser(fixturePath("unicode-titles.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Война и мир");
  EXPECT_EQ(parser.getAuthor(), "Лев Толстой");
  EXPECT_EQ(parser.getLanguage(), "ru");
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].title, "Глава первая");
  EXPECT_EQ(sections[1].title, "Глава вторая");
}

}  // namespace
