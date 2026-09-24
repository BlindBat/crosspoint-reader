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

  // fileOffset is always the "<section" tag, in every chapter: it is what the
  // renderer and the chapter list index by, and this feature must not move it.
  // The span is a different question: chapter 0 is its body's first, so its weight
  // starts at the "<body" tag (contract C7'), while chapter 1's starts at its own.
  const size_t bodyPos = raw.find("<body");
  ASSERT_NE(bodyPos, std::string::npos);
  for (int i = 0; i < 2; i++) {
    const size_t openPos = nthOccurrence(raw, "<section", i);
    const size_t closePos = nthOccurrence(raw, "</section>", i);
    ASSERT_NE(openPos, std::string::npos);
    ASSERT_NE(closePos, std::string::npos);
    EXPECT_EQ(sections[i].fileOffset, openPos) << "section " << i;
    // The parser estimates the end as the closing tag's offset plus
    // strlen("section") + 3 == strlen("</section>").
    const size_t spanStart = i == 0 ? bodyPos : openPos;
    EXPECT_EQ(spanStart + sections[i].length, closePos + 10) << "section " << i;
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

  // The parent's own bytes are its full span minus the inner chapter's. It is also
  // its body's first chapter, so that span starts at the "<body" tag (contract C7'),
  // not at its own "<section" — the front matter between them is rendered into it.
  const size_t parentStart = raw.find("<body");
  ASSERT_NE(parentStart, std::string::npos);
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

// FR-001 at the ceiling: exactly FB2_CHAPTER_INDEX_LIMIT chapters are made, and the
// sections past it read as part of the chapter containing them, so their bytes are
// in that chapter's own length (measured against the source text).
TEST(Fb2MetadataParser, SectionsPastTheChapterLimitStayInTheirContainingChapter) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  const int children = Fb2::FB2_CHAPTER_INDEX_LIMIT + 2;  // the wrapper is chapter 0
  std::string source =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<FictionBook>\n"
      "<description><title-info><book-title>Wide</book-title><lang>en</lang></title-info></description>\n"
      "<body><section><title><p>Wrapper</p></title>\n";
  size_t overflowBytes = 0;
  for (int i = 0; i < children; i++) {
    const std::string child = "<section><p>c" + std::to_string(i) + "</p></section>\n";
    // Children 1..LIMIT-1 are chapters 1..LIMIT-1; the rest are past the limit.
    if (i >= Fb2::FB2_CHAPTER_INDEX_LIMIT - 1) overflowBytes += child.size() - 1;  // the newline is not a section byte
    source += child;
  }
  source += "</section></body>\n</FictionBook>\n";
  const std::string path = tmp.path() + "/wide.fb2";
  ASSERT_TRUE(fb2test::writeAll(path, source));

  CollectingParser parser(path);
  ASSERT_TRUE(parser.parse());
  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), static_cast<size_t>(Fb2::FB2_CHAPTER_INDEX_LIMIT));
  size_t childChapterBytes = 0;
  for (size_t i = 1; i < sections.size(); i++) childChapterBytes += sections[i].length;
  // The wrapper's own bytes are its span minus its child CHAPTERS: the overflow
  // children are not chapters, so their bytes stay in the wrapper.
  EXPECT_EQ(sections[0].length + childChapterBytes, fb2test::topLevelSectionBytes(source));
  EXPECT_GE(sections[0].length, overflowBytes) << "sections past the limit lost their bytes";
}

