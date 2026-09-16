#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "OpdsParser.h"

// The OPDS parser consumes Atom XML streamed straight from an arbitrary HTTP server
// (OpdsBookBrowserActivity -> HttpDownloader -> OpdsParserStream -> OpdsParser), so every
// byte it sees is untrusted. These tests cover both the happy path and hostile-server
// responses: the parser must either succeed or fail gracefully (error()/truncated()),
// never crash, and never store unbounded strings.

namespace {

// Feeds the whole document then finalizes, the same sequence OpdsParserStream performs.
void feed(OpdsParser& parser, const std::string& xml) {
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();
}

std::string wrapFeed(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n" +
         body + "</feed>\n";
}

std::string bookEntry(const std::string& title, const std::string& author, const std::string& href) {
  return "<entry><title>" + title + "</title><author><name>" + author +
         "</name></author>"
         "<id>urn:test:" +
         title + "</id><link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\"" + href +
         "\"/></entry>\n";
}

}  // namespace

TEST(OpdsParser, ParsesEntriesAuthorsLinksAndPaginationFromMinimalFeed) {
  OpdsParser parser;
  feed(parser, wrapFeed("<link rel=\"next\" href=\"/catalog?page=2\"/>"
                        "<link rel=\"previous\" href=\"/catalog?page=0\"/>"
                        "<link rel=\"search\" type=\"application/atom+xml\" "
                        "href=\"/search?q={searchTerms}\"/>" +
                        bookEntry("Moby Dick", "Herman Melville", "/books/moby.epub") +
                        "<entry><title>Science Fiction</title><id>urn:nav:sf</id>"
                        "<link type=\"application/atom+xml;profile=opds-catalog\" href=\"/catalog/sf\"/></entry>"));

  ASSERT_FALSE(parser.error());
  EXPECT_FALSE(parser.truncated());

  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 2u);

  EXPECT_EQ(entries[0].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[0].title, "Moby Dick");
  EXPECT_EQ(entries[0].author, "Herman Melville");
  EXPECT_EQ(entries[0].href, "/books/moby.epub");
  EXPECT_EQ(entries[0].id, "urn:test:Moby Dick");

  EXPECT_EQ(entries[1].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[1].title, "Science Fiction");
  EXPECT_EQ(entries[1].href, "/catalog/sf");

  EXPECT_EQ(parser.getNextPageUrl(), "/catalog?page=2");
  EXPECT_EQ(parser.getPrevPageUrl(), "/catalog?page=0");
  EXPECT_EQ(parser.getSearchTemplate(), "/search?q={searchTerms}");
  EXPECT_EQ(
      std::count_if(entries.begin(), entries.end(), [](const OpdsEntry& e) { return e.type == OpdsEntryType::BOOK; }),
      1);
}

TEST(OpdsParser, ByteAtATimeStreamingMatchesBulkWrite) {
  OpdsParser parser;
  const std::string xml = wrapFeed(bookEntry("Dune", "Frank Herbert", "/dune.epub"));
  for (const char c : xml) parser.write(static_cast<uint8_t>(c));
  parser.flush();

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Dune");
  EXPECT_EQ(parser.getEntries()[0].author, "Frank Herbert");
}

TEST(OpdsParser, EntryWithoutLinkIsDropped) {
  OpdsParser parser;
  feed(parser,
       wrapFeed("<entry><title>No Link Here</title><id>urn:x</id></entry>" + bookEntry("Kept", "A", "/kept.epub")));

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Kept");
}

TEST(OpdsParser, EntryWithoutTitleIsDropped) {
  OpdsParser parser;
  feed(parser, wrapFeed("<entry><id>urn:x</id>"
                        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
                        "href=\"/orphan.epub\"/></entry>" +
                        bookEntry("Kept", "A", "/kept.epub")));

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].href, "/kept.epub");
}

