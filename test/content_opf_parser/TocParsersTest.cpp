// EPUB 3 nav document and EPUB 2 NCX table-of-contents parsing (FR-045):
// entry nesting depth, href/anchor splitting relative to the document, URI
// decoding, and resilience to the malformed shapes an untrusted EPUB can ship.

#include <gtest/gtest.h>

#include <string>

#include "Epub/BookMetadataCache.h"
#include "ParserTestSupport.h"
#include "TocNavParser.h"
#include "TocNcxParser.h"

using parsertest::feed;
using parsertest::nest;

namespace {

const std::string kBase = "OEBPS/";

struct NavResult {
  BookMetadataCache cache;
  bool ok = false;
};

NavResult parseNav(const std::string& doc, const size_t chunk = 512) {
  NavResult r;
  TocNavParser parser(kBase, doc.size(), &r.cache);
  EXPECT_TRUE(parser.setup());
  r.ok = feed(parser, doc, chunk);
  return r;
}

NavResult parseNcx(const std::string& doc, const size_t chunk = 512) {
  NavResult r;
  TocNcxParser parser(kBase, doc.size(), &r.cache);
  EXPECT_TRUE(parser.setup());
  r.ok = feed(parser, doc, chunk);
  return r;
}

std::string navDoc(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
         "<head><title>Nav</title></head><body>" +
         body + "</body></html>";
}

const std::string kNavToc =
    "<nav epub:type=\"toc\"><h1>Contents</h1><ol>"
    "<li><a href=\"ch1.xhtml\">One</a><ol>"
    "<li><a href=\"ch1.xhtml#s1\">One.One</a></li>"
    "</ol></li>"
    "<li><a href=\"ch2.xhtml\">Two</a></li>"
    "</ol></nav>";

std::string ncxDoc(const std::string& navMap) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head/>"
         "<docTitle><text>Book</text></docTitle><navMap>" +
         navMap + "</navMap></ncx>";
}

std::string navPoint(const std::string& label, const std::string& src, const std::string& children = "") {
  return "<navPoint><navLabel><text>" + label + "</text></navLabel><content src=\"" + src + "\"/>" + children +
         "</navPoint>";
}

}  // namespace

// --- nav document -----------------------------------------------------------

TEST(TocNavParser, ParsesNestedListsWithDepthAndAnchors) {
  const NavResult r = parseNav(navDoc(kNavToc));
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(3u, r.cache.toc.size());
  EXPECT_EQ("One", r.cache.toc[0].title);
  EXPECT_EQ("OEBPS/ch1.xhtml", r.cache.toc[0].href);
  EXPECT_EQ("", r.cache.toc[0].anchor);
  EXPECT_EQ(1, r.cache.toc[0].level);
  EXPECT_EQ("One.One", r.cache.toc[1].title);
  EXPECT_EQ("s1", r.cache.toc[1].anchor);
  EXPECT_EQ(2, r.cache.toc[1].level);
  EXPECT_EQ("Two", r.cache.toc[2].title);
  EXPECT_EQ(1, r.cache.toc[2].level);
}

TEST(TocNavParser, SmallChunksProduceSameEntries) {
  const NavResult r = parseNav(navDoc(kNavToc), 3);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(3u, r.cache.toc.size());
  EXPECT_EQ("One.One", r.cache.toc[1].title);
}

TEST(TocNavParser, LandmarksNavIsIgnoredEvenWhenFirst) {
  const std::string body =
      "<nav epub:type=\"landmarks\"><ol><li><a href=\"cover.xhtml\">Cover</a></li></ol></nav>" + kNavToc;
  const NavResult r = parseNav(navDoc(body));
  ASSERT_EQ(3u, r.cache.toc.size());
  EXPECT_EQ("One", r.cache.toc[0].title);
}

TEST(TocNavParser, PlainTypeAttributeAlsoSelectsToc) {
  const NavResult r = parseNav(navDoc("<nav type=\"toc\"><ol><li><a href=\"a.xhtml\">A</a></li></ol></nav>"));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("OEBPS/a.xhtml", r.cache.toc[0].href);
}

TEST(TocNavParser, HrefsAreUriDecodedAndNormalised) {
  const NavResult r =
      parseNav(navDoc("<nav epub:type=\"toc\"><ol><li><a href=\"text/../ch%201.xhtml#sec%202\">X</a></li></ol></nav>"));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("OEBPS/ch 1.xhtml", r.cache.toc[0].href);
  EXPECT_EQ("sec 2", r.cache.toc[0].anchor);
}

