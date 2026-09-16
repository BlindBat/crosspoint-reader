// content.opf package parsing (FR-044, FR-046): metadata capture, manifest
// href resolution, spine construction against the manifest, cover selection
// precedence, TOC/CSS discovery, guide references, and the malformed corpus.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ContentOpfParser.h"
#include "Epub/BookMetadataCache.h"
#include "ParserTestSupport.h"

using parsertest::feed;
using parsertest::nest;
using parsertest::TempDir;

namespace {

const std::string kBase = "OEBPS/";

std::string opf(const std::string& metadata, const std::string& manifest, const std::string& spine,
                const std::string& guide = "") {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<package xmlns=\"http://www.idpf.org/2007/opf\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
         "version=\"3.0\"><metadata>" +
         metadata + "</metadata><manifest>" + manifest + "</manifest><spine>" + spine + "</spine>" +
         (guide.empty() ? "" : "<guide>" + guide + "</guide>") + "</package>";
}

const std::string kMeta = "<dc:title>Main Title</dc:title><dc:creator>Ada</dc:creator><dc:language>en</dc:language>";
const std::string kManifest =
    "<item id=\"ch1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"ch2\" href=\"text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
    "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
    "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>";
const std::string kSpine = "<itemref idref=\"ch1\"/><itemref idref=\"ch2\"/>";

struct Parsed {
  BookMetadataCache cache;
  std::string title, author, language, tocNcxPath, tocNavPath, coverItemHref, guideCoverPageHref, textReferenceHref;
  std::vector<std::string> cssFiles;
  bool ok = false;
  bool itemsBinLeftBehind = false;
};

Parsed parse(const std::string& doc, const bool withCache = true, const size_t chunk = 512) {
  TempDir dir;
  Parsed out;
  {
    ContentOpfParser parser(dir.path(), kBase, doc.size(), withCache ? &out.cache : nullptr);
    EXPECT_TRUE(parser.setup());
    out.ok = feed(parser, doc, chunk);
    out.title = parser.title;
    out.author = parser.author;
    out.language = parser.language;
    out.tocNcxPath = parser.tocNcxPath;
    out.tocNavPath = parser.tocNavPath;
    out.coverItemHref = parser.coverItemHref;
    out.guideCoverPageHref = parser.guideCoverPageHref;
    out.textReferenceHref = parser.textReferenceHref;
    out.cssFiles = parser.cssFiles;
  }
  out.itemsBinLeftBehind = Storage.exists((dir.path() + "/.items.bin").c_str());
  return out;
}

// Makes every openFileForWrite fail for the lifetime of the guard, so a failed
// assertion can never leak the flag into the next test.
class FailWritesGuard {
 public:
  FailWritesGuard() { Storage.failWrites = true; }
  ~FailWritesGuard() { Storage.failWrites = false; }
  FailWritesGuard(const FailWritesGuard&) = delete;
  FailWritesGuard& operator=(const FailWritesGuard&) = delete;
};

}  // namespace

TEST(ContentOpfParser, CapturesTitleAuthorLanguage) {
  const Parsed p = parse(opf(kMeta, kManifest, kSpine));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("Main Title", p.title);
  EXPECT_EQ("Ada", p.author);
  EXPECT_EQ("en", p.language);
}

TEST(ContentOpfParser, OnlyFirstTitleIsKept) {
  const Parsed p = parse(opf("<dc:title>First</dc:title><dc:title>Subtitle</dc:title>", kManifest, kSpine));
  EXPECT_EQ("First", p.title);
}

TEST(ContentOpfParser, MultipleCreatorsJoinedWithCommaSpace) {
  const Parsed p =
      parse(opf("<dc:title>T</dc:title><dc:creator>Ada</dc:creator><dc:creator>Bob</dc:creator>", kManifest, kSpine));
  EXPECT_EQ("Ada, Bob", p.author);
}

