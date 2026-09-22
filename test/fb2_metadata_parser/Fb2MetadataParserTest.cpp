#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "CollectingParser.h"
#include "Fb2/Fb2MetadataParser.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

// Byte offset of the n-th (0-based) occurrence of `needle` in `haystack`.
size_t nthOccurrence(const std::string& haystack, const std::string& needle, int n) {
  size_t pos = haystack.find(needle);
  while (n-- > 0 && pos != std::string::npos) {
    pos = haystack.find(needle, pos + 1);
  }
  return pos;
}

TEST(Fb2MetadataParser, ExtractsTitleAuthorLanguageAndCoverId) {
  CollectingParser parser(fixturePath("basic.fb2"));
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

  CollectingParser parser(fixturePath("basic.fb2"));
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

// Documents a current limitation: only the first <author> is kept (the
// middle name joins in First Middle Last order; the second author is lost).
TEST(Fb2MetadataParser, MultipleAuthorsKeepOnlyTheFirst) {
  CollectingParser parser(fixturePath("multi-author.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getAuthor(), "John Quincy Smith");
}

TEST(Fb2MetadataParser, CoverHrefWithoutHashIsKeptVerbatim) {
  CollectingParser parser(fixturePath("multi-author.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getCoverBinaryId(), "cover-nohash.png");
}

TEST(Fb2MetadataParser, MissingTitleInfoFieldsStayEmpty) {
  CollectingParser parser(fixturePath("no-cover.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Untitled Fields");
  EXPECT_EQ(parser.getAuthor(), "");
  EXPECT_EQ(parser.getLanguage(), "");
  EXPECT_EQ(parser.getCoverBinaryId(), "");
}

// Superseded by contract L2: an untitled section no longer keeps an empty title,
// it borrows its own first paragraph so the chapter list reads as words. A section
// with no text at all still stores nothing (L7, covered in Fb2LabelTest).
TEST(Fb2MetadataParser, UntitledSectionIsLabelledFromItsFirstParagraph) {
  CollectingParser parser(fixturePath("no-cover.fb2"));
  ASSERT_TRUE(parser.parse());
  ASSERT_EQ(parser.getSections().size(), 1u);
  EXPECT_EQ(parser.getSections()[0].title, "A section without a title element.");
  EXPECT_EQ(parser.getSections()[0].titleDerived, 1);
}

// Every <section> is a chapter, at any depth (contract C2/C5). A nested section
// is a chapter of its own, not content of its parent.
TEST(Fb2MetadataParser, NestedSectionsEachBecomeTheirOwnChapter) {
  const std::string raw = readAll(fixturePath("nested-sections.fb2"));
  CollectingParser parser(fixturePath("nested-sections.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 3u);
  // Multi-paragraph titles are joined with a space.
  EXPECT_EQ(sections[0].title, "Part One The Beginning");
  EXPECT_EQ(sections[1].title, "Inner Chapter");
  EXPECT_EQ(sections[2].title, "Part Two");
  EXPECT_EQ(sections[0].level, 0);
  EXPECT_EQ(sections[1].level, 1);
  EXPECT_EQ(sections[2].level, 0);

  // Chapters appear in start-tag order, each at its own "<section".
  EXPECT_EQ(sections[0].fileOffset, nthOccurrence(raw, "<section", 0));
  EXPECT_EQ(sections[1].fileOffset, nthOccurrence(raw, "<section", 1));
  EXPECT_EQ(sections[2].fileOffset, nthOccurrence(raw, "<section", 2));
}

// Contract C7: a chapter's length is its own span minus its children's, so the
// chapter lengths partition the body instead of double-counting nested text.
TEST(Fb2MetadataParser, NestedChapterLengthsExcludeChildSpans) {
  const std::string raw = readAll(fixturePath("nested-sections.fb2"));
  CollectingParser parser(fixturePath("nested-sections.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 3u);

  // The inner chapter owns its whole span: "<section" (index 1) through the
  // first "</section>".
  const size_t innerStart = nthOccurrence(raw, "<section", 1);
  const size_t innerEnd = nthOccurrence(raw, "</section>", 0) + 10;
  EXPECT_EQ(sections[1].length, innerEnd - innerStart);

  // The parent's own bytes are its full span minus the inner chapter's.
  const size_t parentStart = nthOccurrence(raw, "<section", 0);
  const size_t parentEnd = nthOccurrence(raw, "</section>", 1) + 10;
  EXPECT_EQ(sections[0].length, (parentEnd - parentStart) - (innerEnd - innerStart));
}

// Contract C2/C5 over three levels of nesting.
TEST(Fb2MetadataParser, DeepNestingNumbersEveryLevelInDocumentOrder) {
  CollectingParser parser(fixturePath("nested-deep.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 4u);
  EXPECT_EQ(sections[0].title, "Book Level");
  EXPECT_EQ(sections[1].title, "Part Level");
  EXPECT_EQ(sections[2].title, "Chapter Level");
  EXPECT_EQ(sections[3].title, "Scene Level");
  for (size_t i = 0; i < sections.size(); i++) {
    EXPECT_EQ(sections[i].level, static_cast<uint8_t>(i)) << "chapter " << i;
    if (i > 0) EXPECT_GT(sections[i].fileOffset, sections[i - 1].fileOffset) << "chapter " << i;
  }
}

// Contract C4: only a <title> that is a DIRECT child of the section counts. A
// <poem><title> inside the section's own content must not become its title.
TEST(Fb2MetadataParser, PoemTitleInsideASectionIsNotTheChapterTitle) {
  CollectingParser parser(fixturePath("nested-deep.fb2"));
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_FALSE(sections.empty());
  EXPECT_EQ(sections[0].title, "Book Level");
  for (const auto& section : sections) {
    EXPECT_EQ(section.title.find("Poem Title"), std::string::npos) << section.title;
  }
}

// A section whose only content is another section is still a chapter, with an
// empty title (the UI substitutes the localized "Unnamed" label).
TEST(Fb2MetadataParser, WrapperSectionIsAChapterWithAnEmptyTitle) {
  CollectingParser parser(fixturePath("wrapper-only.fb2"));
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].title, "Outer");
  EXPECT_EQ(sections[1].title, "");
  EXPECT_EQ(sections[2].title, "Inner Leaf");
  EXPECT_EQ(sections[1].level, 1);
  EXPECT_EQ(sections[2].level, 2);
}

// Contract C6: the chapter count is bounded; sections past the cap are not
// chapter boundaries.
TEST(Fb2MetadataParser, ChapterCountIsCappedAtFb2MaxChapters) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/tower.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, fb2test::makeSectionTowerFb2(Fb2::FB2_MAX_CHAPTERS + 80, 2)));

  CollectingParser parser(path);
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getSections().size(), static_cast<size_t>(Fb2::FB2_MAX_CHAPTERS));
}

// Hostile nesting must be bounded and deterministic, never a crash.
TEST(Fb2MetadataParser, DeeplyNestedSectionsParseWithoutUnboundedGrowth) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/deep.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, fb2test::makeSectionTowerFb2(64, 64)));

  CollectingParser parser(path);
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 64u);
  EXPECT_EQ(sections[0].level, 0);
  EXPECT_EQ(sections[63].level, 63);
}

// FB2 convention: bodies after the first with a name attribute
// (name="notes"/"comments") hold auxiliary content. Their sections must not
// become reading chapters or TOC entries.
// Contract C1: a later body WITHOUT a name attribute is still a reading body,
// and its sections restart at level 0.
TEST(Fb2MetadataParser, SecondUnnamedBodyStillContributesChapters) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/two-bodies.fb2";
  ASSERT_TRUE(fb2test::writeAll(path,
                                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
                                "<description><title-info><book-title>Two Bodies</book-title>"
                                "<lang>en</lang></title-info></description>\n"
                                "<body><section><title><p>First Body</p></title><p>alpha</p>"
                                "<section><title><p>First Child</p></title><p>beta</p></section></section></body>\n"
                                "<body><section><title><p>Second Body</p></title><p>gamma</p></section></body>\n"
                                "</FictionBook>\n"));

  CollectingParser parser(path);
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].title, "First Body");
  EXPECT_EQ(sections[1].title, "First Child");
  EXPECT_EQ(sections[2].title, "Second Body");
  EXPECT_EQ(sections[0].level, 0);
  EXPECT_EQ(sections[1].level, 1);
  EXPECT_EQ(sections[2].level, 0);
}