TEST(TocNavParser, LabelTextInsideInlineChildrenIsConcatenated) {
  const NavResult r =
      parseNav(navDoc("<nav epub:type=\"toc\"><ol><li><a href=\"a.xhtml\"><span>Chapter</span> 1</a></li></ol></nav>"));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("Chapter 1", r.cache.toc[0].title);
}

TEST(TocNavParser, AnchorsWithoutHrefOrLabelAreDropped) {
  const std::string body =
      "<nav epub:type=\"toc\"><ol>"
      "<li><a>NoHref</a></li>"
      "<li><a href=\"empty.xhtml\"></a></li>"
      "<li><span>Not a link</span></li>"
      "<li><a href=\"ok.xhtml\">Ok</a></li>"
      "</ol></nav>";
  const NavResult r = parseNav(navDoc(body));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("Ok", r.cache.toc[0].title);
}

TEST(TocNavParser, ListItemsOutsideOlAreIgnored) {
  const NavResult r = parseNav(navDoc("<nav epub:type=\"toc\"><li><a href=\"a.xhtml\">A</a></li></nav>"));
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.cache.toc.empty());
}

TEST(TocNavParser, EntriesAfterTocNavAreNotCollected) {
  const std::string body = kNavToc + "<nav epub:type=\"page-list\"><ol><li><a href=\"p.xhtml\">1</a></li></ol></nav>";
  const NavResult r = parseNav(navDoc(body));
  EXPECT_EQ(3u, r.cache.toc.size());
}

TEST(TocNavParser, EmptyAndNonXmlDocumentsAreRejected) {
  // A zero-byte nav document is never written to, so nothing is collected.
  BookMetadataCache cache;
  TocNavParser parser(kBase, 0, &cache);
  ASSERT_TRUE(parser.setup());
  const uint8_t none = 0;
  EXPECT_EQ(0u, parser.write(&none, 0));  // a zero-length write must not finalise the parse
  EXPECT_TRUE(cache.toc.empty());

  EXPECT_FALSE(parseNav("   ").ok);
  // Unquoted attribute value: HTML tolerates it, XML does not.
  EXPECT_FALSE(parseNav("<html><body><nav epub:type=\"toc\"><ol><li><a href=a>x</a></li></ol></nav></body></html>").ok);
  EXPECT_FALSE(parseNav("not xml").ok);
}

TEST(TocNavParser, Utf8BomIsAccepted) {
  const NavResult r = parseNav(std::string("\xEF\xBB\xBF") + navDoc(kNavToc));
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(3u, r.cache.toc.size());
}

