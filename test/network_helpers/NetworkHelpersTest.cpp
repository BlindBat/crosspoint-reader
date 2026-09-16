// Host tests for the pure network helpers: UrlUtils, WebPathUtils (web file
// browser paths, WebSocket START parser, WebDAV header/mime helpers) and
// WifiScanUtils (scan merge/sort, RSSI bars).
//
// These pin the behaviour the Wi-Fi-bound translation units delegate to; the
// path-validation hardening tracked separately (T145) is deliberately not
// asserted here.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "UrlUtils.h"
#include "WebPathUtils.h"
#include "WifiScanUtils.h"

using WebPathUtils::WsStartCommand;
using WebPathUtils::WsStartParseResult;

// ---------------------------------------------------------------------------
// UrlUtils
// ---------------------------------------------------------------------------

TEST(UrlUtils, EnsureProtocolPrependsHttpWhenMissing) {
  EXPECT_EQ(UrlUtils::ensureProtocol("example.com/opds"), "http://example.com/opds");
}

TEST(UrlUtils, EnsureProtocolKeepsAnyExistingScheme) {
  EXPECT_EQ(UrlUtils::ensureProtocol("https://example.com"), "https://example.com");
  EXPECT_EQ(UrlUtils::ensureProtocol("http://example.com"), "http://example.com");
  EXPECT_EQ(UrlUtils::ensureProtocol("ftp://example.com"), "ftp://example.com");
}

TEST(UrlUtils, ExtractHostStopsAtFirstPathSlashAfterScheme) {
  EXPECT_EQ(UrlUtils::extractHost("https://example.com:8080/opds/root"), "https://example.com:8080");
  EXPECT_EQ(UrlUtils::extractHost("http://example.com"), "http://example.com");
}

TEST(UrlUtils, ExtractHostWithoutSchemeStopsAtFirstSlash) {
  EXPECT_EQ(UrlUtils::extractHost("example.com/opds"), "example.com");
  EXPECT_EQ(UrlUtils::extractHost("example.com"), "example.com");
}

TEST(UrlUtils, EncodeUnsafeUrlCharsEncodesSpacesQuotesAndBrackets) {
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("http://h/a b\"c<d>e{f}g|h^i`j\\k"),
            "http://h/a%20b%22c%3Cd%3Ee%7Bf%7Dg%7Ch%5Ei%60j%5Ck");
}

TEST(UrlUtils, EncodeUnsafeUrlCharsLeavesReservedAndUnreservedAlone) {
  const std::string url = "https://h:80/p-a_t.h~/x?q=1&r=[2]#f!$'()*+,;=@";
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars(url), url);
}

TEST(UrlUtils, EncodeUnsafeUrlCharsKeepsValidPercentEscapesAndEncodesLonePercent) {
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("a%20b%2fc"), "a%20b%2fc");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("100%"), "100%25");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("a%G1b"), "a%25G1b");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("a%2"), "a%252");
}

TEST(UrlUtils, EncodeUnsafeUrlCharsEncodesControlAndNonAsciiBytes) {
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("a\tb\x7f"), "a%09b%7F");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("caf\xC3\xA9"), "caf%C3%A9");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars(""), "");
}

TEST(UrlUtils, BuildUrlUsesHostForAbsolutePaths) {
  EXPECT_EQ(UrlUtils::buildUrl("http://example.com/opds/root?x=1", "/opds/new"), "http://example.com/opds/new");
}

TEST(UrlUtils, BuildUrlAppendsRelativePathAndDropsBaseQuery) {
  EXPECT_EQ(UrlUtils::buildUrl("http://example.com/opds?page=2", "next"), "http://example.com/opds/next");
  EXPECT_EQ(UrlUtils::buildUrl("http://example.com/opds/", "next"), "http://example.com/opds/next");
  EXPECT_EQ(UrlUtils::buildUrl("http://example.com/opds", "next"), "http://example.com/opds/next");
}

TEST(UrlUtils, BuildUrlReturnsBaseForEmptyPathAndAddsSchemeToServer) {
  EXPECT_EQ(UrlUtils::buildUrl("example.com/opds", ""), "http://example.com/opds");
  EXPECT_EQ(UrlUtils::buildUrl("example.com", "/x"), "http://example.com/x");
}

TEST(UrlUtils, BuildUrlUsesAbsoluteUrlPathDirectlyAndEncodesIt) {
  EXPECT_EQ(UrlUtils::buildUrl("http://a", "https://b/c d"), "https://b/c%20d");
}

TEST(UrlUtils, EnsureProtocolTreatsAnyTripleAsAnExistingScheme) {
  EXPECT_EQ(UrlUtils::ensureProtocol(""), "http://");
  EXPECT_EQ(UrlUtils::ensureProtocol("://x"), "://x");
  EXPECT_EQ(UrlUtils::ensureProtocol("a://b://c"), "a://b://c");
}

TEST(UrlUtils, ExtractHostOnDegenerateInputs) {
  EXPECT_EQ(UrlUtils::extractHost(""), "");
  EXPECT_EQ(UrlUtils::extractHost("/path/only"), "");
  EXPECT_EQ(UrlUtils::extractHost("://x/y"), "://x");
  EXPECT_EQ(UrlUtils::extractHost("http://"), "http://");
}

