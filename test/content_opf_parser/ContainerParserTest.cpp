// META-INF/container.xml rootfile discovery (FR-044): the package document is
// the <rootfile> with media-type application/oebps-package+xml, and the
// parser must survive the malformed inputs an untrusted EPUB can carry.

#include <gtest/gtest.h>

#include <string>

#include "ContainerParser.h"
#include "ParserTestSupport.h"

using parsertest::feed;
using parsertest::feedBytewise;
using parsertest::nest;

namespace {

constexpr char kGood[] =
    R"(<?xml version="1.0" encoding="UTF-8"?>)"
    R"(<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">)"
    R"(<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>)"
    R"(</rootfiles></container>)";

std::string parsePath(const std::string& doc, bool* ok = nullptr, const size_t chunk = 512) {
  ContainerParser parser(doc.size());
  EXPECT_TRUE(parser.setup());
  const bool accepted = feed(parser, doc, chunk);
  if (ok) *ok = accepted;
  return parser.fullPath;
}

}  // namespace

TEST(ContainerParser, FindsPackageRootfile) {
  bool ok = false;
  EXPECT_EQ("OEBPS/content.opf", parsePath(kGood, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, BytewiseAndSmallChunkFeedsAgree) {
  EXPECT_EQ("OEBPS/content.opf", parsePath(kGood, nullptr, 7));

  const std::string doc = kGood;
  ContainerParser parser(doc.size());
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feedBytewise(parser, doc));
  EXPECT_EQ("OEBPS/content.opf", parser.fullPath);
}

TEST(ContainerParser, SkipsRootfilesWithOtherMediaTypes) {
  const std::string doc =
      "<container><rootfiles>"
      "<rootfile full-path=\"index.pdf\" media-type=\"application/pdf\"/>"
      "<rootfile full-path=\"book/package.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  EXPECT_EQ("book/package.opf", parsePath(doc));
}

TEST(ContainerParser, LastMatchingRootfileWins) {
  // Pins current behaviour: every matching rootfile overwrites fullPath.
  const std::string doc =
      "<container><rootfiles>"
      "<rootfile full-path=\"a.opf\" media-type=\"application/oebps-package+xml\"/>"
      "<rootfile full-path=\"b.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  EXPECT_EQ("b.opf", parsePath(doc));
}

TEST(ContainerParser, RootfileOutsideRootfilesIsIgnored) {
  const std::string doc =
      "<container><rootfile full-path=\"x.opf\" media-type=\"application/oebps-package+xml\"/>"
      "<rootfiles/></container>";
  EXPECT_EQ("", parsePath(doc));
}

TEST(ContainerParser, RootfileMissingFullPathIsIgnored) {
  const std::string doc =
      "<container><rootfiles><rootfile media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  EXPECT_EQ("", parsePath(doc));
}

TEST(ContainerParser, WrongRootElementYieldsNoPath) {
  const std::string doc =
      "<package><rootfiles><rootfile full-path=\"x.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></package>";
  bool ok = false;
  EXPECT_EQ("", parsePath(doc, &ok));
  EXPECT_TRUE(ok);  // well-formed, just not a container document
}

TEST(ContainerParser, ZeroByteEntryIsNeverFedAndYieldsNoPath) {
  // A zero-length META-INF/container.xml means readItemContentsToStream never
  // calls write(): no parse error is raised, and Epub::findContentOpfFile
  // fails on the empty fullPath instead.
  ContainerParser parser(0);
  ASSERT_TRUE(parser.setup());
  const uint8_t none = 0;
  EXPECT_EQ(0u, parser.write(&none, 0));  // a zero-length write must not finalise the parse
  EXPECT_EQ("", parser.fullPath);
}

TEST(ContainerParser, WhitespaceOnlyDocumentIsRejected) {
  bool ok = true;
  EXPECT_EQ("", parsePath("   \n\t ", &ok));
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, ContainerWithoutRootfilesYieldsNoPath) {
  bool ok = false;
  EXPECT_EQ("", parsePath("<container/>", &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ("", parsePath("<container><rootfiles/></container>", &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ("", parsePath("<container><rootfiles></rootfiles></container>", &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, NotXmlIsRejected) {
  bool ok = true;
  EXPECT_EQ("", parsePath("PK\x03\x04 this is not xml at all", &ok));
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, TruncatedAfterRootfileStillReportsError) {
  // Handlers already fired for the rootfile, but the final chunk is
  // unterminated so the write is short: Epub::findContentOpfFile fails the open.
  const std::string doc =
      "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/>";
  bool ok = true;
  EXPECT_EQ("OEBPS/content.opf", parsePath(doc, &ok));
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, TruncatedInsideAttributeYieldsNothing) {
  const std::string doc = "<container><rootfiles><rootfile full-path=\"OEBPS/con";
  bool ok = true;
  EXPECT_EQ("", parsePath(doc, &ok));
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, DeclaredSizeLargerThanDocumentNeverFinalisesButStillParses) {
  // Lying size (too large): the last chunk is not marked final, so no
  // end-of-document error can fire; the element handlers still ran.
  const std::string doc = kGood;
  ContainerParser parser(doc.size() + 100);
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feed(parser, doc));
  EXPECT_EQ("OEBPS/content.opf", parser.fullPath);
}

TEST(ContainerParser, DeclaredSizeSmallerThanDocumentFinalisesEarlyAndFails) {
  // Lying size (too small): the write that consumes the declared last byte is
  // handed to expat as the final chunk, so the still-open document is a parse
  // error. The short write is what tells the caller to abandon the EPUB.
  const std::string doc = kGood;
  const size_t head = 40;
  ContainerParser parser(head);
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(doc.data()), head));
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(doc.data()) + head, doc.size() - head));
  EXPECT_EQ("", parser.fullPath);
}

TEST(ContainerParser, WritesPastTheFinalChunkAreRejected) {
  // Trailing bytes after the declared size hit an already-finished parser.
  const std::string doc = kGood;
  ContainerParser parser(doc.size());
  ASSERT_TRUE(parser.setup());
  ASSERT_TRUE(feed(parser, doc));
  EXPECT_EQ("OEBPS/content.opf", parser.fullPath);
  const std::string trailing = "<extra/>";
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(trailing.data()), trailing.size()));
}