TEST(TocNavParser, EntityReferencesInLabelsAreNotExpanded) {
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE html [<!ENTITY big \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\">]>"
      "<html><body><nav epub:type=\"toc\"><ol><li><a href=\"a.xhtml\">&big;</a></li></ol></nav></body></html>";
  const NavResult r = parseNav(doc);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("&big;", r.cache.toc[0].title);
}

TEST(TocNavParser, HugeAttributeCountOnAnchorIsHandled) {
  std::string atts;
  atts.reserve(3000 * 16);
  for (int i = 0; i < 3000; ++i) {
    atts += " data-a" + std::to_string(i) + "=\"v\"";
  }
  const NavResult r = parseNav(
      navDoc("<nav epub:type=\"toc\"><ol><li><a" + atts + " href=\"many.xhtml\">Many</a></li></ol></nav>"), 1024);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("OEBPS/many.xhtml", r.cache.toc[0].href);
}

TEST(TocNavParser, HrefWithBackslashTraversalNormalisesToEmpty) {
  const NavResult r =
      parseNav(navDoc("<nav epub:type=\"toc\"><ol><li><a href=\"..\\..\\etc\">Esc</a></li></ol></nav>"));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("", r.cache.toc[0].href);
}

TEST(TocNavParser, TruncatedDocumentKeepsCompletedEntriesButReportsError) {
  const std::string full = navDoc(kNavToc);
  const NavResult r = parseNav(full.substr(0, full.find("<li><a href=\"ch2.xhtml\"")));
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(2u, r.cache.toc.size());
}

TEST(TocNavParser, InvalidUtf8AndUnknownEncodingAreRejected) {
  EXPECT_FALSE(parseNav(navDoc("<nav epub:type=\"toc\"><ol><li><a href=\"a\">\xC0\xAF</a></li></ol></nav>")).ok);
  std::string doc = navDoc(kNavToc);
  doc.replace(doc.find("UTF-8"), 5, "X-BOGUS");
  EXPECT_FALSE(parseNav(doc).ok);
}

TEST(TocNavParser, HostileListNestingSurvives) {
  // 300 nested <ol> overflow the uint8_t depth counter. Pins the wrap exactly:
  // the innermost entry is reported at level 300 % 256 = 44, the 256 surplus
  // closes wrap back to zero, and a sibling list afterwards is level 1 again.
  std::string open, close;
  for (int i = 0; i < 300; ++i) {
    open += "<ol><li>";
    close += "</li></ol>";
  }
  const NavResult r = parseNav(navDoc("<nav epub:type=\"toc\">" + open + "<a href=\"deep.xhtml\">Deep</a>" + close +
                                      "<ol><li><a href=\"after.xhtml\">After</a></li></ol></nav>"));
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(2u, r.cache.toc.size());
  EXPECT_EQ("Deep", r.cache.toc[0].title);
  EXPECT_EQ("OEBPS/deep.xhtml", r.cache.toc[0].href);
  EXPECT_EQ(44, r.cache.toc[0].level);
  EXPECT_EQ("After", r.cache.toc[1].title);
  EXPECT_EQ(1, r.cache.toc[1].level);
}

TEST(TocNavParser, DeclaredSizeSmallerThanDocumentFinalisesEarlyAndFails) {
  // Lying size (too small): the write consuming the declared last byte is
  // handed to expat as the final chunk while the list is still open, so it is
  // a parse error. Entries completed before the cut have already been emitted.
  BookMetadataCache cache;
  const std::string doc = navDoc(kNavToc);
  const size_t head = doc.find("<li><a href=\"ch2.xhtml\"");
  ASSERT_NE(std::string::npos, head);
  TocNavParser parser(kBase, head, &cache);
  ASSERT_TRUE(parser.setup());
  const auto* bytes = reinterpret_cast<const uint8_t*>(doc.data());
  EXPECT_EQ(0u, parser.write(bytes, head));
  EXPECT_EQ(0u, parser.write(bytes + head, doc.size() - head));
  EXPECT_EQ(2u, cache.toc.size());
}

TEST(TocNavParser, DeclaredSizeLargerThanDocumentNeverFinalisesButStillCollects) {
  // Lying size (too large): the last chunk is never marked final, so no
  // end-of-document error can fire; the handlers still ran.
  BookMetadataCache cache;
  const std::string doc = navDoc(kNavToc);
  TocNavParser parser(kBase, doc.size() * 3, &cache);
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feed(parser, doc));
  EXPECT_EQ(3u, cache.toc.size());
}

TEST(TocNavParser, WritesPastTheFinalChunkAreRejected) {
  BookMetadataCache cache;
  const std::string doc = navDoc(kNavToc);
  TocNavParser parser(kBase, doc.size(), &cache);
  ASSERT_TRUE(parser.setup());
  ASSERT_TRUE(feed(parser, doc));
  const std::string trailing = "<html/>";
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(trailing.data()), trailing.size()));
  EXPECT_EQ(3u, cache.toc.size());
}

TEST(TocNavParser, NoCachePointerStillParsesWithoutCrashing) {
  const std::string doc = navDoc(kNavToc);
  TocNavParser parser(kBase, doc.size(), nullptr);
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feed(parser, doc));
}

// --- NCX ------------------------------------------------------------------

TEST(TocNcxParser, ParsesNestedNavPointsWithDepthAndAnchors) {
  const std::string navMap = navPoint("One", "ch1.xhtml", navPoint("One.One", "ch1.xhtml#s1")) +
                             navPoint("Two", "text/ch2.xhtml");
  const NavResult r = parseNcx(ncxDoc(navMap));
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(3u, r.cache.toc.size());
  EXPECT_EQ("One", r.cache.toc[0].title);
  EXPECT_EQ("OEBPS/ch1.xhtml", r.cache.toc[0].href);
  EXPECT_EQ(1, r.cache.toc[0].level);
  EXPECT_EQ("One.One", r.cache.toc[1].title);
  EXPECT_EQ("s1", r.cache.toc[1].anchor);
  EXPECT_EQ(2, r.cache.toc[1].level);
  EXPECT_EQ("OEBPS/text/ch2.xhtml", r.cache.toc[2].href);
  EXPECT_EQ(1, r.cache.toc[2].level);
}