TEST(UrlUtils, BuildUrlTreatsProtocolRelativePathsAsHostRelative) {
  EXPECT_EQ(UrlUtils::buildUrl("http://h/a?b", "//cdn/x"), "http://h//cdn/x");
}

TEST(UrlUtils, BuildUrlAppendsABareQueryAsANewSegment) {
  // Rough edge worth pinning: a query-only link is joined like a path component.
  EXPECT_EQ(UrlUtils::buildUrl("http://h", "?q=1"), "http://h/?q=1");
}

TEST(UrlUtils, EncodeUnsafeUrlCharsHandlesTruncatedAndDoubledEscapes) {
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("%"), "%25");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("%2g"), "%252g");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("%%20"), "%25%20");
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars("%%"), "%25%25");
}

TEST(UrlUtils, EncodeUnsafeUrlCharsEncodesEmbeddedNulBytes) {
  const std::string url("http://h/a\0b", 12);
  EXPECT_EQ(UrlUtils::encodeUnsafeUrlChars(url), "http://h/a%00b");
}

// ---------------------------------------------------------------------------
// WebPathUtils::normalizeWebPath / isProtectedItemName
// ---------------------------------------------------------------------------

TEST(NormalizeWebPath, EmptyAndRootMapToRoot) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath(""), "/");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/"), "/");
}

TEST(NormalizeWebPath, EnsuresLeadingSlashAndStripsTrailingSlash) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("books"), "/books");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/books/"), "/books");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("books/sub/"), "/books/sub");
}

TEST(NormalizeWebPath, CollapsesDotSegmentsAndRepeatedSlashes) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a/./b"), "/a/b");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a/../b"), "/b");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("//a//b//"), "/a/b");
}

TEST(NormalizeWebPath, ClampsTraversalAboveRootToRoot) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/../.."), "/");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("../../etc"), "/etc");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("."), "/");
}

TEST(NormalizeWebPath, BackslashPathsNormalizeToRoot) {
  // FsHelpers::normalisePath rejects backslashes with an empty result, which maps to "/".
  EXPECT_EQ(WebPathUtils::normalizeWebPath("a\\b"), "/");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a/b\\..\\c"), "/");
}

TEST(NormalizeWebPath, PreservesLongPathsWithManyComponents) {
  std::string input;
  std::string expected;
  for (int i = 0; i < 40; i++) {
    input += "/dir" + std::to_string(i);
    expected += "/dir" + std::to_string(i);
  }
  EXPECT_EQ(WebPathUtils::normalizeWebPath(input + "/"), expected);
}

TEST(IsProtectedItemName, DotPrefixedNamesAreProtected) {
  EXPECT_TRUE(WebPathUtils::isProtectedItemName("."));
  EXPECT_TRUE(WebPathUtils::isProtectedItemName(".."));
  EXPECT_TRUE(WebPathUtils::isProtectedItemName(".crosspoint"));
  EXPECT_TRUE(WebPathUtils::isProtectedItemName(".hidden.epub"));
}

TEST(IsProtectedItemName, HiddenItemsMatchExactlyAndCaseSensitively) {
  EXPECT_TRUE(WebPathUtils::isProtectedItemName("System Volume Information"));
  EXPECT_TRUE(WebPathUtils::isProtectedItemName("XTCache"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("xtcache"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("XTCache2"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("System Volume Informatio"));
}

TEST(IsProtectedItemName, OrdinaryNamesAreNotProtected) {
  EXPECT_FALSE(WebPathUtils::isProtectedItemName(""));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("book.epub"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("a.b"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("Books"));
}

TEST(NormalizeWebPath, AcceptsNonNullTerminatedViews) {
  const char buf[] = "/books/and-more";
  EXPECT_EQ(WebPathUtils::normalizeWebPath(std::string_view(buf, 6)), "/books");
}

TEST(NormalizeWebPath, KeepsEmbeddedNulBytesAsOrdinaryPathBytes) {
  // Pre-T145 contract: nothing here rejects a NUL. The helper is length-honest, unlike the
  // Arduino String the routes used before the extraction -- see the call-site test below.
  EXPECT_EQ(WebPathUtils::normalizeWebPath(std::string_view("/a\0b", 4)), std::string("/a\0b", 4));
}

TEST(NormalizeWebPath, CallSiteFormTruncatesAtTheFirstNulExactlyAsBefore) {
  // Both /rename and /move pass server->arg(...).c_str(), so the view is strlen-bounded and
  // the result matches the pre-extraction String-based helper byte for byte.
  const char raw[] = "/a\0b";
  EXPECT_EQ(WebPathUtils::normalizeWebPath(std::string_view(raw)), "/a");
}

TEST(NormalizeWebPath, DoesNotDecodePercentEscapes) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a%2e%2e/b"), "/a%2e%2e/b");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("%2e%2e/x"), "/%2e%2e/x");
}

TEST(NormalizeWebPath, ThreeDotSegmentsAreOrdinaryNames) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/..."), "/...");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a/.../b"), "/a/.../b");
}

TEST(NormalizeWebPath, CollapsesRunsOfSlashes) {
  EXPECT_EQ(WebPathUtils::normalizeWebPath("/a///"), "/a");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("//"), "/");
  EXPECT_EQ(WebPathUtils::normalizeWebPath("///a"), "/a");
}