TEST(ContainerParser, WriteAfterErrorIsNoOp) {
  const std::string bad = "<container><rootfiles><rootfile full-path=\"a\" <<<";
  ContainerParser parser(bad.size() + 64);
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(bad.data()), bad.size()));
  const std::string more = "</rootfiles></container>";
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(more.data()), more.size()));
}

TEST(ContainerParser, InvalidUtf8InAttributeIsRejected) {
  std::string doc =
      "<container><rootfiles><rootfile full-path=\"\xC3\x28.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  bool ok = true;
  parsePath(doc, &ok);
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, UnknownEncodingDeclarationIsRejected) {
  std::string doc = kGood;
  doc.replace(doc.find("UTF-8"), 5, "X-NO-SUCH-ENCODING");
  bool ok = true;
  EXPECT_EQ("", parsePath(doc, &ok));
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, Latin1DeclarationIsTranscodedToUtf8) {
  // expat has ISO-8859-1 built in: the 0xE9 byte must surface as UTF-8 "é".
  std::string doc =
      "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>"
      "<container><rootfiles><rootfile full-path=\"caf\xE9/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  bool ok = false;
  EXPECT_EQ("caf\xC3\xA9/content.opf", parsePath(doc, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, Utf16DocumentIsDecoded) {
  const std::string ascii =
      "<container><rootfiles><rootfile full-path=\"u16.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  std::string doc = "\xFF\xFE";  // little-endian BOM
  for (const char c : ascii) {
    doc += c;
    doc += '\0';
  }
  bool ok = false;
  EXPECT_EQ("u16.opf", parsePath(doc, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, DeepNestingBeforeRootfilesDoesNotDisturbStateMachine) {
  const std::string doc =
      "<container>" + nest("junk", 2000, "") +
      "<rootfiles><rootfile full-path=\"deep.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles>"
      "</container>";
  bool ok = false;
  EXPECT_EQ("deep.opf", parsePath(doc, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, EntityExpansionIsNotPerformedWithGeneralEntitiesDisabled) {
  // Billion-laughs shape; with XML_GE=0 expat must not expand it into memory.
  std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE c [<!ENTITY a \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\">"
      "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"><!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
      "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">]>"
      "<container><rootfiles><rootfile full-path=\"&d;.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  bool ok = false;
  const std::string path = parsePath(doc, &ok);
  EXPECT_TRUE(ok);
  // XML_GE=0 rewrites every declared entity to its own literal reference, so
  // "&d;" stays four characters instead of expanding to 32,000.
  EXPECT_EQ("&d;.opf", path);
}

TEST(ContainerParser, Utf8BomBeforeTheDeclarationIsAccepted) {
  const std::string doc = std::string("\xEF\xBB\xBF") + kGood;
  bool ok = false;
  EXPECT_EQ("OEBPS/content.opf", parsePath(doc, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, Utf8BomMidDocumentIsRejected) {
  // A BOM is only legal at offset 0; anywhere else it is stray content.
  std::string doc = kGood;
  doc.insert(doc.find("<container"), "\xEF\xBB\xBF");
  bool ok = true;
  parsePath(doc, &ok);
  EXPECT_FALSE(ok);
}

TEST(ContainerParser, HugeAttributeCountOnRootfileIsHandled) {
  // The attribute walk is a NULL-terminated char* pair list; 4,000 junk
  // attributes must not stop it finding the two that matter.
  std::string atts;
  atts.reserve(4000 * 16);
  for (int i = 0; i < 4000; ++i) {
    atts += " a" + std::to_string(i) + "=\"v\"";
  }
  const std::string doc = "<container><rootfiles><rootfile" + atts +
                          " full-path=\"many.opf\" media-type=\"application/oebps-package+xml\"/>"
                          "</rootfiles></container>";
  bool ok = false;
  EXPECT_EQ("many.opf", parsePath(doc, &ok));
  EXPECT_TRUE(ok);
}

TEST(ContainerParser, DuplicateAttributeIsAWellFormednessError) {
  const std::string doc =
      "<container><rootfiles><rootfile full-path=\"a.opf\" full-path=\"b.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  bool ok = true;
  EXPECT_EQ("", parsePath(doc, &ok));
  EXPECT_FALSE(ok);
}