TEST(OpdsParser, ExpandsXmlEntitiesInTitles) {
  OpdsParser parser;
  feed(parser, wrapFeed(bookEntry("Pride &amp; Prejudice &lt;annotated&gt; Caf&#233;", "Jane", "/pp.epub")));

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  // &#233; is e-acute, UTF-8 encoded as 0xC3 0xA9.
  EXPECT_EQ(parser.getEntries()[0].title, "Pride & Prejudice <annotated> Caf\xC3\xA9");
}

TEST(OpdsParser, UndefinedHtmlEntityFailsGracefully) {
  // Servers sometimes emit HTML entities (&nbsp;) in what claims to be Atom XML.
  // Expat rejects undefined entities; the parser must surface an error, not crash.
  OpdsParser parser;
  feed(parser, wrapFeed(bookEntry("Bad&nbsp;Title", "A", "/x.epub")));
  EXPECT_TRUE(parser.error());
}

TEST(OpdsParser, PrefixedElementNamesAreRecognized) {
  // Expat runs without namespace processing, so prefixed tag names arrive verbatim.
  OpdsParser parser;
  feed(parser,
       "<atom:feed xmlns:atom=\"http://www.w3.org/2005/Atom\">"
       "<atom:entry><atom:title>Prefixed</atom:title>"
       "<atom:author><atom:name>Ns Author</atom:name></atom:author>"
       "<atom:link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\"/p.epub\"/>"
       "</atom:entry></atom:feed>");

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Prefixed");
  EXPECT_EQ(parser.getEntries()[0].author, "Ns Author");
  EXPECT_EQ(parser.getEntries()[0].type, OpdsEntryType::BOOK);
}

TEST(OpdsParser, MismatchedTagsSetErrorAndKeepCompleteEntries) {
  OpdsParser parser;
  // First entry is complete; then the document closes a tag that was never opened.
  feed(parser, wrapFeed(bookEntry("Complete", "A", "/c.epub") + "<entry><title>Broken</wrong>"));

  EXPECT_TRUE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Complete");
}

TEST(OpdsParser, TruncatedFeedMidEntrySetsErrorOnFlush) {
  OpdsParser parser;
  const std::string full = wrapFeed(bookEntry("First", "A", "/1.epub") + bookEntry("Second", "B", "/2.epub"));
  // Cut the connection in the middle of the second entry.
  const std::string cut = full.substr(0, full.find("/2.epub"));

  parser.write(reinterpret_cast<const uint8_t*>(cut.data()), cut.size());
  EXPECT_FALSE(parser.error());  // Streaming chunks can end mid-document.
  parser.flush();

  EXPECT_TRUE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "First");
}

TEST(OpdsParser, NonXmlResponseSetsError) {
  OpdsParser parser;
  feed(parser, "404 Not Found: the catalog you requested does not exist\r\n");
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.getEntries().empty());
}

TEST(OpdsParser, HtmlErrorPageYieldsNoEntries) {
  // Well-formed XML that simply is not an OPDS feed (e.g. an XHTML error page).
  OpdsParser parser;
  feed(parser, "<html><head><title>502 Bad Gateway</title></head><body><p>upstream error</p></body></html>");
  EXPECT_FALSE(parser.error());
  EXPECT_TRUE(parser.getEntries().empty());
  EXPECT_TRUE(parser.getNextPageUrl().empty());
}

TEST(OpdsParser, EmptyResponseSetsError) {
  OpdsParser parser;
  parser.flush();
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.getEntries().empty());
}

TEST(OpdsParser, HugeAttributeValuesAreBoundedNotStored) {
  // A hostile server can send megabyte-long attribute values; stored copies must be
  // clamped so the ~380KB heap is not exhausted.
  constexpr size_t kHugeLen = 64 * 1024;
  const std::string hugeHref(kHugeLen, 'a');

  OpdsParser parser;
  feed(parser, wrapFeed("<link rel=\"next\" href=\"" + hugeHref +
                        "\"/>"
                        "<entry><title>T</title>"
                        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\"" +
                        hugeHref + "\"/></entry>"));

  ASSERT_FALSE(parser.error());
  EXPECT_LE(parser.getNextPageUrl().size(), 768u);
  EXPECT_GT(parser.getNextPageUrl().size(), 0u);
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_LE(parser.getEntries()[0].href.size(), 768u);
}