TEST(IsProtectedItemName, HiddenItemsTableIsTheOneTheWebServerEnforces) {
  ASSERT_EQ(std::size(WebPathUtils::HIDDEN_ITEMS), 2u);
  EXPECT_STREQ(WebPathUtils::HIDDEN_ITEMS[0], "System Volume Information");
  EXPECT_STREQ(WebPathUtils::HIDDEN_ITEMS[1], "XTCache");
  // Same traversal the file listing, download and delete routes run over the table.
  int seen = 0;
  for (const auto* item : WebPathUtils::HIDDEN_ITEMS) {
    EXPECT_TRUE(WebPathUtils::isProtectedItemName(item));
    seen++;
  }
  EXPECT_EQ(seen, 2);
}

TEST(IsProtectedItemName, SurroundingWhitespaceDefeatsTheCheck) {
  // Pre-T145 gap: the comparison is exact, so padded variants slip through.
  EXPECT_FALSE(WebPathUtils::isProtectedItemName(" .hidden"));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("XTCache "));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName("System Volume Information "));
}

TEST(IsProtectedItemName, AcceptsNonNullTerminatedViews) {
  const char buf[] = "XTCache-backup";
  EXPECT_TRUE(WebPathUtils::isProtectedItemName(std::string_view(buf, 7)));
  EXPECT_FALSE(WebPathUtils::isProtectedItemName(std::string_view(buf, 8)));
}

// ---------------------------------------------------------------------------
// WebPathUtils::parseWsStart
// ---------------------------------------------------------------------------

TEST(ParseWsStart, ParsesWellFormedCommand) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:book.epub:12345:/Books", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "book.epub");
  EXPECT_EQ(cmd.size, 12345u);
  EXPECT_EQ(cmd.path, "/Books");
  EXPECT_EQ(cmd.filePath, "/Books/book.epub");
}

TEST(ParseWsStart, RootPathJoinsWithoutDoubleSlash) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a.txt:1:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/");
  EXPECT_EQ(cmd.filePath, "/a.txt");
}

TEST(ParseWsStart, EmptyPathMapsToRoot) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a.txt:1:", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/");
  EXPECT_EQ(cmd.filePath, "/a.txt");
}

TEST(ParseWsStart, PathGetsLeadingSlashAndLosesOneTrailingSlash) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a.txt:1:Books/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/Books");
  EXPECT_EQ(cmd.filePath, "/Books/a.txt");

  ASSERT_EQ(WebPathUtils::parseWsStart("START:a.txt:1:Books//", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/Books/");
  EXPECT_EQ(cmd.filePath, "/Books/a.txt");
}

TEST(ParseWsStart, PathMayContainFurtherColons) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a.txt:7:/x:y/z", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/x:y/z");
  EXPECT_EQ(cmd.filePath, "/x:y/z/a.txt");
}

TEST(ParseWsStart, FilenameSplitsAtFirstColonSoColonsInNamesBreakTheSize) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:b.txt:10:/", cmd), WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, SizeAcceptsLeadingPlusZeroAndLeadingZeros) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:+5:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 5u);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:0:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 0u);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:007:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 7u);
}

TEST(ParseWsStart, EmptyFilenameIsAcceptedByTheParser) {
  // The server refuses it later because "/" already exists; T145 will reject it up front.
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START::10:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "");
  EXPECT_EQ(cmd.filePath, "/");
}

TEST(ParseWsStart, RejectsMessagesWithoutTheStartPrefix) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("", cmd), WsStartParseResult::NOT_START);
  EXPECT_EQ(WebPathUtils::parseWsStart("start:a:1:/", cmd), WsStartParseResult::NOT_START);
  EXPECT_EQ(WebPathUtils::parseWsStart("STARTa:1:/", cmd), WsStartParseResult::NOT_START);
  EXPECT_EQ(WebPathUtils::parseWsStart("START", cmd), WsStartParseResult::NOT_START);
  EXPECT_EQ(WebPathUtils::parseWsStart(" START:a:1:/", cmd), WsStartParseResult::NOT_START);
}

TEST(ParseWsStart, RejectsMissingFields) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START:", cmd), WsStartParseResult::MISSING_FIELDS);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a.txt", cmd), WsStartParseResult::MISSING_FIELDS);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a.txt:10", cmd), WsStartParseResult::MISSING_FIELDS);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a.txt:", cmd), WsStartParseResult::MISSING_FIELDS);
}

TEST(ParseWsStart, RejectsNonNumericSizes) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a::/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:+:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:-1:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:10MB:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a: 10:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:1.5:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:0x10:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:\xE2\x82\xAC:/", cmd), WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, AbsurdSizesSaturateRatherThanFail) {
  // String::toInt() is atol() over the device's 32-bit long: it clamps at LONG_MAX and the
  // upload is still accepted. Pinned, not fixed -- nothing here rejects an impossible size.
  constexpr size_t LONG_MAX_32 = 2147483647u;
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:4294967295:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:18446744073709551616:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:1000000000000000000000000:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
  const std::string huge(500, '9');
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:" + huge + ":/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
}

TEST(ParseWsStart, SizeSaturationBoundaryMatchesAtol) {
  constexpr size_t LONG_MAX_32 = 2147483647u;
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:2147483646:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 2147483646u);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:2147483647:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:2147483648:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, LONG_MAX_32);
  // Leading zeros do not push a representable value over the edge.
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:0000000002147483646:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 2147483646u);
}