TEST(ContentOpfParser, TitleSplitAcrossExpatChunksIsReassembled) {
  // A 1-byte chunk size forces the character-data callback to fire per byte.
  const Parsed p = parse(opf(kMeta, kManifest, kSpine), true, 1);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("Main Title", p.title);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch1.xhtml", "OEBPS/text/ch2.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, EntityAndCharacterReferencesInTitleAreDecoded) {
  const Parsed p = parse(opf("<dc:title>Tom &amp; Jerry &#233;</dc:title>", kManifest, kSpine));
  EXPECT_EQ("Tom & Jerry \xC3\xA9", p.title);
}

TEST(ContentOpfParser, SpineFollowsItemrefOrderWithResolvedHrefs) {
  const Parsed p = parse(opf(kMeta, kManifest, "<itemref idref=\"ch2\"/><itemref idref=\"ch1\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/text/ch2.xhtml", "OEBPS/ch1.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, UnknownIdrefsAreSkipped) {
  const Parsed p =
      parse(opf(kMeta, kManifest, "<itemref idref=\"ch1\"/><itemref idref=\"ghost\"/><itemref idref=\"ch2\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch1.xhtml", "OEBPS/text/ch2.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, ItemrefWithoutIdrefIsSkipped) {
  const Parsed p = parse(opf(kMeta, kManifest, "<itemref linear=\"no\"/><itemref idref=\"ch1\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch1.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, NoCacheMeansNoSpineEntries) {
  const Parsed p = parse(opf(kMeta, kManifest, kSpine), /*withCache=*/false);
  EXPECT_TRUE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_EQ("Main Title", p.title);  // metadata still captured
}

TEST(ContentOpfParser, ManifestHrefsAreUriDecodedAndNormalised) {
  const std::string manifest =
      "<item id=\"a\" href=\"text/../ch%201.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"b\" href=\"./sub/./x.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c\" href=\"../../escape.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"a\"/><itemref idref=\"b\"/><itemref idref=\"c\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch 1.xhtml", "OEBPS/sub/x.xhtml", "escape.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, DuplicateManifestIdsResolveToExactlyOneEntry) {
  const std::string manifest =
      "<item id=\"dup\" href=\"first.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"dup\" href=\"second.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"dup\"/>"));
  ASSERT_EQ(1u, p.cache.spine.size());
  EXPECT_TRUE(p.cache.spine[0] == "OEBPS/first.xhtml" || p.cache.spine[0] == "OEBPS/second.xhtml");
}

TEST(ContentOpfParser, HashCollidingIdsAreDisambiguatedByFullCompare) {
  // "idypzdos" and "idaugiir" are a real FNV-1a/32 collision at equal length,
  // so both index entries share the whole (idHash, idLen) sort key. The scan
  // after lower_bound must walk past the wrong candidate and compare the
  // stored id before accepting a href.
  const std::string a = "idypzdos";
  const std::string b = "idaugiir";
  const std::string manifest = "<item id=\"" + a + "\" href=\"a.xhtml\" media-type=\"application/xhtml+xml\"/>" +
                               "<item id=\"" + b + "\" href=\"b.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"" + b + "\"/><itemref idref=\"" + a + "\"/>"));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/b.xhtml", "OEBPS/a.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, ManyManifestItemsResolveThroughTheSortedIndex) {
  // Reverse-ordered spine over a 64-item manifest: every idref must come back
  // through the sorted index, not through manifest order.
  std::string manifest;
  std::string spine;
  for (int i = 0; i < 64; ++i) {
    const std::string id = "id" + std::to_string(i);
    manifest += "<item id=\"" + id + "\" href=\"" + id + ".xhtml\" media-type=\"application/xhtml+xml\"/>";
  }
  for (int i = 63; i >= 0; --i) {
    spine += "<itemref idref=\"id" + std::to_string(i) + "\"/>";
  }
  const Parsed p = parse(opf(kMeta, manifest, spine));
  ASSERT_EQ(64u, p.cache.spine.size());
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ("OEBPS/id" + std::to_string(63 - i) + ".xhtml", p.cache.spine[i]);
  }
}

TEST(ContentOpfParser, NamespacePrefixedElementsAreRecognised) {
  const std::string doc =
      "<opf:package xmlns:opf=\"http://www.idpf.org/2007/opf\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<opf:metadata><dc:title>NS</dc:title></opf:metadata>"
      "<opf:manifest><opf:item id=\"c\" href=\"c.xhtml\" media-type=\"application/xhtml+xml\"/></opf:manifest>"
      "<opf:spine><opf:itemref idref=\"c\"/></opf:spine></opf:package>";
  const Parsed p = parse(doc);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("NS", p.title);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/c.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, MetaCoverWithImageMediaTypeIsUsed) {
  const std::string manifest = kManifest + "<item id=\"cov\" href=\"images/cover.jpg\" media-type=\"image/jpeg\"/>";
  const Parsed p = parse(opf(kMeta + "<meta name=\"cover\" content=\"cov\"/>", manifest, kSpine));
  EXPECT_EQ("OEBPS/images/cover.jpg", p.coverItemHref);
}

TEST(ContentOpfParser, MetaCoverMediaTypeMatchIsCaseInsensitive) {
  const std::string manifest = kManifest + "<item id=\"cov\" href=\"c.png\" media-type=\"IMAGE/PNG\"/>";
  const Parsed p = parse(opf(kMeta + "<meta name=\"cover\" content=\"cov\"/>", manifest, kSpine));
  EXPECT_EQ("OEBPS/c.png", p.coverItemHref);
}

TEST(ContentOpfParser, MetaCoverPointingAtXhtmlIsIgnored) {
  const std::string manifest =
      kManifest + "<item id=\"covpage\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta + "<meta name=\"cover\" content=\"covpage\"/>", manifest, kSpine));
  EXPECT_EQ("", p.coverItemHref);
}