TEST(Fb2MetadataParser, NotesBodySectionsAreNotReadingSections) {
  CollectingParser parser(fixturePath("notes-body.fb2"));
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 1u);
  EXPECT_EQ(sections[0].title, "Story");
}

TEST(Fb2MetadataParser, UnnamedSecondBodyIsStillReadingContent) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/twobody.fb2";
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<FictionBook><body>"
      "<section><title><p>One</p></title><p>a</p></section>"
      "</body><body>"
      "<section><title><p>Two</p></title><p>b</p></section>"
      "</body></FictionBook>\n";
  ASSERT_TRUE(fb2test::writeAll(path, doc));
  CollectingParser parser(path);
  ASSERT_TRUE(parser.parse());
  ASSERT_EQ(parser.getSections().size(), 2u);
  EXPECT_EQ(parser.getSections()[0].title, "One");
  EXPECT_EQ(parser.getSections()[1].title, "Two");
}

TEST(Fb2MetadataParser, TruncatedXmlFailsParse) {
  CollectingParser parser(fixturePath("malformed-truncated.fb2"));
  EXPECT_FALSE(parser.parse());
}

// Documents current behavior: the parser never validates the root element, so
// any well-formed XML "parses" and falls back to one whole-file section.
TEST(Fb2MetadataParser, WrongRootElementParsesWithWholeFileFallback) {
  const std::string raw = readAll(fixturePath("wrong-root.fb2"));
  CollectingParser parser(fixturePath("wrong-root.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "");
  ASSERT_EQ(parser.getSections().size(), 1u);
  EXPECT_EQ(parser.getSections()[0].fileOffset, 0u);
  EXPECT_EQ(parser.getSections()[0].length, raw.size());
}

TEST(Fb2MetadataParser, SectionlessBodyFallsBackToOneWholeFileSection) {
  const std::string raw = readAll(fixturePath("no-sections.fb2"));
  CollectingParser parser(fixturePath("no-sections.fb2"));
  ASSERT_TRUE(parser.parse());
  ASSERT_EQ(parser.getSections().size(), 1u);
  // The book title stands in for the section title.
  EXPECT_EQ(parser.getSections()[0].title, "Plain Stream");
  EXPECT_EQ(parser.getSections()[0].fileOffset, 0u);
  EXPECT_EQ(parser.getSections()[0].length, raw.size());
}

// Unbalanced <section> markup is an XML well-formedness error: expat rejects it,
// so the parse fails deterministically instead of leaving the section stack in a
// half-open state.
TEST(Fb2MetadataParser, UnbalancedSectionTagsFailParseDeterministically) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());

  const std::string unclosed = tmp.path() + "/unclosed.fb2";
  ASSERT_TRUE(fb2test::writeAll(unclosed,
                                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
                                "<description><title-info><book-title>Unbalanced</book-title>"
                                "<lang>en</lang></title-info></description>\n"
                                "<body><section><title><p>Open</p></title><p>alpha</p>"
                                "<section><title><p>Never closed</p></title><p>beta</p>\n"));
  CollectingParser openParser(unclosed);
  EXPECT_FALSE(openParser.parse());

  const std::string stray = tmp.path() + "/stray-close.fb2";
  ASSERT_TRUE(fb2test::writeAll(stray,
                                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
                                "<description><title-info><book-title>Stray</book-title>"
                                "<lang>en</lang></title-info></description>\n"
                                "<body></section><section><title><p>After</p></title>"
                                "<p>gamma</p></section></body>\n</FictionBook>\n"));
  CollectingParser strayParser(stray);
  EXPECT_FALSE(strayParser.parse());
}