// A reading body's bytes ahead of its first <section> are rendered into that body's
// first chapter, so they carry its weight (contract C7'). body-prefix.fb2 puts 126 B of
// <title>/<epigraph> between the <body> tag at 188 and the first <section> at 314.
TEST(Fb2MetadataParser, BodyFrontMatterWeighsIntoTheBodysFirstChapter) {
  CollectingParser parser(fixturePath("body-prefix.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 2u);

  // Chapter 0 spans the <body> tag through its own </section>: 110 B of section
  // plus the 126 B of front matter ahead of it.
  EXPECT_EQ(sections[0].length, 236u);
  // Chapter 1 is untouched: its span still starts at its own <section>.
  EXPECT_EQ(sections[1].length, 113u);
  // cumulativeLength is accumulated by Fb2, not by this parser; the book-level
  // total is pinned in Fb2BookTest.BodyFrontMatterMovesTheProgressPercentage.
  EXPECT_EQ(sections[0].length + sections[1].length, 349u);

  // The offset must NOT move: it is what the renderer and the chapter list use.
  EXPECT_EQ(sections[0].fileOffset, 314u) << "fileOffset is the <section> tag, never the <body> tag";
}

// FR-005: an auxiliary body (a later <body> with a name attribute) carries no weight,
// and that includes its own front matter. notes-body.fb2 has a named second body.
TEST(Fb2MetadataParser, AuxiliaryBodyFrontMatterAddsNoWeight) {
  const std::string raw = readAll(fixturePath("notes-body.fb2"));
  CollectingParser parser(fixturePath("notes-body.fb2"));
  ASSERT_TRUE(parser.parse());

  size_t total = 0;
  for (const auto& section : parser.getSections()) total += section.length;

  // The oracle counts reading bodies only, so agreeing with it is exactly the
  // claim that the named body contributed nothing -- its prefix included.
  EXPECT_EQ(total, fb2test::topLevelSectionBytes(raw));
  EXPECT_LT(total, raw.size()) << "the named body's bytes must not be in the total";
}

// FR-006: with several reading bodies, each body's front matter weighs into that
// body's own first chapter, never into chapter 0 of the book.
TEST(Fb2MetadataParser, EachReadingBodyFrontMatterWeighsIntoItsOwnFirstChapter) {
  const std::string raw = readAll(fixturePath("multi-reading-body.fb2"));
  CollectingParser parser(fixturePath("multi-reading-body.fb2"));
  ASSERT_TRUE(parser.parse());

  const auto& sections = parser.getSections();
  ASSERT_EQ(sections.size(), 4u);

  const size_t body0 = raw.find("<body");
  const size_t body1 = raw.find("<body", body0 + 1);
  ASSERT_NE(body1, std::string::npos);

  // Chapters 0 and 2 are their bodies' firsts: each spans from its own body tag.
  for (size_t i = 0; i < 4; i++) {
    const size_t openPos = nthOccurrence(raw, "<section", static_cast<int>(i));
    const size_t closePos = nthOccurrence(raw, "</section>", static_cast<int>(i)) + 10;
    const size_t spanStart = i == 0 ? body0 : (i == 2 ? body1 : openPos);
    EXPECT_EQ(sections[i].length, closePos - spanStart) << "chapter " << i;
    EXPECT_EQ(sections[i].fileOffset, openPos) << "chapter " << i;
  }

  // Body 1's prefix went to chapter 2, so chapter 0 spans only its own body.
  EXPECT_LT(sections[0].length + sections[1].length, body1 - body0 + 1);
}

// FR-007: a reading body with no <section> already spans the whole file as one
// chapter. Nothing is added on top, and the oracle (which counts sections) agrees.
TEST(Fb2MetadataParser, NoSectionBodyKeepsItsWholeFileWeight) {
  // no-sections.fb2 has no <section> at all, so contract C9's whole-file fallback
  // gives it one chapter spanning the file. The section-counting oracle returns 0
  // here by construction, so the fallback is asserted directly: the body prefix
  // must NOT be added on top of a span that already covers everything.
  {
    const std::string raw = readAll(fixturePath("no-sections.fb2"));
    CollectingParser parser(fixturePath("no-sections.fb2"));
    ASSERT_TRUE(parser.parse());
    ASSERT_EQ(parser.getSections().size(), 1u);
    EXPECT_EQ(parser.getSections()[0].length, raw.size());
    EXPECT_EQ(fb2test::topLevelSectionBytes(raw), 0u) << "the oracle counts sections, and there are none";
  }

  // wrapper-only.fb2 does have sections, so the oracle applies unchanged.
  {
    const std::string raw = readAll(fixturePath("wrapper-only.fb2"));
    CollectingParser parser(fixturePath("wrapper-only.fb2"));
    ASSERT_TRUE(parser.parse());
    size_t total = 0;
    for (const auto& section : parser.getSections()) total += section.length;
    EXPECT_EQ(total, fb2test::topLevelSectionBytes(raw));
    EXPECT_LE(total, raw.size());
  }
}

// FR-008 / Constitution VI: malformed input clamps to zero rather than underflowing.
// No reported weight may exceed the file, and none may wrap.
TEST(Fb2MetadataParser, MalformedInputNeverWrapsOrExceedsTheFile) {
  for (const char* fixture : {"malformed-truncated.fb2", "wrong-root.fb2"}) {
    const std::string raw = readAll(fixturePath(fixture));
    CollectingParser parser(fixturePath(fixture));
    parser.parse();  // may fail; either way nothing may wrap

    size_t total = 0;
    for (const auto& section : parser.getSections()) {
      EXPECT_LE(section.length, raw.size()) << fixture;
      EXPECT_LE(section.fileOffset, raw.size()) << fixture;
      total += section.length;
    }
    EXPECT_LE(total, raw.size()) << fixture;
  }
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