TEST(ContentOpfParser, CoverImagePropertyIsTheFallback) {
  const std::string manifest =
      kManifest +
      "<item id=\"covpage\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"img\" href=\"images/front.png\" media-type=\"image/png\" properties=\"cover-image\"/>";
  const Parsed p = parse(opf(kMeta + "<meta name=\"cover\" content=\"covpage\"/>", manifest, kSpine));
  EXPECT_EQ("OEBPS/images/front.png", p.coverItemHref);
}

TEST(ContentOpfParser, MetaCoverBeatsCoverImagePropertyRegardlessOfManifestOrder) {
  const std::string propFirst =
      "<item id=\"img\" href=\"prop.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
      "<item id=\"cov\" href=\"meta.jpg\" media-type=\"image/jpeg\"/>";
  const std::string metaFirst =
      "<item id=\"cov\" href=\"meta.jpg\" media-type=\"image/jpeg\"/>"
      "<item id=\"img\" href=\"prop.png\" media-type=\"image/png\" properties=\"cover-image\"/>";
  const std::string meta = kMeta + "<meta name=\"cover\" content=\"cov\"/>";
  EXPECT_EQ("OEBPS/meta.jpg", parse(opf(meta, propFirst, "")).coverItemHref);
  EXPECT_EQ("OEBPS/meta.jpg", parse(opf(meta, metaFirst, "")).coverItemHref);
}

TEST(ContentOpfParser, FirstCoverImagePropertyWinsAmongSeveral) {
  const std::string manifest =
      "<item id=\"a\" href=\"a.png\" media-type=\"image/png\" properties=\"svg cover-image\"/>"
      "<item id=\"b\" href=\"b.png\" media-type=\"image/png\" properties=\"cover-image\"/>";
  EXPECT_EQ("OEBPS/a.png", parse(opf(kMeta, manifest, "")).coverItemHref);
}

TEST(ContentOpfParser, ItemWithoutIdBecomesTheCoverWhenNoMetaCoverIsDeclared) {
  // Pins a defect: coverItemId starts empty and an item with no id serialises
  // an empty id too, so the `itemId == coverItemId` test matches and any
  // id-less image item is adopted as the cover.
  const std::string manifest = "<item href=\"stray.png\" media-type=\"image/png\"/>";
  const Parsed p = parse(opf(kMeta, manifest, ""));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("OEBPS/stray.png", p.coverItemHref);
}