TEST(TocNcxParser, SmallChunksProduceSameEntries) {
  const NavResult r = parseNcx(ncxDoc(navPoint("One", "ch1.xhtml") + navPoint("Two", "ch2.xhtml")), 5);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(2u, r.cache.toc.size());
  EXPECT_EQ("Two", r.cache.toc[1].title);
}

TEST(TocNcxParser, SrcIsUriDecodedAndNormalised) {
  const NavResult r = parseNcx(ncxDoc(navPoint("X", "./a/../ch%201.xhtml#p%20q")));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("OEBPS/ch 1.xhtml", r.cache.toc[0].href);
  EXPECT_EQ("p q", r.cache.toc[0].anchor);
}

TEST(TocNcxParser, ContentBeforeLabelIsDropped) {
  // The parser emits at </content> and requires the label to precede it (NCX order).
  const std::string navMap = "<navPoint><content src=\"ch1.xhtml\"/><navLabel><text>Late</text></navLabel></navPoint>";
  const NavResult r = parseNcx(ncxDoc(navMap));
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.cache.toc.empty());
}

TEST(TocNcxParser, NavPointWithoutContentOrLabelIsDropped) {
  const std::string navMap = "<navPoint><navLabel><text>NoContent</text></navLabel></navPoint>" +
                             std::string("<navPoint><content src=\"nolabel.xhtml\"/></navPoint>") +
                             navPoint("Ok", "ok.xhtml");
  const NavResult r = parseNcx(ncxDoc(navMap));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("Ok", r.cache.toc[0].title);
}

TEST(TocNcxParser, DepthResetsAfterNestedNavPointsClose) {
  const std::string navMap = navPoint("A", "a.xhtml", navPoint("A1", "a1.xhtml", navPoint("A11", "a11.xhtml"))) +
                             navPoint("B", "b.xhtml");
  const NavResult r = parseNcx(ncxDoc(navMap));
  ASSERT_EQ(4u, r.cache.toc.size());
  EXPECT_EQ(3, r.cache.toc[2].level);
  EXPECT_EQ(1, r.cache.toc[3].level);
}

TEST(TocNcxParser, NavMapOutsideNcxRootIsIgnored) {
  const NavResult r = parseNcx("<navMap>" + navPoint("A", "a.xhtml") + "</navMap>");
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.cache.toc.empty());
}

TEST(TocNcxParser, EmptyAndNonXmlDocumentsAreRejected) {
  BookMetadataCache cache;
  TocNcxParser parser(kBase, 0, &cache);
  ASSERT_TRUE(parser.setup());
  const uint8_t none = 0;
  EXPECT_EQ(0u, parser.write(&none, 0));  // a zero-length write must not finalise the parse
  EXPECT_TRUE(cache.toc.empty());
  EXPECT_FALSE(parseNcx("   ").ok);
  EXPECT_FALSE(parseNcx(std::string("\x89PNG\r\n\x1A\n", 8)).ok);
}

TEST(TocNcxParser, TruncatedDocumentKeepsCompletedEntriesButReportsError) {
  const std::string full = ncxDoc(navPoint("One", "ch1.xhtml") + navPoint("Two", "ch2.xhtml"));
  const NavResult r = parseNcx(full.substr(0, full.rfind("<content")));
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(1u, r.cache.toc.size());
}

TEST(TocNcxParser, InvalidUtf8AndUnknownEncodingAreRejected) {
  EXPECT_FALSE(parseNcx(ncxDoc(navPoint("\xF8\x88\x80\x80\x80", "a.xhtml"))).ok);
  std::string doc = ncxDoc(navPoint("A", "a.xhtml"));
  doc.replace(doc.find("UTF-8"), 5, "X-BOGUS");
  EXPECT_FALSE(parseNcx(doc).ok);
}

TEST(TocNcxParser, HostileNavPointNestingSurvives) {
  // 300 nested <navPoint> overflow the uint8_t depth counter. Pins the wrap:
  // the innermost entry is reported at level 300 % 256 = 44, the surplus
  // closes are swallowed by the state guard, and a sibling is level 1 again.
  std::string open, close;
  for (int i = 0; i < 300; ++i) {
    open += "<navPoint>";
    close += "</navPoint>";
  }
  const NavResult r = parseNcx(ncxDoc(open + "<navLabel><text>Deep</text></navLabel><content src=\"d.xhtml\"/>" +
                                      close + navPoint("After", "after.xhtml")));
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(2u, r.cache.toc.size());
  EXPECT_EQ("Deep", r.cache.toc[0].title);
  EXPECT_EQ("OEBPS/d.xhtml", r.cache.toc[0].href);
  EXPECT_EQ(44, r.cache.toc[0].level);
  EXPECT_EQ("After", r.cache.toc[1].title);
  EXPECT_EQ(1, r.cache.toc[1].level);
}

