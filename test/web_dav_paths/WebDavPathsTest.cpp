// User-supplied path handling for the web stack.
//
// Part 1 pins the REAL FsHelpers primitives (normalisePath, decodeUriEscapes)
// every user path funnels through -- including the behaviors upstream
// security fix #3353 ("normalize every user-supplied path and escape file
// names in the files page") builds on.
//
// Part 2 drives the REAL WebDAVHandler through its public RequestHandler
// interface against a POSIX-sandboxed storage stub that records every path
// the handler passes to the filesystem boundary, pinning that traversal
// (plain and percent-encoded) can never escape the storage root.

#include <dirent.h>
#include <gtest/gtest.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "FsHelpers.h"
#include "network/WebDAVHandler.h"

namespace {

// ---------------------------------------------------------------------------
// Part 1: FsHelpers path primitives
// ---------------------------------------------------------------------------

TEST(NormalisePath, CollapsesParentTraversalSegments) {
  EXPECT_EQ(FsHelpers::normalisePath("/Books/../secret"), "secret");
  EXPECT_EQ(FsHelpers::normalisePath("a/b/../../c"), "c");
  EXPECT_EQ(FsHelpers::normalisePath("/Books/sub/../story.epub"), "Books/story.epub");
}

TEST(NormalisePath, ClampsLeadingParentReferencesAtRoot) {
  // ".." at (or above) the root must not climb out of it.
  EXPECT_EQ(FsHelpers::normalisePath("/../../etc/passwd"), "etc/passwd");
  EXPECT_EQ(FsHelpers::normalisePath("../x"), "x");
  EXPECT_EQ(FsHelpers::normalisePath("/.."), "");
}

TEST(NormalisePath, ResultIsRelativeWithCollapsedSlashes) {
  // The helper returns a relative, slash-collapsed path; callers
  // (normalizeWebPath / getRequestPath) re-add the leading '/'.
  EXPECT_EQ(FsHelpers::normalisePath("//a//b//"), "a/b");
  EXPECT_EQ(FsHelpers::normalisePath("/a/b/"), "a/b");
  EXPECT_EQ(FsHelpers::normalisePath("/"), "");
  EXPECT_EQ(FsHelpers::normalisePath(""), "");
}

TEST(NormalisePath, ResolvesSingleDotSegments) {
  // '.' segments resolve to nothing: they never reach the filesystem layer and
  // never absorb a following "..".
  EXPECT_EQ(FsHelpers::normalisePath("/a/./b"), "a/b");
  EXPECT_EQ(FsHelpers::normalisePath("."), "");
  EXPECT_EQ(FsHelpers::normalisePath("./x"), "x");
  EXPECT_EQ(FsHelpers::normalisePath("/a/b/./.."), "a");
  // ".." after "." pops the REAL directory. (Before '.' was resolved, it
  // popped the literal "." component and the traversal resolved deeper than
  // true path semantics.)
  EXPECT_EQ(FsHelpers::normalisePath("/Books/./../secret"), "secret");
  // Dot-PREFIXED names are ordinary components, not "." segments.
  EXPECT_EQ(FsHelpers::normalisePath("/.crosspoint/./cfg"), ".crosspoint/cfg");
}

TEST(NormalisePath, RejectsPathsContainingBackslashes) {
  // normalisePath splits on '/' only; rather than treating '\\' as a second
  // separator it rejects the whole path, so Windows-style traversal cannot
  // survive as literal component text. Rejection (over normalisation) mirrors
  // upstream #3353, whose isSafePathComponent also rejects '\\'. '\\' is not a
  // valid FAT32 filename character, so no legitimate path is refused; the
  // empty result resolves to the storage root, where callers' protections
  // apply.
  EXPECT_EQ(FsHelpers::normalisePath("..\\..\\x"), "");
  EXPECT_EQ(FsHelpers::normalisePath("/a\\..\\b/c"), "");
  EXPECT_EQ(FsHelpers::normalisePath("a\\b"), "");
}

TEST(NormalisePath, HandlesOverlongPathsWithManyComponents) {
  // 200 components then 200 "..": everything collapses back to the root.
  std::string deep;
  for (int i = 0; i < 200; ++i) deep += "/d" + std::to_string(i);
  std::string climb = deep;
  for (int i = 0; i < 200; ++i) climb += "/..";
  EXPECT_EQ(FsHelpers::normalisePath(climb), "");
  EXPECT_EQ(FsHelpers::normalisePath(climb + "/leaf"), "leaf");
}

TEST(DecodeUriEscapes, DecodesPercentSequencesIncludingTraversal) {
  EXPECT_EQ(FsHelpers::decodeUriEscapes("%2e%2e%2fsecret"), "../secret");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("%2E%2E"), "..");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("my%20book.epub"), "my book.epub");
  // '+' is NOT a space in this helper (unlike WebServer::urlDecode).
  EXPECT_EQ(FsHelpers::decodeUriEscapes("a+b"), "a+b");
}