TEST(ContentOpfParser, PropertiesSubstringMatchMisdetectsNavAndCoverImage) {
  // Pins a defect: the word-list check falls back to a plain substring search
  // for " nav" / " cover-image", so a longer word that merely starts with the
  // token is accepted.
  const std::string manifest =
      "<item id=\"n\" href=\"pages.xhtml\" media-type=\"application/xhtml+xml\" properties=\"scripted navigation\"/>"
      "<item id=\"i\" href=\"plate.png\" media-type=\"image/png\" properties=\"svg cover-images\"/>";
  const Parsed p = parse(opf(kMeta, manifest, ""));
  EXPECT_EQ("OEBPS/pages.xhtml", p.tocNavPath);
  EXPECT_EQ("OEBPS/plate.png", p.coverItemHref);
}

TEST(ContentOpfParser, NavDocumentDetectedFromPropertiesWordList) {
  const std::string manifest =
      "<item id=\"n\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"scripted nav\"/>";
  EXPECT_EQ("OEBPS/nav.xhtml", parse(opf(kMeta, manifest, "")).tocNavPath);
  const std::string plain =
      "<item id=\"n\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
  EXPECT_EQ("OEBPS/nav.xhtml", parse(opf(kMeta, plain, "")).tocNavPath);
  const std::string none =
      "<item id=\"n\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"scripted\"/>";
  EXPECT_EQ("", parse(opf(kMeta, none, "")).tocNavPath);
}

TEST(ContentOpfParser, NcxDetectedByMediaTypeAndDuplicatesIgnored) {
  const std::string manifest =
      "<item id=\"n1\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "<item id=\"n2\" href=\"other.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  EXPECT_EQ("OEBPS/toc.ncx", parse(opf(kMeta, manifest, "")).tocNcxPath);
}

TEST(ContentOpfParser, CssFilesCollectedInManifestOrder) {
  const std::string manifest =
      "<item id=\"b\" href=\"css/b.css\" media-type=\"text/css\"/>"
      "<item id=\"a\" href=\"a.css\" media-type=\"text/css\"/>"
      "<item id=\"x\" href=\"x.xhtml\" media-type=\"application/xhtml+xml\"/>";
  EXPECT_EQ((std::vector<std::string>{"OEBPS/css/b.css", "OEBPS/a.css"}), parse(opf(kMeta, manifest, "")).cssFiles);
}

TEST(ContentOpfParser, GuideStartReferenceRecordedTextIgnored) {
  const std::string guide =
      "<reference type=\"text\" href=\"ch1.xhtml\"/>"
      "<reference type=\"start\" href=\"text/ch2.xhtml#top\"/>"
      "<reference type=\"start\" href=\"later.xhtml\"/>";
  const Parsed p = parse(opf(kMeta, kManifest, kSpine, guide));
  EXPECT_TRUE(p.ok);
  // Pins current behaviour: the fragment is kept on guide hrefs (the spine
  // lookup in Epub::getSpineIndexForTextReference compares whole strings).
  EXPECT_EQ("OEBPS/text/ch2.xhtml#top", p.textReferenceHref);
}

TEST(ContentOpfParser, GuideTextOnlyLeavesNoStartReference) {
  const Parsed p = parse(opf(kMeta, kManifest, kSpine, "<reference type=\"text\" href=\"ch1.xhtml\"/>"));
  EXPECT_EQ("", p.textReferenceHref);
}

TEST(ContentOpfParser, GuideCoverReferenceRecordedFirstWins) {
  const std::string guide =
      "<reference type=\"cover-page\" href=\"cover%20page.xhtml\"/>"
      "<reference type=\"cover\" href=\"other.xhtml\"/>";
  EXPECT_EQ("OEBPS/cover page.xhtml", parse(opf(kMeta, kManifest, kSpine, guide)).guideCoverPageHref);
  EXPECT_EQ("OEBPS/c.xhtml",
            parse(opf(kMeta, kManifest, kSpine, "<reference type=\"cover\" href=\"c.xhtml\"/>")).guideCoverPageHref);
}