TEST(Fb2MetadataParser, EmptyFileFailsParse) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const std::string path = tmp.path() + "/empty.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, ""));
  CollectingParser parser(path);
  EXPECT_FALSE(parser.parse());
}

TEST(Fb2MetadataParser, MissingFileFailsParse) {
  CollectingParser parser("/nonexistent/definitely-not-here.fb2");
  EXPECT_FALSE(parser.parse());
}

// windows-1251 covers most Russian FB2s in the wild; the registered
// unknown-encoding handler must map the cp1251 bytes to UTF-8 output.
TEST(Fb2MetadataParser, DeclaredWindows1251EncodingDecodesToUtf8) {
  CollectingParser parser(fixturePath("cp1251-declared.fb2"));
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
  CollectingParser parser(fixturePath("cp1252-declared.fb2"));
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
  CollectingParser parser(path);
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
  CollectingParser parser(path);
  EXPECT_FALSE(parser.parse());
}

TEST(Fb2MetadataParser, Utf8CyrillicTitlesSurviveIntact) {
  CollectingParser parser(fixturePath("unicode-titles.fb2"));
  ASSERT_TRUE(parser.parse());
  EXPECT_EQ(parser.getTitle(), "Война и мир");
  EXPECT_EQ(parser.getAuthor(), "Лев Толстой");
  EXPECT_EQ(parser.getLanguage(), "ru");
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].title, "Глава первая");
  EXPECT_EQ(sections[1].title, "Глава вторая");
}