TEST(ParseWsStart, SaturatedTokenStillFailsOnALaterNonDigit) {
  // Saturation must not short-circuit validation of the rest of the token.
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:99999999999999999999x:/", cmd), WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, FailedParseLeavesTheCommandUntouched) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:keep.txt:3:/k", cmd), WsStartParseResult::OK);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:x:bad:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(cmd.fileName, "keep.txt");
  EXPECT_EQ(cmd.size, 3u);
  EXPECT_EQ(cmd.path, "/k");
  EXPECT_EQ(cmd.filePath, "/k/keep.txt");
}

TEST(ParseWsStart, EmbeddedNulTruncatesTheFrameAtTheTransportBoundary) {
  // The server builds an Arduino String from the NUL-terminated payload, so the
  // parser only ever sees the bytes before the first NUL.
  const char frame[] = "START:a\0b.txt:10:/Books";
  const std::string_view asServerSees(frame);  // strlen-bounded
  EXPECT_EQ(asServerSees, "START:a");
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart(asServerSees, cmd), WsStartParseResult::MISSING_FIELDS);
}

TEST(ParseWsStart, ParserItselfIsLengthHonestAboutNulBytes) {
  // A length-bounded view keeps the NUL as ordinary data; nothing is validated here (T145).
  const char frame[] = "START:a\0b.txt:10:/Books";
  const std::string_view full(frame, sizeof(frame) - 1);
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart(full, cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, std::string("a\0b.txt", 7));
  EXPECT_EQ(cmd.size, 10u);
  EXPECT_EQ(cmd.path, "/Books");
}

TEST(ParseWsStart, TraversalAndDotNamesPassThroughUnvalidated) {
  // Pins the pre-T145 contract: the parser only splits and joins.
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:../x.epub:1:/../..", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "../x.epub");
  EXPECT_EQ(cmd.path, "/../..");
  EXPECT_EQ(cmd.filePath, "/../../../x.epub");
}

TEST(ParseWsStart, HandlesVeryLongFilenamesAndPaths) {
  const std::string name(4096, 'n');
  const std::string dir(4096, 'd');
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:" + name + ":1:/" + dir, cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, name);
  EXPECT_EQ(cmd.filePath, "/" + dir + "/" + name);
}

TEST(ParseWsStart, AcceptsUtf8FilenamesAndPathsVerbatim) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:caf\xC3\xA9.epub:42:/B\xC3\xBC"
                                       "cher",
                                       cmd),
            WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "caf\xC3\xA9.epub");
  EXPECT_EQ(cmd.size, 42u);
  EXPECT_EQ(cmd.path,
            "/B\xC3\xBC"
            "cher");
  EXPECT_EQ(cmd.filePath,
            "/B\xC3\xBC"
            "cher/caf\xC3\xA9.epub");
}

TEST(ParseWsStart, AcceptsFourByteUtf8Filenames) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:\xF0\x9F\x93\x96"
                                       ".epub:1:/",
                                       cmd),
            WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName,
            "\xF0\x9F\x93\x96"
            ".epub");
  EXPECT_EQ(cmd.filePath,
            "/\xF0\x9F\x93\x96"
            ".epub");
}

TEST(ParseWsStart, DistinguishesAMissingThirdFieldFromAnEmptySize) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START::", cmd), WsStartParseResult::MISSING_FIELDS);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:::", cmd), WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, ExtraDelimitersAllLandInThePath) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:1:::", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/::");
  EXPECT_EQ(cmd.filePath, "/::/a");
}

TEST(ParseWsStart, EmptyFilenameWithARealDirectoryYieldsATrailingSlashTarget) {
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START::5:/Books", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "");
  EXPECT_EQ(cmd.filePath, "/Books/");
}

TEST(ParseWsStart, SizeTokenWithAnEmbeddedNulIsRejected) {
  const char frame[] = "START:a:1\0 0:/";
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart(std::string_view(frame, sizeof(frame) - 1), cmd),
            WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, NulInsideThePathSurvivesALengthBoundedFrame) {
  const char frame[] = "START:a.txt:1:/Bo\0oks";
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart(std::string_view(frame, sizeof(frame) - 1), cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, std::string("/Bo\0oks", 7));
  EXPECT_EQ(cmd.filePath, std::string("/Bo\0oks/a.txt", 13));
}

TEST(ParseWsStart, AllZeroSizeTokenOfAnyLengthParsesAsZero) {
  const std::string zeros(500, '0');
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:" + zeros + ":/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.size, 0u);
}

TEST(ParseWsStart, RejectsSignsAndStraySeparatorsInsideTheSize) {
  WsStartCommand cmd;
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:+a:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:++1:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:-0:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:1-:/", cmd), WsStartParseResult::INVALID_SIZE);
  EXPECT_EQ(WebPathUtils::parseWsStart("START:a:1 :/", cmd), WsStartParseResult::INVALID_SIZE);
}

TEST(ParseWsStart, ControlCharactersInTheFilenamePassThrough) {
  // Pre-T145 gap: nothing rejects CR/LF in the target name.
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a\r\nb.epub:1:/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.fileName, "a\r\nb.epub");
  EXPECT_EQ(cmd.filePath, "/a\r\nb.epub");
}