TEST(ContentOpfParser, TempItemStoreIsRemovedAfterParse) {
  const Parsed p = parse(opf(kMeta, kManifest, kSpine));
  EXPECT_TRUE(p.ok);
  EXPECT_FALSE(p.itemsBinLeftBehind);
}

// --- malformed corpus ------------------------------------------------------

TEST(ContentOpfParser, TruncatedInsideManifestFailsAndCleansUp) {
  const std::string full = opf(kMeta, kManifest, kSpine);
  const std::string doc = full.substr(0, full.find("media-type=\"text/css\""));
  const Parsed p = parse(doc);
  EXPECT_FALSE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_FALSE(p.itemsBinLeftBehind);
  EXPECT_EQ("Main Title", p.title);  // metadata parsed before the cut survives
}

TEST(ContentOpfParser, TruncatedMidSpineKeepsEntriesSeenSoFar) {
  const std::string full = opf(kMeta, kManifest, kSpine);
  const std::string doc = full.substr(0, full.find("<itemref idref=\"ch2\""));
  const Parsed p = parse(doc);
  EXPECT_FALSE(p.ok);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch1.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, SpineBeforeManifestResolvesNothing) {
  const std::string doc =
      "<package><metadata><dc:title xmlns:dc=\"x\">T</dc:title></metadata>"
      "<spine><itemref idref=\"ch1\"/></spine>"
      "<manifest><item id=\"ch1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest></package>";
  const Parsed p = parse(doc);
  EXPECT_TRUE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
}

TEST(ContentOpfParser, ItemWithoutHrefProducesEmptySpineHref) {
  // Pins current behaviour: a manifest item lacking href is indexed with an
  // empty path, so the spine entry is created with href "".
  const std::string manifest = "<item id=\"nohref\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"nohref\"/>"));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ((std::vector<std::string>{""}), p.cache.spine);
}

TEST(ContentOpfParser, ZeroByteDocumentIsNeverFedAndLeavesNothingBehind) {
  // A zero-length content.opf means readItemContentsToStream never calls
  // write(): no metadata, no spine, and no .items.bin to clean up.
  TempDir dir;
  BookMetadataCache cache;
  {
    ContentOpfParser parser(dir.path(), kBase, 0, &cache);
    ASSERT_TRUE(parser.setup());
    const uint8_t none = 0;
    EXPECT_EQ(0u, parser.write(&none, 0));  // a zero-length write must not finalise anything
    EXPECT_EQ("", parser.title);
  }
  EXPECT_TRUE(cache.spine.empty());
  EXPECT_FALSE(Storage.exists((dir.path() + "/.items.bin").c_str()));
}

TEST(ContentOpfParser, WhitespaceOnlyDocumentIsRejected) {
  const Parsed p = parse("   \n\t ");
  EXPECT_FALSE(p.ok);
}

TEST(ContentOpfParser, NotXmlIsRejected) {
  // Explicit length: the fixture starts with a NUL byte.
  const Parsed p = parse(std::string("\x00\x01\x02 garbage", 11));
  EXPECT_FALSE(p.ok);
}

TEST(ContentOpfParser, InvalidUtf8InTitleIsRejected) {
  const Parsed p = parse(opf("<dc:title>bad \xFF\xFE title</dc:title>", kManifest, kSpine));
  EXPECT_FALSE(p.ok);
}

TEST(ContentOpfParser, UnknownEncodingDeclarationIsRejected) {
  std::string doc = opf(kMeta, kManifest, kSpine);
  doc.replace(doc.find("UTF-8"), 5, "KOI8-NOPE");
  const Parsed p = parse(doc);
  EXPECT_FALSE(p.ok);
  EXPECT_EQ("", p.title);
}