TEST(DecodeUriEscapes, PassesMalformedEscapesThroughUnchanged) {
  EXPECT_EQ(FsHelpers::decodeUriEscapes("abc%"), "abc%");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("abc%2"), "abc%2");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("%zz"), "%zz");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("100%25"), "100%");
}

TEST(DecodeUriEscapes, DecodesNulByteIntoTheString) {
  // %00 becomes a real NUL inside the std::string. Anything later handed to
  // a c_str()-consuming API is silently truncated at this byte -- the WebDAV
  // test below pins that consequence end to end.
  const std::string decoded = FsHelpers::decodeUriEscapes("a%00b");
  ASSERT_EQ(decoded.size(), 3u);
  EXPECT_EQ(decoded[0], 'a');
  EXPECT_EQ(decoded[1], '\0');
  EXPECT_EQ(decoded[2], 'b');
}

// ---------------------------------------------------------------------------
// Part 2: WebDAVHandler end-to-end path handling
// ---------------------------------------------------------------------------

void rmTree(const std::string& path) {
  if (DIR* dir = opendir(path.c_str())) {
    while (dirent* entry = readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      rmTree(path + "/" + name);
    }
    closedir(dir);
    ::rmdir(path.c_str());
  } else {
    ::unlink(path.c_str());
  }
}

void writeFileAt(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
  ASSERT_TRUE(out.good()) << "failed to write fixture " << path;
}