// A sink that cannot store a chapter (e.g. the SD card is full) must fail the parse,
// not leave a book with a chapter list that has holes in it.
TEST(Fb2MetadataParser, SinkWriteFailureFailsTheParse) {
  CollectingParser parser(fixturePath("nested-sections.fb2"), /*failWrites=*/true);
  EXPECT_FALSE(parser.parse());
  EXPECT_EQ(parser.writes, 1) << "parsing must stop at the first failed write";
}

// Contract L1-L8: a section with no <title> of its own is labelled from its own
// first paragraph, so the chapter list reads as words instead of "Unnamed".
class Fb2LabelTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(tmp.valid()); }

  const std::vector<Fb2::SectionInfo>& parseSource(const std::string& source) {
    path_ = tmp.path() + "/labels.fb2";
    EXPECT_TRUE(writeAll(path_, source));
    parser_ = std::make_unique<CollectingParser>(path_);
    EXPECT_TRUE(parser_->parse());
    return parser_->getSections();
  }

  fb2test::TempDir tmp;
  std::string path_;
  std::unique_ptr<CollectingParser> parser_;
};

TEST_F(Fb2LabelTest, RealTitleWinsAndIsNotMarkedDerived) {
  // L1: a <title> the book supplies is stored as-is, uncapped and unflagged.
  const auto& sections = parseSource(fb2test::makeSectionTowerFb2(3, 1));
  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].title, "Section number 0 of the tower");
  EXPECT_EQ(sections[0].titleDerived, 0);
}

TEST_F(Fb2LabelTest, UntitledSectionTakesItsOwnFirstParagraph) {
  // L2 and L4: the first <p> becomes the label; the second never joins it.
  const auto& sections = parseSource(fb2test::makeUntitledSectionsFb2(4, 1));
  ASSERT_EQ(sections.size(), 4u);
  EXPECT_EQ(sections[2].title, "Body of section 2");
  EXPECT_EQ(sections[2].titleDerived, 1);
  EXPECT_EQ(sections[2].title.find("Second paragraph"), std::string::npos);
}