TEST(ContentOpfParser, DeclaredSizeSmallerThanDocumentFinalisesEarlyAndFails) {
  // Lying size (too small): the write that consumes the declared last byte is
  // handed to expat as the final chunk while <package> is still open, so it is
  // a parse error. Metadata seen before the cut survives on the parser.
  TempDir dir;
  BookMetadataCache cache;
  const std::string doc = opf(kMeta, kManifest, kSpine);
  const size_t head = doc.find("<manifest>");
  ContentOpfParser parser(dir.path(), kBase, head, &cache);
  ASSERT_TRUE(parser.setup());
  const auto* bytes = reinterpret_cast<const uint8_t*>(doc.data());
  EXPECT_EQ(0u, parser.write(bytes, head));
  EXPECT_EQ(0u, parser.write(bytes + head, doc.size() - head));
  EXPECT_TRUE(cache.spine.empty());
  EXPECT_EQ("Main Title", parser.title);
}

TEST(ContentOpfParser, WritesPastTheFinalChunkAreRejected) {
  TempDir dir;
  BookMetadataCache cache;
  const std::string doc = opf(kMeta, kManifest, kSpine);
  ContentOpfParser parser(dir.path(), kBase, doc.size(), &cache);
  ASSERT_TRUE(parser.setup());
  ASSERT_TRUE(feed(parser, doc));
  EXPECT_EQ(2u, cache.spine.size());
  const std::string trailing = "<package/>";
  EXPECT_EQ(0u, parser.write(reinterpret_cast<const uint8_t*>(trailing.data()), trailing.size()));
  EXPECT_EQ(2u, cache.spine.size());
}

TEST(ContentOpfParser, DeclaredSizeLargerThanDocumentStillYieldsSpine) {
  TempDir dir;
  BookMetadataCache cache;
  const std::string doc = opf(kMeta, kManifest, kSpine);
  ContentOpfParser parser(dir.path(), kBase, doc.size() * 3, &cache);
  ASSERT_TRUE(parser.setup());
  EXPECT_TRUE(feed(parser, doc));
  EXPECT_EQ(2u, cache.spine.size());
}

TEST(ContentOpfParser, HostileNestingInsideMetadataDoesNotBreakLaterSections) {
  const std::string meta = kMeta + nest("x", 3000, "<dc:title>ignored</dc:title>");
  const Parsed p = parse(opf(meta, kManifest, kSpine));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("Main Title", p.title);
  EXPECT_EQ(2u, p.cache.spine.size());
}

TEST(ContentOpfParser, LongIdWithinTheSerialisationLimitStillResolves) {
  const std::string longId(4000, 'q');
  const std::string manifest = "<item id=\"" + longId + "\" href=\"big.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"" + longId + "\"/>"), true, 1024);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/big.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, IdBeyondTheSerialisationLimitCannotBeResolved) {
  // serialization::MAX_STRING_LENGTH caps readString at 8KB, so a 70KB manifest
  // id is spilled but never read back. The spine entry is dropped instead of
  // the firmware attempting a 70KB resize; the XML itself still parses.
  const std::string longId(70000, 'q');
  const std::string manifest = "<item id=\"" + longId + "\" href=\"big.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"" + longId + "\"/>"), true, 1024);
  EXPECT_TRUE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_FALSE(p.itemsBinLeftBehind);
}

TEST(ContentOpfParser, HugeAttributeCountOnManifestItemIsHandled) {
  // The attribute walk is a NULL-terminated char* pair list; 4,000 junk
  // attributes must not stop it finding id/href/media-type.
  std::string atts;
  atts.reserve(4000 * 16);
  for (int i = 0; i < 4000; ++i) {
    atts += " a" + std::to_string(i) + "=\"v\"";
  }
  const std::string manifest =
      "<item" + atts + " id=\"many\" href=\"many.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"many\"/>"), true, 1024);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/many.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, ItemWithoutMediaTypeResolvesButClassifiesAsNothing) {
  const std::string manifest =
      "<item id=\"plain\" href=\"plain.xhtml\"/>"
      "<item id=\"ncxish\" href=\"toc.ncx\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"plain\"/>"));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/plain.xhtml"}), p.cache.spine);
  EXPECT_EQ("", p.tocNcxPath);
  EXPECT_TRUE(p.cssFiles.empty());
}