bool existsAt(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

std::string readFileAt(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

class WebDavPathsTest : public ::testing::Test {
 protected:
  std::string root;
  WebDAVHandler handler;

  void SetUp() override {
    // ctest runs each test as its own process, in parallel: the sandbox must
    // be unique per process or concurrent tests clobber each other's trees.
    std::string rootTemplate = ::testing::TempDir() + "webdav_sandbox_XXXXXX";
    std::vector<char> buf(rootTemplate.begin(), rootTemplate.end());
    buf.push_back('\0');
    ASSERT_NE(mkdtemp(buf.data()), nullptr);
    root = buf.data();

    // Virtual tree:
    //   /notes.txt                          "hello"
    //   /my book.epub                       "book"
    //   /Books/story.epub                   "EPUB"
    //   /.crosspoint/settings.bin           "cfg"    (protected: dot-prefixed)
    //   /System Volume Information/IndexerVolumeGuid (protected: hidden item)
    ::mkdir((root + "/Books").c_str(), 0755);
    ::mkdir((root + "/.crosspoint").c_str(), 0755);
    ::mkdir((root + "/System Volume Information").c_str(), 0755);
    writeFileAt(root + "/notes.txt", "hello");
    writeFileAt(root + "/my book.epub", "book");
    writeFileAt(root + "/Books/story.epub", "EPUB");
    writeFileAt(root + "/.crosspoint/settings.bin", "cfg");
    writeFileAt(root + "/System Volume Information/IndexerVolumeGuid", "guid");

    HalStorage::getInstance().resetForTest(root);
  }

  void TearDown() override { rmTree(root); }

  WebServer makeRequest(HTTPMethod method, const char* uri) {
    WebServer server;
    server.requestMethod = method;
    server.requestUri = uri;
    return server;
  }

  int run(WebServer& server) {
    EXPECT_TRUE(handler.canHandle(server, server.requestMethod, server.uri()));
    EXPECT_TRUE(handler.handle(server, server.requestMethod, server.uri()));
    return server.statusCode;
  }

  // Every virtual path the handler hands to the storage layer must already be
  // normalized: absolute, free of ".." segments, free of embedded NULs.
  void expectStorageSawOnlyNormalizedPaths() {
    for (const auto& p : HalStorage::getInstance().receivedPaths) {
      EXPECT_FALSE(p.empty());
      EXPECT_EQ(p[0], '/') << "non-absolute path reached storage: " << p;
      EXPECT_EQ(p.find('\0'), std::string::npos) << "NUL reached storage";
      // Reject a ".." *segment* (backslash tricks are a separate, pinned case).
      const std::string slashed = p + "/";
      EXPECT_EQ(slashed.find("/../"), std::string::npos) << "'..' segment reached storage: " << p;
    }
  }
};

TEST_F(WebDavPathsTest, GetWithPlainTraversalIsConfinedToTheRoot) {
  auto server = makeRequest(HTTP_GET, "/../../notes.txt");
  EXPECT_EQ(run(server), 200);
  EXPECT_EQ(server.bodySent, "hello");
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, GetWithPercentEncodedTraversalIsConfinedToTheRoot) {
  auto server = makeRequest(HTTP_GET, "/%2e%2e/%2e%2e/notes.txt");
  EXPECT_EQ(run(server), 200);
  EXPECT_EQ(server.bodySent, "hello");
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, GetServesFileWithItsMimeType) {
  auto server = makeRequest(HTTP_GET, "/Books/story.epub");
  EXPECT_EQ(run(server), 200);
  EXPECT_EQ(server.contentTypeSent, "application/epub+zip");
  EXPECT_EQ(server.bodySent, "EPUB");
  EXPECT_EQ(server.contentLengthSet, 4u);
}

TEST_F(WebDavPathsTest, DotPrefixedPathsAreForbiddenEvenAfterTraversal) {
  auto direct = makeRequest(HTTP_GET, "/.crosspoint/settings.bin");
  EXPECT_EQ(run(direct), 403);

  // Protection is applied to the NORMALIZED path, so hiding behind a
  // traversal prefix does not help.
  auto sneaky = makeRequest(HTTP_GET, "/Books/../.crosspoint/settings.bin");
  EXPECT_EQ(run(sneaky), 403);
  EXPECT_EQ(sneaky.bodySent, "Forbidden");
}

TEST_F(WebDavPathsTest, HiddenSystemFolderIsForbiddenInAnySegment) {
  auto server = makeRequest(HTTP_GET, "/System%20Volume%20Information/IndexerVolumeGuid");
  EXPECT_EQ(run(server), 403);
}

TEST_F(WebDavPathsTest, MkcolCreatesTheDirectoryAtTheNormalizedPath) {
  auto server = makeRequest(HTTP_MKCOL, "/Books/../library2");
  EXPECT_EQ(run(server), 201);
  EXPECT_TRUE(existsAt(root + "/library2"));
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, EncodedBackslashPathsNeverReachTheFilesystemAsComponents) {
  // "%5C" decodes to '\\' and normalisePath rejects the whole backslash path
  // (see NormalisePath.RejectsPathsContainingBackslashes), collapsing it to
  // the root. MKCOL on the already-existing root answers 405, and no
  // backslash-named entry is ever created or handed to the storage layer.
  auto dotLeading = makeRequest(HTTP_MKCOL, "/..%5Cup");
  EXPECT_EQ(run(dotLeading), 405);
  EXPECT_FALSE(existsAt(root + "/..\\up"));

  auto nonDot = makeRequest(HTTP_MKCOL, "/up%5C..%5Cx");
  EXPECT_EQ(run(nonDot), 405);
  EXPECT_FALSE(existsAt(root + "/up\\..\\x"));

  for (const auto& p : HalStorage::getInstance().receivedPaths) {
    EXPECT_EQ(p.find('\\'), std::string::npos) << "backslash reached storage: " << p;
  }
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, EncodedNulByteTruncatesTheRestOfThePath) {
  // Documents current limitation: getRequestPath re-reads the decoded String
  // through c_str(), so everything after an encoded %00 is silently dropped
  // and the request acts on the truncated path.
  auto server = makeRequest(HTTP_MKCOL, "/evil%00/../ignored");
  EXPECT_EQ(run(server), 201);
  EXPECT_TRUE(existsAt(root + "/evil"));
  EXPECT_FALSE(existsAt(root + "/ignored"));
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, DeleteRootIsForbiddenEvenViaTraversal) {
  auto plain = makeRequest(HTTP_DELETE, "/");
  EXPECT_EQ(run(plain), 403);

  // "/%2e%2e/" normalizes to "/", which must hit the same guard.
  auto sneaky = makeRequest(HTTP_DELETE, "/%2e%2e/");
  EXPECT_EQ(run(sneaky), 403);
}

TEST_F(WebDavPathsTest, DeleteProtectedItemIsForbidden) {
  auto server = makeRequest(HTTP_DELETE, "/.crosspoint");
  EXPECT_EQ(run(server), 403);
  EXPECT_TRUE(existsAt(root + "/.crosspoint/settings.bin"));
}

TEST_F(WebDavPathsTest, DeleteNonEmptyDirectoryConflicts) {
  auto server = makeRequest(HTTP_DELETE, "/Books");
  EXPECT_EQ(run(server), 409);
  EXPECT_TRUE(existsAt(root + "/Books/story.epub"));
}

TEST_F(WebDavPathsTest, DeleteRemovesAFile) {
  auto server = makeRequest(HTTP_DELETE, "/notes.txt");
  EXPECT_EQ(run(server), 204);
  EXPECT_FALSE(existsAt(root + "/notes.txt"));
}

TEST_F(WebDavPathsTest, PutStoresTheBodyAtTheNormalizedPath) {
  auto server = makeRequest(HTTP_PUT, "/Books/../upload.txt");

  HTTPRaw raw;
  raw.status = RAW_START;
  handler.raw(server, server.uri(), raw);

  const char payload[] = "data123";
  raw.status = RAW_WRITE;
  raw.currentSize = sizeof(payload) - 1;
  std::memcpy(raw.buf, payload, raw.currentSize);
  handler.raw(server, server.uri(), raw);

  raw.status = RAW_END;
  raw.totalSize = sizeof(payload) - 1;
  handler.raw(server, server.uri(), raw);

  EXPECT_TRUE(handler.handle(server, HTTP_PUT, server.uri()));
  EXPECT_EQ(server.statusCode, 201);
  EXPECT_EQ(readFileAt(root + "/upload.txt"), "data123");
  EXPECT_FALSE(existsAt(root + "/upload.txt.davtmp"));  // temp cleaned up
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, PutToADotPrefixedNameIsRefused) {
  auto server = makeRequest(HTTP_PUT, "/.sneaky.txt");

  HTTPRaw raw;
  raw.status = RAW_START;
  handler.raw(server, server.uri(), raw);
  raw.status = RAW_END;
  handler.raw(server, server.uri(), raw);

  EXPECT_TRUE(handler.handle(server, HTTP_PUT, server.uri()));
  EXPECT_EQ(server.statusCode, 403);
  EXPECT_FALSE(existsAt(root + "/.sneaky.txt"));
}

TEST_F(WebDavPathsTest, MoveNormalizesThePercentEncodedDestinationHeader) {
  auto server = makeRequest(HTTP_MOVE, "/notes.txt");
  server.requestHeaders["Destination"] = String("http://192.168.4.1/%2e%2e/renamed.txt");

  EXPECT_EQ(run(server), 201);
  EXPECT_FALSE(existsAt(root + "/notes.txt"));
  EXPECT_EQ(readFileAt(root + "/renamed.txt"), "hello");
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, MoveToAProtectedDestinationIsForbidden) {
  auto server = makeRequest(HTTP_MOVE, "/notes.txt");
  server.requestHeaders["Destination"] = String("http://192.168.4.1/.crosspoint/notes.txt");

  EXPECT_EQ(run(server), 403);
  EXPECT_TRUE(existsAt(root + "/notes.txt"));
}

TEST_F(WebDavPathsTest, CopyDuplicatesTheFileAtTheNormalizedDestination) {
  auto server = makeRequest(HTTP_COPY, "/notes.txt");
  server.requestHeaders["Destination"] = String("/Books/../copy.txt");

  EXPECT_EQ(run(server), 201);
  EXPECT_EQ(readFileAt(root + "/notes.txt"), "hello");
  EXPECT_EQ(readFileAt(root + "/copy.txt"), "hello");
  expectStorageSawOnlyNormalizedPaths();
}

TEST_F(WebDavPathsTest, PropfindListsChildrenButSkipsHiddenAndProtectedItems) {
  auto server = makeRequest(HTTP_PROPFIND, "/");
  server.requestHeaders["Depth"] = String("1");

  EXPECT_EQ(run(server), 207);
  EXPECT_NE(server.bodySent.find("<D:href>/Books/</D:href>"), std::string::npos);
  EXPECT_NE(server.bodySent.find("<D:href>/notes.txt</D:href>"), std::string::npos);
  EXPECT_EQ(server.bodySent.find(".crosspoint"), std::string::npos);
  EXPECT_EQ(server.bodySent.find("System Volume Information"), std::string::npos);
  EXPECT_EQ(server.bodySent.find("Volume%20Information"), std::string::npos);
}

TEST_F(WebDavPathsTest, PropfindPercentEncodesSpecialFilenameBytesInHrefs) {
  writeFileAt(root + "/50%.epub", "pct");
  writeFileAt(root + "/caf\xC3\xA9.epub", "acc");

  auto server = makeRequest(HTTP_PROPFIND, "/");
  server.requestHeaders["Depth"] = String("1");

  EXPECT_EQ(run(server), 207);
  EXPECT_NE(server.bodySent.find("<D:href>/my%20book.epub</D:href>"), std::string::npos);
  EXPECT_NE(server.bodySent.find("<D:href>/50%25.epub</D:href>"), std::string::npos);
  EXPECT_NE(server.bodySent.find("<D:href>/caf%C3%A9.epub</D:href>"), std::string::npos);
  // The raw byte forms must not leak into the XML.
  EXPECT_EQ(server.bodySent.find("my book.epub"), std::string::npos);
  EXPECT_EQ(server.bodySent.find("caf\xC3\xA9"), std::string::npos);
}

TEST_F(WebDavPathsTest, OverlongUriIsHandledWithoutTruncationSideEffects) {
  std::string longUri = "/missing";
  for (int i = 0; i < 250; ++i) longUri += "/component" + std::to_string(i);
  auto server = makeRequest(HTTP_GET, longUri.c_str());
  EXPECT_EQ(run(server), 404);
  expectStorageSawOnlyNormalizedPaths();
}

}  // namespace