TEST_F(Fb2LabelTest, EpigraphOnlySectionIsStillLabelled) {
  // L2: the reference anthology opens two sections with an <epigraph> and no bare
  // <p>. Requiring a direct child left those showing "Unnamed"; the paragraph is
  // still the section's own text, so it labels the section.
  const std::string source =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Epi</book-title></title-info></description>\n<body>\n"
      "<section><epigraph><p>Great is the power of wonder</p></epigraph><empty-line/></section>\n"
      "</body>\n</FictionBook>\n";
  const auto& sections = parseSource(source);
  ASSERT_EQ(sections.size(), 1u);
  EXPECT_EQ(sections[0].title, "Great is the power of wonder");
  EXPECT_EQ(sections[0].titleDerived, 1);
}

TEST_F(Fb2LabelTest, AChildSectionsTextNeverLabelsItsParent) {
  // L3: "its own" excludes descendants, the same rule a child's <title> follows.
  const std::string source =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Nest</book-title></title-info></description>\n<body>\n"
      "<section><section><p>Child text only</p></section></section>\n"
      "</body>\n</FictionBook>\n";
  const auto& sections = parseSource(source);
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_TRUE(sections[0].title.empty()) << "parent borrowed its child's text";
  EXPECT_EQ(sections[0].titleDerived, 0);
  EXPECT_EQ(sections[1].title, "Child text only");
}

TEST_F(Fb2LabelTest, WhitespaceIsCollapsedAndStripped) {
  // L5.
  const std::string source =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Space</book-title></title-info></description>\n<body>\n"
      "<section><p>\n   spaced   out\n   text\n</p></section>\n"
      "</body>\n</FictionBook>\n";
  const auto& sections = parseSource(source);
  ASSERT_EQ(sections.size(), 1u);
  EXPECT_EQ(sections[0].title, "spaced out text");
}

TEST_F(Fb2LabelTest, LabelIsCutOnACharacterBoundaryNotAByteBoundary) {
  // L6: 80 Cyrillic characters (160 bytes) must cut to 64 characters, and the
  // result must still be valid UTF-8 - a byte-wise cut would split a character.
  const auto& sections = parseSource(fb2test::makeUntitledSectionsFb2(3, 1));
  ASSERT_GE(sections.size(), 1u);
  const std::string& label = sections[0].title;
  EXPECT_EQ(sections[0].titleDerived, 1);
  size_t chars = 0;
  for (size_t i = 0; i < label.size();) {
    const unsigned char c = static_cast<unsigned char>(label[i]);
    const size_t width = c < 0x80 ? 1 : (c >> 5) == 0x06 ? 2 : (c >> 4) == 0x0E ? 3 : 4;
    ASSERT_LE(i + width, label.size()) << "label was cut mid-character";
    i += width;
    chars++;
  }
  EXPECT_EQ(chars, static_cast<size_t>(Fb2::FB2_MAX_LABEL_CHARS));
}

TEST_F(Fb2LabelTest, SectionWithNeitherTitleNorTextKeepsAnEmptyLabel) {
  // L7: the UI substitutes the localized placeholder at display time.
  const auto& sections = parseSource(fb2test::makeUntitledSectionsFb2(4, 1));
  ASSERT_GE(sections.size(), 2u);
  EXPECT_TRUE(sections[1].title.empty());
  EXPECT_EQ(sections[1].titleDerived, 0);
}

TEST_F(Fb2LabelTest, DerivingALabelChangesNoOffsetLengthOrLevel) {
  // L8: labels are display data; the chapter model is untouched.
  const std::string source = fb2test::makeUntitledSectionsFb2(6, 2);
  const auto& sections = parseSource(source);
  ASSERT_EQ(sections.size(), 6u);
  size_t partitioned = 0;
  for (size_t i = 0; i < sections.size(); i++) {
    EXPECT_EQ(sections[i].fileOffset, nthOccurrence(source, "<section", static_cast<int>(i)))
        << "chapter " << i << " offset moved";
    partitioned += sections[i].length;
  }
  EXPECT_GT(partitioned, 0u);
  EXPECT_EQ(sections[0].level, 0);
  EXPECT_EQ(sections[1].level, 1);
}

}  // namespace