TEST(OpdsParser, HugeTextContentIsBounded) {
  const std::string hugeTitle(128 * 1024, 'T');
  const std::string hugeAuthor(64 * 1024, 'A');

  OpdsParser parser;
  feed(parser, wrapFeed(bookEntry(hugeTitle, hugeAuthor, "/x.epub")));

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_LE(parser.getEntries()[0].title.size(), 160u);
  EXPECT_GT(parser.getEntries()[0].title.size(), 0u);
  EXPECT_LE(parser.getEntries()[0].author.size(), 120u);
}

TEST(OpdsParser, DeeplyNestedElementsDoNotCrash) {
  constexpr int kDepth = 20000;
  std::string body;
  body.reserve(kDepth * 7 + 64);
  for (int i = 0; i < kDepth; ++i) body += "<d>";
  for (int i = 0; i < kDepth; ++i) body += "</d>";
  body += bookEntry("After Nesting", "A", "/deep.epub");

  OpdsParser parser;
  feed(parser, wrapFeed(body));

  // Expat imposes no depth limit; the parser must survive and keep working afterwards.
  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "After Nesting");
}

TEST(OpdsParser, EntryCountIsCappedAndReportedAsTruncated) {
  // ENTRY_STORAGE_CAPACITY is 64 with MAX_ENTRIES = 62; a huge catalog page must not
  // grow the entries vector without bound.
  std::string body;
  for (int i = 0; i < 100; ++i) {
    body += bookEntry("Book " + std::to_string(i), "A", "/b" + std::to_string(i) + ".epub");
  }

  OpdsParser parser;
  feed(parser, wrapFeed(body));

  ASSERT_FALSE(parser.error());
  EXPECT_EQ(parser.getEntries().size(), 62u);
  EXPECT_TRUE(parser.truncated());
  EXPECT_EQ(parser.getEntries().front().title, "Book 0");
  EXPECT_EQ(parser.getEntries().back().title, "Book 61");
}

TEST(OpdsParser, PaginationLinksInsideEntriesAreIgnored) {
  OpdsParser parser;
  feed(parser, wrapFeed("<entry><title>T</title>"
                        "<link rel=\"next\" href=\"/entry-scoped-next\"/>"
                        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
                        "href=\"/t.epub\"/></entry>"
                        "<link rel=\"next\" href=\"/feed-next\"/>"));

  ASSERT_FALSE(parser.error());
  EXPECT_EQ(parser.getNextPageUrl(), "/feed-next");
  EXPECT_TRUE(parser.getPrevPageUrl().empty());
}

TEST(OpdsParser, SearchLinkWithoutTemplatePlaceholderIsIgnored) {
  OpdsParser parser;
  feed(parser, wrapFeed("<link rel=\"search\" href=\"/opensearch.xml\"/>" + bookEntry("T", "A", "/t.epub")));

  ASSERT_FALSE(parser.error());
  EXPECT_TRUE(parser.getSearchTemplate().empty());
}

TEST(OpdsParser, PrefersPlainEpubAcquisitionLinkOverDerivedFormat) {
  OpdsParser parser;
  feed(parser, wrapFeed("<entry><title>T</title>"
                        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
                        "href=\"/download/12/kepub\"/>"
                        "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
                        "href=\"/download/12/book.epub\"/></entry>"));

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].href, "/download/12/book.epub");
}

TEST(OpdsParser, ClearResetsAllParsedState) {
  OpdsParser parser;
  feed(parser, wrapFeed("<link rel=\"next\" href=\"/n\"/>" + bookEntry("T", "A", "/t.epub")));
  ASSERT_EQ(parser.getEntries().size(), 1u);

  parser.clear();
  EXPECT_TRUE(parser.getEntries().empty());
  EXPECT_TRUE(parser.getNextPageUrl().empty());
  EXPECT_TRUE(parser.getPrevPageUrl().empty());
  EXPECT_TRUE(parser.getSearchTemplate().empty());
  EXPECT_FALSE(parser.truncated());
}