TEST(ContentOpfParser, MetaCoverPointingAtAnItemWithoutMediaTypeIsIgnored) {
  const std::string manifest = "<item id=\"cov\" href=\"cover.jpg\"/>";
  const Parsed p = parse(opf(kMeta + "<meta name=\"cover\" content=\"cov\"/>", manifest, ""));
  EXPECT_EQ("", p.coverItemHref);
}

TEST(ContentOpfParser, Utf8BomBeforeTheDeclarationIsAccepted) {
  const Parsed p = parse(std::string("\xEF\xBB\xBF") + opf(kMeta, kManifest, kSpine));
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("Main Title", p.title);
  EXPECT_EQ(2u, p.cache.spine.size());
}

TEST(ContentOpfParser, Utf8BomMidDocumentIsRejected) {
  std::string doc = opf(kMeta, kManifest, kSpine);
  doc.insert(doc.find("<package"), "\xEF\xBB\xBF");
  EXPECT_FALSE(parse(doc).ok);
}

TEST(ContentOpfParser, EntityReferencesAreNotExpanded) {
  // Billion-laughs shape. XML_GE=0 rewrites each declared entity to its own
  // literal reference, so nothing is amplified into the heap.
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE package [<!ENTITY a \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\">"
      "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"><!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
      "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">]>"
      "<package><metadata><dc:title xmlns:dc=\"d\">&d;</dc:title></metadata>"
      "<manifest><item id=\"x\" href=\"&d;.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
      "<spine><itemref idref=\"x\"/></spine></package>";
  const Parsed p = parse(doc);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ("&d;", p.title);
  EXPECT_EQ((std::vector<std::string>{"OEBPS/&d;.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, BackslashHrefNormalisesToEmpty) {
  // FsHelpers::normalisePath refuses backslashes outright rather than guessing
  // at separator semantics, so a "..\..\" traversal collapses to nothing.
  const std::string manifest = "<item id=\"esc\" href=\"..\\..\\etc\\passwd\" media-type=\"text/css\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"esc\"/>"));
  EXPECT_EQ((std::vector<std::string>{""}), p.cache.spine);
  EXPECT_EQ((std::vector<std::string>{""}), p.cssFiles);
}

TEST(ContentOpfParser, AbsoluteHrefLosesItsLeadingSlash) {
  const std::string manifest = "<item id=\"abs\" href=\"/root.xhtml\" media-type=\"application/xhtml+xml\"/>";
  const Parsed p = parse(opf(kMeta, manifest, "<itemref idref=\"abs\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/root.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, RepeatedIdrefEmitsOneSpineEntryEach) {
  const Parsed p = parse(opf(kMeta, kManifest, "<itemref idref=\"ch1\"/><itemref idref=\"ch1\"/>"));
  EXPECT_EQ((std::vector<std::string>{"OEBPS/ch1.xhtml", "OEBPS/ch1.xhtml"}), p.cache.spine);
}

TEST(ContentOpfParser, EmptyManifestTakesTheLinearFallbackAndResolvesNothing) {
  // With no manifest items the index stays empty, so useItemIndex is false and
  // the linear rescan runs against a zero-length .items.bin.
  const Parsed p = parse(opf(kMeta, "", "<itemref idref=\"ghost\"/>"));
  EXPECT_TRUE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_FALSE(p.itemsBinLeftBehind);
}

TEST(ContentOpfParser, MissingManifestSectionLeavesSpineEmpty) {
  const std::string doc =
      "<package><metadata><dc:title xmlns:dc=\"d\">T</dc:title></metadata>"
      "<spine><itemref idref=\"ch1\"/></spine></package>";
  const Parsed p = parse(doc);
  EXPECT_TRUE(p.ok);
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_EQ("T", p.title);
}

TEST(ContentOpfParser, StorageWriteFailureLeavesSpineEmptyWithoutCrashing) {
  FailWritesGuard guard;
  const Parsed p = parse(opf(kMeta, kManifest, kSpine));
  EXPECT_TRUE(p.ok);  // XML itself is fine; only the item spill failed
  EXPECT_TRUE(p.cache.spine.empty());
  EXPECT_EQ("Main Title", p.title);
}