TEST(ParseWsStart, TwoSlashPathStripsDownToRoot) {
  // Shortest token the trailing-slash strip can act on: "//" must become "/", not stay "//".
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:1://", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/");
  EXPECT_EQ(cmd.filePath, "/a");

  // Same shape reached by the leading-slash insert instead of a literal leading '/'.
  ASSERT_EQ(WebPathUtils::parseWsStart("START:b:1:x/", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "/x");
  EXPECT_EQ(cmd.filePath, "/x/b");
}

TEST(ParseWsStart, PathOfOnlySlashesIsNotCollapsed) {
  // The parser strips exactly one trailing slash; it never normalises the rest (T145 gap).
  WsStartCommand cmd;
  ASSERT_EQ(WebPathUtils::parseWsStart("START:a:1:///", cmd), WsStartParseResult::OK);
  EXPECT_EQ(cmd.path, "//");
  EXPECT_EQ(cmd.filePath, "//a");
}

// ---------------------------------------------------------------------------
// WebPathUtils WebDAV helpers
// ---------------------------------------------------------------------------

TEST(DavHeaders, DepthZeroIsZeroEverythingElseIsOne) {
  EXPECT_EQ(WebPathUtils::davDepth("0"), 0);
  EXPECT_EQ(WebPathUtils::davDepth("1"), 1);
  EXPECT_EQ(WebPathUtils::davDepth("infinity"), 1);
  EXPECT_EQ(WebPathUtils::davDepth(""), 1);
  EXPECT_EQ(WebPathUtils::davDepth("00"), 1);
  EXPECT_EQ(WebPathUtils::davDepth(" 0"), 1);
  EXPECT_EQ(WebPathUtils::davDepth("2"), 1);
}

TEST(DavHeaders, OverwriteDefaultsToTrueAndOnlyFDisables) {
  EXPECT_FALSE(WebPathUtils::davOverwrite("F"));
  EXPECT_FALSE(WebPathUtils::davOverwrite("f"));
  EXPECT_TRUE(WebPathUtils::davOverwrite("T"));
  EXPECT_TRUE(WebPathUtils::davOverwrite(""));
  EXPECT_TRUE(WebPathUtils::davOverwrite("false"));
  EXPECT_TRUE(WebPathUtils::davOverwrite("F "));
}

TEST(DavMimeType, DocumentTypes) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/Books/a.epub"), "application/epub+zip");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/Books/A.EPUB"), "application/epub+zip");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.pdf"), "application/pdf");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.txt"), "text/plain");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.html"), "text/html");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.htm"), "text/html");
}

TEST(DavMimeType, WebAndDataTypes) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.css"), "text/css");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.js"), "application/javascript");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.json"), "application/json");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.xml"), "application/xml");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.zip"), "application/zip");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.gz"), "application/gzip");
}

TEST(DavMimeType, ImageTypes) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.jpg"), "image/jpeg");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.jpeg"), "image/jpeg");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.png"), "image/png");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.gif"), "image/gif");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.svg"), "image/svg+xml");
}

TEST(DavMimeType, UnknownOrMissingExtensionIsOctetStream) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.bin"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/README"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.epub.bak"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath(""), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("epub"), "application/octet-stream");
}

TEST(DavHeaders, DepthMatchesTheZeroTokenExactly) {
  EXPECT_EQ(WebPathUtils::davDepth("Infinity"), 1);
  EXPECT_EQ(WebPathUtils::davDepth("0,noroot"), 1);
  EXPECT_EQ(WebPathUtils::davDepth(std::string_view("0x", 1)), 0);
}

TEST(DavHeaders, OverwriteMatchesTheBareFlagOnly) {
  EXPECT_TRUE(WebPathUtils::davOverwrite("FF"));
  EXPECT_TRUE(WebPathUtils::davOverwrite("T"));
  EXPECT_FALSE(WebPathUtils::davOverwrite(std::string_view("False", 1)));
}

TEST(DavMimeType, ExtensionOnlyNamesStillMatch) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath(".epub"), "application/epub+zip");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath(".txt"), "text/plain");
}

TEST(DavMimeType, MatchingIsCaseInsensitiveAcrossTheTable) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.PnG"), "image/png");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.JPEG"), "image/jpeg");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.GZ"), "application/gzip");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.SvG"), "image/svg+xml");
}

TEST(DavMimeType, ReadableFormatsWithoutAWebDavMappingFallBack) {
  // .fb2/.xtc/.md/.bmp are formats the reader opens but the WebDAV table does not name.
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.fb2"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.xtc"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.md"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.bmp"), "application/octet-stream");
}

TEST(DavMimeType, OnlyTheTrailingExtensionCounts) {
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.epub/b"), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.epub."), "application/octet-stream");
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath("/a.epub "), "application/octet-stream");
}

TEST(DavMimeType, AcceptsNonNullTerminatedViews) {
  const char buf[] = "/a.epubXX";
  EXPECT_STREQ(WebPathUtils::mimeTypeForPath(std::string_view(buf, 7)), "application/epub+zip");
}

// ---------------------------------------------------------------------------
// WifiScanUtils::barsForRssi
// ---------------------------------------------------------------------------

TEST(BarsForRssi, RisingThresholdsFromZeroBars) {
  EXPECT_EQ(WifiScanUtils::barsForRssi(-100, 0), 0);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-86, 0), 0);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-85, 0), 1);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-76, 0), 1);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-75, 0), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-65, 0), 3);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-55, 0), 4);
  EXPECT_EQ(WifiScanUtils::barsForRssi(0, 0), 4);
}