TEST(TocNcxParser, DeclaredSizeLargerThanDocumentNeverFinalisesButStillCollects) {
  BookMetadataCache cache;
  const std::string doc = ncxDoc(navPoint("One", "ch1.xhtml") + navPoint("Two", "ch2.xhtml"));
  TocNcxParser parser(kBase, doc.size() * 3, &cache);
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feed(parser, doc));
  EXPECT_EQ(2u, cache.toc.size());
}

TEST(TocNcxParser, DeclaredSizeSmallerThanDocumentFinalisesEarlyAndFails) {
  // Lying size (too small): expat is told the first chunk is final while the
  // navMap is still open, so the write is short. Entries completed before the
  // cut have already reached the cache.
  BookMetadataCache cache;
  const std::string doc = ncxDoc(navPoint("One", "ch1.xhtml") + navPoint("Two", "ch2.xhtml"));
  const size_t head = doc.find("<navPoint>", doc.find("<navPoint>") + 1);
  TocNcxParser parser(kBase, head, &cache);
  ASSERT_TRUE(parser.setup());
  const auto* bytes = reinterpret_cast<const uint8_t*>(doc.data());
  EXPECT_EQ(0u, parser.write(bytes, head));
  EXPECT_EQ(0u, parser.write(bytes + head, doc.size() - head));
  EXPECT_EQ(1u, cache.toc.size());
}

TEST(TocNcxParser, WritesPastTheFinalChunkAreRejected) {
  BookMetadataCache cache;
  const std::string doc = ncxDoc(navPoint("One", "ch1.xhtml"));
  TocNcxParser parser(kBase, doc.size(), &cache);
  ASSERT_TRUE(parser.setup());
  ASSERT_TRUE(feed(parser, doc));
  const std::string trailing = "<ncx/>";
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(trailing.data()), trailing.size()));
  EXPECT_EQ(1u, cache.toc.size());
}

TEST(TocNcxParser, Utf8BomIsAccepted) {
  const NavResult r = parseNcx(std::string("\xEF\xBB\xBF") + ncxDoc(navPoint("One", "ch1.xhtml")));
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("One", r.cache.toc[0].title);
}

TEST(TocNcxParser, EntityReferencesAreNotExpanded) {
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE ncx [<!ENTITY a \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\">"
      "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\">]>"
      "<ncx><navMap><navPoint><navLabel><text>&b;</text></navLabel>"
      "<content src=\"c.xhtml\"/></navPoint></navMap></ncx>";
  const NavResult r = parseNcx(doc);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("&b;", r.cache.toc[0].title);
}

TEST(TocNcxParser, HugeAttributeCountOnContentIsHandled) {
  std::string atts;
  atts.reserve(3000 * 16);
  for (int i = 0; i < 3000; ++i) {
    atts += " a" + std::to_string(i) + "=\"v\"";
  }
  const std::string navMap = "<navPoint><navLabel><text>Many</text></navLabel><content" + atts +
                             " src=\"many.xhtml\"/></navPoint>";
  const NavResult r = parseNcx(ncxDoc(navMap), 1024);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("OEBPS/many.xhtml", r.cache.toc[0].href);
}

TEST(TocNcxParser, SrcWithBackslashTraversalNormalisesToEmpty) {
  // FsHelpers::normalisePath refuses backslashes outright, so the entry is
  // recorded with an empty path rather than escaping the base directory.
  const NavResult r = parseNcx(ncxDoc(navPoint("Esc", "..\\..\\etc\\passwd")));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("", r.cache.toc[0].href);
}

TEST(TocNcxParser, SrcEscapingTheBaseDirectoryIsClamped) {
  const NavResult r = parseNcx(ncxDoc(navPoint("Up", "../../../secret.xhtml")));
  ASSERT_EQ(1u, r.cache.toc.size());
  EXPECT_EQ("secret.xhtml", r.cache.toc[0].href);
}