TEST(BarsForRssi, FallingUsesThreeDbmHysteresis) {
  // At 2 bars the level only drops once the signal falls below -78 dBm.
  EXPECT_EQ(WifiScanUtils::barsForRssi(-76, 2), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-78, 2), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-79, 2), 1);
  // At 4 bars, -57 holds and -59 drops.
  EXPECT_EQ(WifiScanUtils::barsForRssi(-57, 4), 4);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-59, 4), 3);
}

TEST(BarsForRssi, JumpsSeveralLevelsInOneStep) {
  EXPECT_EQ(WifiScanUtils::barsForRssi(-50, 1), 4);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-95, 4), 0);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-70, 4), 2);
}

TEST(BarsForRssi, ClampsOutOfRangeCurrentBars) {
  EXPECT_EQ(WifiScanUtils::barsForRssi(-70, 10), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-70, -3), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-120, 99), 0);
}

TEST(BarsForRssi, HoldsInsideTheHysteresisBand) {
  EXPECT_EQ(WifiScanUtils::barsForRssi(-77, 1), 1);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-77, 2), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-67, 2), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-67, 3), 3);
}

TEST(BarsForRssi, EachFallThresholdIsExclusive) {
  EXPECT_EQ(WifiScanUtils::barsForRssi(-88, 1), 1);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-89, 1), 0);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-68, 3), 3);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-69, 3), 2);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-58, 4), 4);
  EXPECT_EQ(WifiScanUtils::barsForRssi(-59, 4), 3);
}

TEST(BarsForRssi, IsMonotonicInSignalForEveryStartingLevel) {
  for (int start = 0; start <= 4; start++) {
    int previous = -1;
    for (int rssi = -110; rssi <= 0; rssi++) {
      const int bars = WifiScanUtils::barsForRssi(rssi, start);
      ASSERT_GE(bars, previous) << "start=" << start << " rssi=" << rssi;
      ASSERT_GE(bars, 0);
      ASSERT_LE(bars, 4);
      previous = bars;
    }
  }
}

TEST(BarsForRssi, SettledLevelsAreStableUnderRepeatedPolling) {
  for (int start = 0; start <= 4; start++) {
    for (int rssi = -110; rssi <= 0; rssi++) {
      const int bars = WifiScanUtils::barsForRssi(rssi, start);
      ASSERT_EQ(WifiScanUtils::barsForRssi(rssi, bars), bars) << "start=" << start << " rssi=" << rssi;
    }
  }
}

// ---------------------------------------------------------------------------
// WifiScanUtils::mergeScanResult / sortScannedNetworks
// ---------------------------------------------------------------------------

TEST(MergeScanResult, AppendsNewNetworkWithSavedPasswordCleared) {
  std::vector<WifiNetworkInfo> networks;
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, "Home", -60, true));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].ssid, "Home");
  EXPECT_EQ(networks[0].rssi, -60);
  EXPECT_TRUE(networks[0].isEncrypted);
  EXPECT_FALSE(networks[0].hasSavedPassword);
  EXPECT_FALSE(networks[0].isHiddenPlaceholder);
}

TEST(MergeScanResult, SkipsHiddenNetworksWithEmptySsid) {
  std::vector<WifiNetworkInfo> networks;
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "", -30, true));
  EXPECT_TRUE(networks.empty());
}

TEST(MergeScanResult, StrongerDuplicateReplacesSignalAndEncryption) {
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, "Home", -70, false));
  networks[0].hasSavedPassword = true;
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "Home", -50, true));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].rssi, -50);
  EXPECT_TRUE(networks[0].isEncrypted);
  EXPECT_TRUE(networks[0].hasSavedPassword);
}

TEST(MergeScanResult, WeakerOrEqualDuplicateLeavesEntryUnchanged) {
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, "Home", -50, true));
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "Home", -70, false));
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "Home", -50, false));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].rssi, -50);
  EXPECT_TRUE(networks[0].isEncrypted);
}

TEST(MergeScanResult, SsidsAreCaseSensitiveAndDistinct) {
  std::vector<WifiNetworkInfo> networks;
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, "Home", -50, true));
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, "home", -50, true));
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, "Home ", -50, true));
  EXPECT_EQ(networks.size(), 3u);
}

TEST(MergeScanResult, TruncatesSsidToThirtyTwoBytesBeforeMerging) {
  const std::string base(32, 'x');
  std::vector<WifiNetworkInfo> networks;
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, base + "overflow", -70, true));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].ssid, base);
  // The 32-byte prefix and the overlong form are the same network.
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, base, -40, false));
  EXPECT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].rssi, -40);
}

TEST(MergeScanResult, ThirtyTwoByteSsidIsKeptWhole) {
  const std::string exact(32, 's');
  std::vector<WifiNetworkInfo> networks;
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, exact, -70, true));
  EXPECT_EQ(networks[0].ssid, exact);
}

TEST(MergeScanResult, TruncationIsByteWiseAndCanSplitAMultiByteCodepoint) {
  // strlcpy into char[33] on device truncates bytes, not codepoints; the helper matches.
  const std::string ssid = std::string(31, 'x') + "\xC3\xA9";
  ASSERT_EQ(ssid.size(), 33u);
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, ssid, -60, true));
  ASSERT_EQ(networks[0].ssid.size(), 32u);
  EXPECT_EQ(networks[0].ssid, std::string(31, 'x') + "\xC3");
}

TEST(MergeScanResult, ThirtyThreeByteSsidCollidesWithItsTruncatedForm) {
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, std::string(33, 'z'), -70, true));
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, std::string(64, 'z'), -70, true));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].ssid.size(), 32u);
}

TEST(MergeScanResult, LengthBoundedSsidsKeepEmbeddedNulBytes) {
  const char raw[] = "Ho\0me";
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, std::string_view(raw, 5), -50, true));
  EXPECT_EQ(networks[0].ssid, std::string("Ho\0me", 5));
  // The activity passes WiFi.SSID(i).c_str(), which stops at the NUL, so these stay distinct rows.
  EXPECT_TRUE(WifiScanUtils::mergeScanResult(networks, "Ho", -50, true));
  EXPECT_EQ(networks.size(), 2u);
}

TEST(MergeScanResult, AppendsPreserveDiscoveryOrderAndReScansAreNoOps) {
  std::vector<WifiNetworkInfo> networks;
  for (int i = 0; i < 20; i++) {
    ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, "net-" + std::to_string(i), -40 - i, true));
  }
  ASSERT_EQ(networks.size(), 20u);
  for (int i = 0; i < 20; i++) {
    EXPECT_EQ(networks[i].ssid, "net-" + std::to_string(i));
  }
  for (int i = 0; i < 20; i++) {
    EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "net-" + std::to_string(i), -40 - i, true));
  }
  EXPECT_EQ(networks.size(), 20u);
}

TEST(MergeScanResult, ToleratesExtremeRssiValues) {
  std::vector<WifiNetworkInfo> networks;
  ASSERT_TRUE(WifiScanUtils::mergeScanResult(networks, "Edge", INT32_MIN, false));
  EXPECT_FALSE(WifiScanUtils::mergeScanResult(networks, "Edge", INT32_MAX, true));
  ASSERT_EQ(networks.size(), 1u);
  EXPECT_EQ(networks[0].rssi, INT32_MAX);
  EXPECT_TRUE(networks[0].isEncrypted);
}

namespace {
WifiNetworkInfo net(const char* ssid, int32_t rssi, bool saved) {
  WifiNetworkInfo n;
  n.ssid = ssid;
  n.rssi = rssi;
  n.isEncrypted = true;
  n.hasSavedPassword = saved;
  return n;
}
}  // namespace

TEST(SortScannedNetworks, SavedNetworksComeFirstThenStrongestSignal) {
  std::vector<WifiNetworkInfo> networks = {net("weak-unsaved", -90, false), net("strong-unsaved", -40, false),
                                           net("weak-saved", -85, true), net("strong-saved", -50, true)};
  WifiScanUtils::sortScannedNetworks(networks);
  ASSERT_EQ(networks.size(), 4u);
  EXPECT_EQ(networks[0].ssid, "strong-saved");
  EXPECT_EQ(networks[1].ssid, "weak-saved");
  EXPECT_EQ(networks[2].ssid, "strong-unsaved");
  EXPECT_EQ(networks[3].ssid, "weak-unsaved");
}

TEST(SortScannedNetworks, OrdersBySignalWhenNoneAreSaved) {
  std::vector<WifiNetworkInfo> networks = {net("c", -80, false), net("a", -40, false), net("b", -60, false)};
  WifiScanUtils::sortScannedNetworks(networks);
  EXPECT_EQ(networks[0].ssid, "a");
  EXPECT_EQ(networks[1].ssid, "b");
  EXPECT_EQ(networks[2].ssid, "c");
}

TEST(SortScannedNetworks, HandlesEmptyAndSingleElementLists) {
  std::vector<WifiNetworkInfo> empty;
  WifiScanUtils::sortScannedNetworks(empty);
  EXPECT_TRUE(empty.empty());

  std::vector<WifiNetworkInfo> one = {net("only", -70, false)};
  WifiScanUtils::sortScannedNetworks(one);
  ASSERT_EQ(one.size(), 1u);
  EXPECT_EQ(one[0].ssid, "only");
}

TEST(SortScannedNetworks, MergeThenSortMatchesTheActivityPipeline) {
  // Two scan rows for "Cafe" (weaker first), one hidden row, one saved network.
  std::vector<WifiNetworkInfo> networks;
  WifiScanUtils::mergeScanResult(networks, "Cafe", -80, false);
  WifiScanUtils::mergeScanResult(networks, "", -20, false);
  if (WifiScanUtils::mergeScanResult(networks, "Home", -75, true)) networks.back().hasSavedPassword = true;
  WifiScanUtils::mergeScanResult(networks, "Cafe", -45, true);
  WifiScanUtils::mergeScanResult(networks, "Guest", -60, false);
  WifiScanUtils::sortScannedNetworks(networks);

  ASSERT_EQ(networks.size(), 3u);
  EXPECT_EQ(networks[0].ssid, "Home");
  EXPECT_EQ(networks[1].ssid, "Cafe");
  EXPECT_EQ(networks[1].rssi, -45);
  EXPECT_TRUE(networks[1].isEncrypted);
  EXPECT_EQ(networks[2].ssid, "Guest");
}

TEST(SortScannedNetworks, OrdersBySignalWhenAllAreSaved) {
  std::vector<WifiNetworkInfo> networks = {net("c", -80, true), net("a", -40, true), net("b", -60, true)};
  WifiScanUtils::sortScannedNetworks(networks);
  EXPECT_EQ(networks[0].ssid, "a");
  EXPECT_EQ(networks[1].ssid, "b");
  EXPECT_EQ(networks[2].ssid, "c");
}

TEST(SortScannedNetworks, KeepsEveryRowAndTheGroupInvariantOnLargeLists) {
  std::vector<WifiNetworkInfo> networks;
  networks.reserve(64);
  for (int i = 0; i < 64; i++) {
    networks.push_back(net("n", static_cast<int32_t>(-30 - ((i * 7) % 60)), (i % 3) == 0));
  }
  WifiScanUtils::sortScannedNetworks(networks);
  ASSERT_EQ(networks.size(), 64u);
  for (size_t i = 1; i < networks.size(); i++) {
    const WifiNetworkInfo& a = networks[i - 1];
    const WifiNetworkInfo& b = networks[i];
    ASSERT_FALSE(!a.hasSavedPassword && b.hasSavedPassword);
    if (a.hasSavedPassword == b.hasSavedPassword) ASSERT_GE(a.rssi, b.rssi);
  }
}

// ---------------------------------------------------------------------------
// Component-wide path protection and name validation (T145). Checking only the
// final component let /.crosspoint/settings.json through /download.
// ---------------------------------------------------------------------------

namespace {

using WebPathUtils::NameCheck;

TEST(PathHasProtectedComponent, HiddenParentProtectsEverythingBeneathIt) {
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/.crosspoint/settings.json"));
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/.crosspoint/wifi.json"));
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/.crosspoint/epub_123/progress.bin"));
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/books/.hidden/secret.epub"));
}

TEST(PathHasProtectedComponent, NamedHiddenItemsAreProtectedAtAnyDepth) {
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/System Volume Information"));
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/System Volume Information/IndexerVolumeGuid"));
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent("/books/XTCache/page0"));
}

TEST(PathHasProtectedComponent, OrdinaryPathsPass) {
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/books"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/books/dune.epub"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/books/a.b.c/x.epub"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent(""));
}

TEST(PathHasProtectedComponent, MatchIsExactAndCaseSensitive) {
  // A name that merely starts with a protected one is not protected.
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/XTCache2/x"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/xtcache/x"));
  EXPECT_FALSE(WebPathUtils::pathHasProtectedComponent("/books/file.hidden"));
}

TEST(PathHasProtectedComponent, NormalisedTraversalIsStillCaught) {
  // A client sending "/books/../.crosspoint/wifi.json" normalises to the hidden
  // path, which is what the endpoints check.
  const std::string normalised = WebPathUtils::normalizeWebPath("/books/../.crosspoint/wifi.json");
  EXPECT_EQ(normalised, "/.crosspoint/wifi.json");
  EXPECT_TRUE(WebPathUtils::pathHasProtectedComponent(normalised));
}

TEST(CheckItemName, AcceptsOrdinaryNames) {
  EXPECT_EQ(WebPathUtils::checkItemName("dune.epub"), NameCheck::Ok);
  EXPECT_EQ(WebPathUtils::checkItemName("My Books"), NameCheck::Ok);
  EXPECT_EQ(WebPathUtils::checkItemName("a"), NameCheck::Ok);
}

TEST(CheckItemName, RejectsEmptyAndBlank) {
  EXPECT_EQ(WebPathUtils::checkItemName(""), NameCheck::Empty);
  EXPECT_EQ(WebPathUtils::checkItemName("   "), NameCheck::Empty);
  EXPECT_EQ(WebPathUtils::checkItemName("\t"), NameCheck::Empty);
}

TEST(CheckItemName, RejectsSeparatorsThatWouldEscapeTheFolder) {
  EXPECT_EQ(WebPathUtils::checkItemName("../evil"), NameCheck::HasSeparator);
  EXPECT_EQ(WebPathUtils::checkItemName("sub/child"), NameCheck::HasSeparator);
  EXPECT_EQ(WebPathUtils::checkItemName("..\\evil"), NameCheck::HasSeparator);
  EXPECT_EQ(WebPathUtils::checkItemName("/absolute"), NameCheck::HasSeparator);
}

TEST(CheckItemName, RejectsProtectedNames) {
  EXPECT_EQ(WebPathUtils::checkItemName(".crosspoint"), NameCheck::Protected);
  EXPECT_EQ(WebPathUtils::checkItemName(".hidden"), NameCheck::Protected);
  EXPECT_EQ(WebPathUtils::checkItemName("XTCache"), NameCheck::Protected);
  EXPECT_EQ(WebPathUtils::checkItemName("System Volume Information"), NameCheck::Protected);
}

TEST(CheckItemName, EveryRejectionHasAMessageAndOkHasNone) {
  EXPECT_STREQ(WebPathUtils::nameCheckMessage(NameCheck::Ok), "");
  for (const auto check : {NameCheck::Empty, NameCheck::HasSeparator, NameCheck::Protected}) {
    EXPECT_STRNE(WebPathUtils::nameCheckMessage(check), "");
  }
}

}  // namespace
