// KOSync client tests (FR-169..FR-176, contracts/kosync-protocol.md).
//
// Covers KOReaderDocumentId (basename hash, partial-MD5 offset schedule and
// EOF skipping), KOReaderCredentialStore::getBaseUrl/getMd5Password,
// KOReaderSyncClient's request shape, status -> Error mapping and JSON
// parsing/serialisation behind the canned SecureHttpClient stub, the
// SmartSync decision table and rich-position builder extracted from
// KOReaderSyncActivity, the settings URL helpers, ProgressMapper's
// generateXPath byte fallback and its <a id> attribute scanner.
//
// Malformed bodies (non-JSON, truncated, hostile nesting, embedded NUL,
// oversized fields, wrong types, unexpected status codes) are covered against
// the client's parser; the missing field/body bounds are task T144's.
//
// Already pinned elsewhere and not repeated here: koreader.json persistence
// and config migration (test/persistable_stores), the double-quoted anchor-id
// happy path, xpointer resolution and one malformed-chapter generateXPath case
// (test/progress_mapper), and ChapterXPathResolver (test/chapter_xpath_resolver).

#include <ArduinoJson.h>
#include <Epub/Section.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <KOReaderServerUrl.h>
#include <KOReaderSyncClient.h>
#include <MD5Builder.h>
#include <PlatformHost.h>
#include <ProgressMapper.h>
#include <SecureHttpClient.h>
#include <SmartSyncDecision.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using Error = KOReaderSyncClient::Error;
using freeink::http_stub::enqueue;
using freeink::http_stub::exchanges;

constexpr char DEFAULT_BASE[] = "https://sync.crosspointreader.com";
constexpr char THIRD_PARTY_BASE[] = "https://sync.koreader.rocks:443";
constexpr char DOC_HASH[] = "0123456789abcdef0123456789abcdef";

std::string md5Hex(const std::string& data) {
  MD5Builder md5;
  md5.begin();
  md5.add(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  md5.calculate();
  return md5.toString().c_str();
}

// Deterministic non-repeating byte pattern for generated files.
uint8_t patternByte(const size_t i) { return static_cast<uint8_t>((i * 31u + (i >> 7)) & 0xFF); }

std::string patternBytes(const size_t from, const size_t count) {
  std::string out;
  out.reserve(count);
  for (size_t i = 0; i < count; i++) out += static_cast<char>(patternByte(from + i));
  return out;
}

// KOReader's schedule: 0, then 1024 << (2*i) for i = 0..10.
std::vector<size_t> koreaderOffsets() {
  std::vector<size_t> offsets{0};
  for (int i = 0; i <= 10; i++) offsets.push_back(static_cast<size_t>(1024) << (2 * i));
  return offsets;
}

std::string expectedPartialMd5(const size_t fileSize) {
  std::string sampled;
  for (const size_t off : koreaderOffsets()) {
    if (off >= fileSize) continue;
    sampled += patternBytes(off, std::min<size_t>(1024, fileSize - off));
  }
  return md5Hex(sampled);
}

void resetStore() {
  KOREADER_STORE.setCredentials("", "");
  KOREADER_STORE.setServerUrl("");
  KOREADER_STORE.setMatchMethod(DocumentMatchMethod::FILENAME);
  KOREADER_STORE.setSendMetadata(false);
  KOREADER_STORE.setSyncBehavior(KOReaderSyncBehavior::SMART);
}

class KosyncClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    freeink::http_stub::reset();
    platform_host::setHeap(0, 0);
    SectionStubRegistry::reset();
    resetStore();
    KOReaderSyncClient::lastHttpCode = 0;
  }

  static void withCredentials() { KOREADER_STORE.setCredentials("reader", "secret"); }

  static const freeink::HttpStubExchange& lastExchange() {
    EXPECT_FALSE(exchanges().empty());
    return exchanges().back();
  }

  static JsonDocument parseBody(const std::string& body) {
    JsonDocument doc;
    EXPECT_EQ(deserializeJson(doc, body), DeserializationError::Ok) << body;
    return doc;
  }

  static KOReaderProgress sampleProgress() {
    KOReaderProgress p;
    p.document = DOC_HASH;
    p.progress = "/body/DocFragment[3]/body/p[7]/text().12";
    p.percentage = 0.375f;
    return p;
  }

  static KOReaderRichPosition samplePosition() {
    KOReaderRichPosition pos;
    pos.pctQ = 375000;
    pos.spineIndex = 2;
    pos.pageNumber = 4;
    pos.totalPages = 9;
    pos.paragraphIndex = 7;
    pos.xpath = "/body/DocFragment[3]/body/p[7]";
    return pos;
  }

  GfxRenderer renderer;
};

}  // namespace

// --- MD5 stub self-check ----------------------------------------------------

TEST_F(KosyncClientTest, Md5StubMatchesRfcVectors) {
  EXPECT_EQ(md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(md5Hex(std::string(1000, 'x')), md5Hex(std::string(500, 'x') + std::string(500, 'x')));
  EXPECT_EQ(md5Hex("password"), "5f4dcc3b5aa765d61d8327deb882cf99");
}

// --- KOReaderDocumentId: filename mode -------------------------------------

TEST_F(KosyncClientTest, FilenameHashIsMd5OfBasename) {
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/books/sub/book.epub"), "03053ffc045564439ff7f2cabb3b58c5");
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("book.epub"), "03053ffc045564439ff7f2cabb3b58c5");
}

TEST_F(KosyncClientTest, FilenameHashIgnoresDirectoryButNotCase) {
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/x/book.epub"),
            KOReaderDocumentId::calculateFromFilename("/y/z/book.epub"));
  EXPECT_NE(KOReaderDocumentId::calculateFromFilename("/x/Book.epub"),
            KOReaderDocumentId::calculateFromFilename("/x/book.epub"));
}

TEST_F(KosyncClientTest, FilenameHashOfDirectoryOrEmptyPathIsEmpty) {
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/books/"), "");
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/"), "");
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename(""), "");
}

TEST_F(KosyncClientTest, FilenameHashNeedsNoFileAccess) {
  KOReaderDocumentId::calculateFromFilename("/missing/book.epub");
  EXPECT_EQ(Storage.openCount, 0);
}

// --- KOReaderDocumentId: binary mode ---------------------------------------

TEST_F(KosyncClientTest, BinaryHashMissingFileIsEmpty) {
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/missing.epub"), "");
}

TEST_F(KosyncClientTest, BinaryHashEmptyFileIsMd5OfNothing) {
  Storage.addFile("/books/empty.epub", "");
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/empty.epub"), "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_F(KosyncClientTest, BinaryHashSmallFileHashesWholeFile) {
  Storage.addFile("/books/small.epub", patternBytes(0, 100));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/small.epub"), md5Hex(patternBytes(0, 100)));
}

TEST_F(KosyncClientTest, BinaryHashExactly1024BytesSamplesOnlyOffsetZero) {
  Storage.addFile("/books/1k.epub", patternBytes(0, 1024));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/1k.epub"), md5Hex(patternBytes(0, 1024)));
}

TEST_F(KosyncClientTest, BinaryHashSecondSampleIsTruncatedAtEof) {
  Storage.addFile("/books/1500.epub", patternBytes(0, 1500));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/1500.epub"), md5Hex(patternBytes(0, 1024) + patternBytes(1024, 476)));
}

TEST_F(KosyncClientTest, BinaryHashSkipsOffsetsBeyondEof) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  const std::string expected = md5Hex(patternBytes(0, 1024) + patternBytes(1024, 1024) + patternBytes(4096, 904));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/5000.epub"), expected);
  EXPECT_EQ(expected, expectedPartialMd5(5000));
}

TEST_F(KosyncClientTest, BinaryHashFollowsFullTwelveOffsetSchedule) {
  const size_t size = (static_cast<size_t>(1) << 30) + 100;  // last sample is 100 bytes at 2^30
  Storage.addGeneratedFile("/books/huge.epub", size, patternByte);
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/huge.epub"), expectedPartialMd5(size));
}

TEST_F(KosyncClientTest, BinaryHashReadsExactlyTheOffsetsBelowEof) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  KOReaderDocumentId::calculate("/books/5000.epub");
  const std::vector<std::pair<size_t, size_t>> expected{{0, 1024}, {1024, 1024}, {4096, 904}};
  EXPECT_EQ(Storage.reads, expected);  // offset 16384 and beyond are never touched
}

TEST_F(KosyncClientTest, BinaryHashDoesNotReadAtAnOffsetEqualToTheFileSize) {
  Storage.addFile("/books/4096.epub", patternBytes(0, 4096));
  KOReaderDocumentId::calculate("/books/4096.epub");
  const std::vector<std::pair<size_t, size_t>> expected{{0, 1024}, {1024, 1024}};
  EXPECT_EQ(Storage.reads, expected);  // offset == size is skipped, not read as 0 bytes
}

TEST_F(KosyncClientTest, BinaryHashOnlyDependsOnSampledBytes) {
  Storage.addGeneratedFile("/books/a.epub", 20000, patternByte);
  Storage.addGeneratedFile("/books/b.epub", 20000, [](const size_t i) { return i == 3000 ? 0xAA : patternByte(i); });
  Storage.addGeneratedFile("/books/c.epub", 20000, [](const size_t i) { return i == 4096 ? 0xAA : patternByte(i); });
  const std::string a = KOReaderDocumentId::calculate("/books/a.epub");
  EXPECT_EQ(a, KOReaderDocumentId::calculate("/books/b.epub"));  // 3000 lies between samples
  EXPECT_NE(a, KOReaderDocumentId::calculate("/books/c.epub"));  // 4096 starts a sample
}

TEST_F(KosyncClientTest, BinaryHashSkipsOffsetWhoseSeekFails) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  Storage.failSeekOffsets.insert(1024);
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/5000.epub"), md5Hex(patternBytes(0, 1024) + patternBytes(4096, 904)));
}

TEST_F(KosyncClientTest, BinaryHashWithEverySeekFailingIsMd5OfNothing) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  for (const size_t off : {size_t{0}, size_t{1024}, size_t{4096}}) Storage.failSeekOffsets.insert(off);
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/5000.epub"), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_TRUE(Storage.reads.empty());
}

TEST_F(KosyncClientTest, FilenameHashSplitsOnlyOnForwardSlash) {
  // Backslashes are ordinary filename bytes: a Windows-style path hashes whole.
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("C:\\books\\book.epub"), md5Hex("C:\\books\\book.epub"));
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/sd/C:\\book.epub"), md5Hex("C:\\book.epub"));
}

TEST_F(KosyncClientTest, FilenameHashKeepsSpacesAndNonAsciiBytes) {
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/b/My Book \xC3\xA9.epub"), md5Hex("My Book \xC3\xA9.epub"));
  EXPECT_NE(KOReaderDocumentId::calculateFromFilename("/b/book .epub"),
            KOReaderDocumentId::calculateFromFilename("/b/book.epub"));
}

TEST_F(KosyncClientTest, FilenameHashOfTrailingSlashesIsEmpty) {
  EXPECT_EQ(KOReaderDocumentId::calculateFromFilename("/books/sub//"), "");
}

TEST_F(KosyncClientTest, BinaryHashOneByteOverAnOffsetSamplesThatByte) {
  Storage.addFile("/books/1025.epub", patternBytes(0, 1025));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/1025.epub"), md5Hex(patternBytes(0, 1024) + patternBytes(1024, 1)));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/1025.epub"), expectedPartialMd5(1025));
}

TEST_F(KosyncClientTest, BinaryHashOfSingleByteFileIsThatByte) {
  Storage.addFile("/books/1b.epub", patternBytes(0, 1));
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/1b.epub"), md5Hex(patternBytes(0, 1)));
}

TEST_F(KosyncClientTest, BinaryHashSkipsChunkThatReadsZeroBytes) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  Storage.emptyReadOffsets.insert(1024);
  EXPECT_EQ(KOReaderDocumentId::calculate("/books/5000.epub"), md5Hex(patternBytes(0, 1024) + patternBytes(4096, 904)));
}

TEST_F(KosyncClientTest, BinaryHashOpensTheFileExactlyOnce) {
  Storage.addFile("/books/5000.epub", patternBytes(0, 5000));
  KOReaderDocumentId::calculate("/books/5000.epub");
  EXPECT_EQ(Storage.openCount, 1);
}

TEST_F(KosyncClientTest, BinaryAndFilenameHashesAreIndependent) {
  Storage.addFile("/books/book.epub", patternBytes(0, 3000));
  EXPECT_NE(KOReaderDocumentId::calculate("/books/book.epub"),
            KOReaderDocumentId::calculateFromFilename("/books/book.epub"));
}

// --- KOReaderCredentialStore: base URL and auth key -------------------------

TEST_F(KosyncClientTest, BaseUrlDefaultsToCrossPointServer) {
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), DEFAULT_BASE);
  EXPECT_TRUE(KOREADER_STORE.usesCrossPointSyncServer());
}

TEST_F(KosyncClientTest, BaseUrlAddsHttpWhenSchemeMissing) {
  KOREADER_STORE.setServerUrl("sync.local:8080");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "http://sync.local:8080");
}

TEST_F(KosyncClientTest, BaseUrlKeepsExplicitScheme) {
  KOREADER_STORE.setServerUrl(THIRD_PARTY_BASE);
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), THIRD_PARTY_BASE);
  KOREADER_STORE.setServerUrl("HTTPS://Example.org");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "HTTPS://Example.org");
}

TEST_F(KosyncClientTest, BaseUrlStripsAllTrailingSlashes) {
  KOREADER_STORE.setServerUrl("https://example.org/");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "https://example.org");
  KOREADER_STORE.setServerUrl("https://example.org/kosync///");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "https://example.org/kosync");
  KOREADER_STORE.setServerUrl("host/");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "http://host");
}

TEST_F(KosyncClientTest, BaseUrlOfBareSlashCollapsesToScheme) {
  KOREADER_STORE.setServerUrl("/");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "http:");  // http:/// with every slash stripped
}

TEST_F(KosyncClientTest, UsesCrossPointSyncServerOnlyForExactDefaultBase) {
  KOREADER_STORE.setServerUrl("https://sync.crosspointreader.com/");
  EXPECT_TRUE(KOREADER_STORE.usesCrossPointSyncServer());
  KOREADER_STORE.setServerUrl("sync.crosspointreader.com");  // becomes http://
  EXPECT_FALSE(KOREADER_STORE.usesCrossPointSyncServer());
  KOREADER_STORE.setServerUrl(THIRD_PARTY_BASE);
  EXPECT_FALSE(KOREADER_STORE.usesCrossPointSyncServer());
}

TEST_F(KosyncClientTest, Md5PasswordIsLowercaseHexOrEmpty) {
  EXPECT_EQ(KOREADER_STORE.getMd5Password(), "");
  KOREADER_STORE.setCredentials("reader", "password");
  EXPECT_EQ(KOREADER_STORE.getMd5Password(), "5f4dcc3b5aa765d61d8327deb882cf99");
}

TEST_F(KosyncClientTest, HasCredentialsRequiresBothFields) {
  EXPECT_FALSE(KOREADER_STORE.hasCredentials());
  KOREADER_STORE.setCredentials("reader", "");
  EXPECT_FALSE(KOREADER_STORE.hasCredentials());
  KOREADER_STORE.setCredentials("", "secret");
  EXPECT_FALSE(KOREADER_STORE.hasCredentials());
  KOREADER_STORE.setCredentials("reader", "secret");
  EXPECT_TRUE(KOREADER_STORE.hasCredentials());
}

TEST_F(KosyncClientTest, BaseUrlTreatsWhitespaceAsAHostname) {
  KOREADER_STORE.setServerUrl(" ");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "http:// ");  // no trimming (T144 owns input bounds)
}

TEST_F(KosyncClientTest, BaseUrlAcceptsAnyNonEmptyScheme) {
  KOREADER_STORE.setServerUrl("ftp://files.local");
  EXPECT_EQ(KOREADER_STORE.getBaseUrl(), "ftp://files.local");  // only "://" is looked for
}

TEST_F(KosyncClientTest, Md5PasswordChangesWithEveryPassword) {
  KOREADER_STORE.setCredentials("reader", "a");
  const std::string first = KOREADER_STORE.getMd5Password();
  KOREADER_STORE.setCredentials("reader", "b");
  EXPECT_NE(first, KOREADER_STORE.getMd5Password());
  EXPECT_EQ(KOREADER_STORE.getMd5Password().size(), 32u);
  EXPECT_EQ(KOREADER_STORE.getMd5Password().find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST_F(KosyncClientTest, Md5PasswordHashesRawBytesIncludingColons) {
  KOREADER_STORE.setCredentials("reader", "pa:ss word");
  EXPECT_EQ(KOREADER_STORE.getMd5Password(), md5Hex("pa:ss word"));
}

// --- KOReaderSyncClient::authenticate --------------------------------------

TEST_F(KosyncClientTest, AuthenticateWithoutCredentialsMakesNoRequest) {
  enqueue(200);
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::NO_CREDENTIALS);
  EXPECT_TRUE(exchanges().empty());
  EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 0);
}

TEST_F(KosyncClientTest, AuthenticateRefusedBelowHeapFloors) {
  withCredentials();
  enqueue(200);
  platform_host::setHeap(34999, 1 << 20);
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::LOW_MEMORY);
  platform_host::setHeap(1 << 20, 19999);
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::LOW_MEMORY);
  EXPECT_TRUE(exchanges().empty());
}

TEST_F(KosyncClientTest, AuthenticateProceedsAtExactHeapFloors) {
  withCredentials();
  enqueue(200);
  platform_host::setHeap(35000, 20000);
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::OK);
  EXPECT_EQ(exchanges().size(), 1u);
}

TEST_F(KosyncClientTest, AuthenticateRequestShape) {
  withCredentials();
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::authenticate(), Error::OK);
  const auto& ex = lastExchange();
  EXPECT_EQ(ex.url, std::string(DEFAULT_BASE) + "/users/auth");
  EXPECT_EQ(ex.method, "GET");
  EXPECT_TRUE(ex.body.empty());
  EXPECT_TRUE(ex.insecure);
  EXPECT_TRUE(ex.ended);
  ASSERT_EQ(ex.headers.size(), 4u);
  ASSERT_TRUE(ex.header("Accept"));
  EXPECT_EQ(*ex.header("Accept"), "application/vnd.koreader.v1+json");
  ASSERT_TRUE(ex.header("x-auth-user"));
  EXPECT_EQ(*ex.header("x-auth-user"), "reader");
  ASSERT_TRUE(ex.header("x-auth-key"));
  EXPECT_EQ(*ex.header("x-auth-key"), md5Hex("secret"));
  ASSERT_TRUE(ex.header("Authorization"));
  EXPECT_EQ(*ex.header("Authorization"), "Basic cmVhZGVyOnNlY3JldA==");  // base64("reader:secret")
}

TEST_F(KosyncClientTest, AuthenticateMapsStatusCodes) {
  withCredentials();
  const struct {
    int status;
    Error expected;
  } table[] = {{200, Error::OK},           {201, Error::OK},           {299, Error::OK},
               {401, Error::AUTH_FAILED},  {403, Error::SERVER_ERROR}, {500, Error::SERVER_ERROR},
               {300, Error::SERVER_ERROR}, {199, Error::SERVER_ERROR}, {0, Error::NETWORK_ERROR},
               {-1, Error::NETWORK_ERROR}};
  for (const auto& row : table) {
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::authenticate(), row.expected) << "status " << row.status;
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, row.status);
  }
}

TEST_F(KosyncClientTest, AuthenticateTransportFailureIsNetworkError) {
  withCredentials();  // nothing queued: the stub answers -1 like a failed connect
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::NETWORK_ERROR);
  EXPECT_EQ(KOReaderSyncClient::lastHttpCode, -1);
}

TEST_F(KosyncClientTest, AuthenticateUsesNormalisedCustomServer) {
  withCredentials();
  KOREADER_STORE.setServerUrl("sync.local:8080/");
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::authenticate(), Error::OK);
  EXPECT_EQ(lastExchange().url, "http://sync.local:8080/users/auth");
}

TEST_F(KosyncClientTest, AuthenticateRejectsUnparseableBaseUrl) {
  withCredentials();
  KOREADER_STORE.setServerUrl("/");  // base becomes "http:", which begin() refuses
  enqueue(200);
  EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::NETWORK_ERROR);
  EXPECT_TRUE(exchanges().empty());
  EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 0);
}

// --- KOReaderSyncClient::createUser ----------------------------------------

TEST_F(KosyncClientTest, CreateUserPostsJsonWithMd5PasswordAndNoAuthHeaders) {
  withCredentials();
  enqueue(201);
  ASSERT_EQ(KOReaderSyncClient::createUser(), Error::OK);
  const auto& ex = lastExchange();
  EXPECT_EQ(ex.url, std::string(DEFAULT_BASE) + "/users/create");
  EXPECT_EQ(ex.method, "POST");
  EXPECT_TRUE(ex.ended);
  ASSERT_EQ(ex.headers.size(), 2u);
  EXPECT_EQ(*ex.header("Accept"), "application/vnd.koreader.v1+json");
  EXPECT_EQ(*ex.header("Content-Type"), "application/json");
  EXPECT_EQ(ex.header("x-auth-user"), nullptr);
  EXPECT_EQ(ex.header("Authorization"), nullptr);
  const JsonDocument body = parseBody(ex.body);
  EXPECT_EQ(body["username"].as<std::string>(), "reader");
  EXPECT_EQ(body["password"].as<std::string>(), md5Hex("secret"));
  EXPECT_EQ(body.as<JsonObjectConst>().size(), 2u);
}

TEST_F(KosyncClientTest, CreateUserMapsStatusCodes) {
  withCredentials();
  const struct {
    int status;
    Error expected;
  } table[] = {{200, Error::OK},           {201, Error::OK},           {402, Error::USER_EXISTS},
               {401, Error::SERVER_ERROR}, {500, Error::SERVER_ERROR}, {-1, Error::NETWORK_ERROR}};
  for (const auto& row : table) {
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::createUser(), row.expected) << "status " << row.status;
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, row.status);
  }
}

TEST_F(KosyncClientTest, CreateUserGatesOnCredentialsAndHeap) {
  EXPECT_EQ(KOReaderSyncClient::createUser(), Error::NO_CREDENTIALS);
  withCredentials();
  platform_host::setHeap(34999, 1 << 20);
  EXPECT_EQ(KOReaderSyncClient::createUser(), Error::LOW_MEMORY);
  EXPECT_TRUE(exchanges().empty());
}

TEST_F(KosyncClientTest, CreateUserEscapesJsonSpecialCharacters) {
  KOREADER_STORE.setCredentials("a\"b\\c\nd", "secret");
  enqueue(201);
  ASSERT_EQ(KOReaderSyncClient::createUser(), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["username"].as<std::string>(), "a\"b\\c\nd");
}

TEST_F(KosyncClientTest, BasicAuthEncodesEverythingAfterTheFirstColon) {
  // RFC 7617 splits on the first colon, so a password may contain one.
  KOREADER_STORE.setCredentials("reader", "pa:ss");
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::authenticate(), Error::OK);
  EXPECT_EQ(*lastExchange().header("Authorization"), "Basic cmVhZGVyOnBhOnNz");  // base64("reader:pa:ss")
  EXPECT_EQ(*lastExchange().header("x-auth-key"), md5Hex("pa:ss"));
}

TEST_F(KosyncClientTest, BasicAuthEncodesNonAsciiCredentialsAsUtf8Bytes) {
  KOREADER_STORE.setCredentials("r\xC3\xA9" "ader", "s");
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::authenticate(), Error::OK);
  EXPECT_EQ(*lastExchange().header("x-auth-user"), "r\xC3\xA9" "ader");
  EXPECT_EQ(*lastExchange().header("Authorization"), "Basic csOpYWRlcjpz");  // base64("r<U+00E9>ader:s")
}

TEST_F(KosyncClientTest, UnusualStatusCodesAllLandInTheCatchAll) {
  withCredentials();
  const struct {
    int status;
    Error expected;
  } table[] = {{100, Error::SERVER_ERROR}, {301, Error::SERVER_ERROR}, {302, Error::SERVER_ERROR},
               {418, Error::SERVER_ERROR}, {503, Error::SERVER_ERROR}, {999, Error::SERVER_ERROR}};
  for (const auto& row : table) {
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::authenticate(), row.expected) << "status " << row.status;
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::createUser(), row.expected) << "status " << row.status;
    enqueue(row.status, "{}");
    KOReaderProgress out;
    EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), row.expected) << "status " << row.status;
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), row.expected) << "status " << row.status;
  }
}

TEST_F(KosyncClientTest, NegativeAndZeroStatusCodesAreNetworkErrorsEverywhere) {
  withCredentials();
  for (const int status : {0, -1, -11}) {
    enqueue(status);
    EXPECT_EQ(KOReaderSyncClient::authenticate(), Error::NETWORK_ERROR) << status;
    enqueue(status);
    EXPECT_EQ(KOReaderSyncClient::createUser(), Error::NETWORK_ERROR) << status;
    enqueue(status, "{}");
    KOReaderProgress out;
    EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::NETWORK_ERROR) << status;
    enqueue(status);
    EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::NETWORK_ERROR) << status;
  }
}

// --- KOReaderSyncClient::getProgress ---------------------------------------

TEST_F(KosyncClientTest, GetProgressRequestShape) {
  withCredentials();
  enqueue(200, "{}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  const auto& ex = lastExchange();
  EXPECT_EQ(ex.url, std::string(DEFAULT_BASE) + "/syncs/progress/" + DOC_HASH);
  EXPECT_EQ(ex.method, "GET");
  EXPECT_TRUE(ex.ended);
  EXPECT_EQ(ex.headers.size(), 4u);
  EXPECT_EQ(*ex.header("x-auth-key"), md5Hex("secret"));
}

TEST_F(KosyncClientTest, GetProgressMapsStatusCodes) {
  withCredentials();
  const struct {
    int status;
    Error expected;
  } table[] = {{204, Error::NOT_FOUND},    {404, Error::NOT_FOUND},    {401, Error::AUTH_FAILED},
               {403, Error::SERVER_ERROR}, {500, Error::SERVER_ERROR}, {0, Error::NETWORK_ERROR},
               {-1, Error::NETWORK_ERROR}};
  for (const auto& row : table) {
    enqueue(row.status, "{\"progress\":\"ignored\"}");
    KOReaderProgress out;
    EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), row.expected) << "status " << row.status;
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, row.status);
    EXPECT_TRUE(lastExchange().ended);
  }
}

TEST_F(KosyncClientTest, GetProgressGatesOnCredentialsAndHeap) {
  KOReaderProgress out;
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::NO_CREDENTIALS);
  withCredentials();
  platform_host::setHeap(1 << 20, 19999);
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::LOW_MEMORY);
  EXPECT_TRUE(exchanges().empty());
}

TEST_F(KosyncClientTest, GetProgressParsesReferenceResponse) {
  withCredentials();
  enqueue(200,
          R"({"document":"server-side-id","progress":"/body/DocFragment[5]/body/p[3]/text().7","percentage":0.42,)"
          R"("device":"KOReader","device_id":"abc123","timestamp":1735689600})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.document, DOC_HASH);  // the requested id, not the body's
  EXPECT_EQ(out.progress, "/body/DocFragment[5]/body/p[3]/text().7");
  EXPECT_FLOAT_EQ(out.percentage, 0.42f);
  EXPECT_EQ(out.device, "KOReader");
  EXPECT_EQ(out.deviceId, "abc123");
  EXPECT_EQ(out.timestamp, 1735689600);
  EXPECT_FALSE(out.position.has_value());
}

TEST_F(KosyncClientTest, GetProgressSuccessCodesOtherThan204Parse) {
  withCredentials();
  enqueue(201, R"({"percentage":0.5})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.5f);
}

TEST_F(KosyncClientTest, GetProgressNonJsonBodyIsJsonError) {
  withCredentials();
  enqueue(200, "<html><body>login</body></html>");
  KOReaderProgress out;
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  EXPECT_TRUE(lastExchange().ended);
  EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 200);
}

TEST_F(KosyncClientTest, GetProgressEmptyAndTruncatedBodiesAreJsonErrors) {
  withCredentials();
  KOReaderProgress out;
  enqueue(200, "");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  enqueue(200, R"({"progress":"/body/DocFragment[1]/body)");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  enqueue(200, R"({"percentage":0.5,)");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
}

TEST_F(KosyncClientTest, GetProgressHostileNestingIsJsonError) {
  withCredentials();
  // ArduinoJson refuses documents past its nesting limit (TooDeep).
  std::string deep(200, '[');
  deep += std::string(200, ']');
  enqueue(200, deep);
  KOReaderProgress out;
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  enqueue(200, "{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":1}}}}}}}}}}}");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
}

TEST_F(KosyncClientTest, GetProgressUnterminatedStringAndBadEscapeAreJsonErrors) {
  withCredentials();
  KOReaderProgress out;
  enqueue(200, "{\"progress\":\"unterminated}");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  enqueue(200, "{\"progress\":\"\\uZZZZ\"}");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
  enqueue(200, "{progress:1}");  // ArduinoJson accepts an unquoted key that starts with a letter
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.progress, "1");
  enqueue(200, "{@:1}");  // ... but not one that starts with a symbol
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
}

TEST_F(KosyncClientTest, GetProgressStopsAtTheEndOfTheFirstDocument) {
  withCredentials();
  KOReaderProgress out;
  enqueue(200, "\r\n\t  {\"percentage\":0.5}  \n");
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.5f);
  // ArduinoJson stops at the closing brace and never inspects the rest, so a
  // body with junk appended still parses. Nothing downstream sees the junk.
  enqueue(200, "{\"percentage\":0.25} <html>error</html>");
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.25f);
  enqueue(200, "{\"percentage\":0.75}{\"percentage\":0.9}");
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.75f);  // only the first document is read
}

TEST_F(KosyncClientTest, GetProgressLeavesTheOutputUntouchedOnEveryNonParsingResult) {
  withCredentials();
  // The activity reads remoteProgress after a failed fetch, so pin that a
  // non-OK result writes nothing: whatever the caller passed in survives.
  for (const int status : {404, 204, 401, 500, -1}) {
    KOReaderProgress out = sampleProgress();
    out.percentage = 0.875f;
    out.timestamp = 42;
    out.position = samplePosition();
    enqueue(status, R"({"progress":"remote","percentage":0.1})");
    EXPECT_NE(KOReaderSyncClient::getProgress("other-hash", out), Error::OK) << status;
    EXPECT_EQ(out.document, DOC_HASH) << status;
    EXPECT_EQ(out.progress, "/body/DocFragment[3]/body/p[7]/text().12") << status;
    EXPECT_FLOAT_EQ(out.percentage, 0.875f) << status;
    EXPECT_EQ(out.timestamp, 42) << status;
    EXPECT_TRUE(out.position.has_value()) << status;
  }
}

TEST_F(KosyncClientTest, GetProgressBodyIsTruncatedAtAnEmbeddedNul) {
  withCredentials();
  // The client hands getString().c_str() to deserializeJson, so a NUL inside
  // the body ends the document early (KOReaderSyncClient.cpp:183).
  std::string body = "{\"percentage\":0.5}";
  body.push_back('\0');
  body += "{\"percentage\":0.9}";
  enqueue(200, body);
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.5f);
}

TEST_F(KosyncClientTest, GetProgressHugeBodyIsAcceptedUpToTheStringLengthLimit) {
  withCredentials();
  KOReaderProgress out;
  // The body itself is unbounded (T144 owns bounds), but ArduinoJson stores a
  // string length in ARDUINOJSON_STRING_LENGTH_SIZE bytes -- 2 on every target
  // with a pointer wider than 16 bits, host and ESP32-C3 alike. A single field
  // longer than 65535 bytes fails the whole document with NoMemory, which the
  // client reports as JSON_ERROR.
  enqueue(200, "{\"pad\":\"" + std::string(65535, 'x') + "\",\"percentage\":0.5}");
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.5f);

  enqueue(200, "{\"pad\":\"" + std::string(65536, 'x') + "\",\"percentage\":0.5}");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);

  enqueue(200, "{\"progress\":\"" + std::string(200000, 'p') + "\"}");
  EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::JSON_ERROR);
}

TEST_F(KosyncClientTest, GetProgressManySmallFieldsAreAcceptedUnbounded) {
  withCredentials();
  // A 200 KB body made of short values parses fine: only per-string length is
  // capped, not the document.
  std::string body = "{";
  for (int i = 0; i < 4000; i++) body += "\"k" + std::to_string(i) + "\":\"" + std::string(30, 'v') + "\",";
  body += "\"percentage\":0.5}";
  ASSERT_GT(body.size(), 150000u);
  enqueue(200, body);
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.5f);
}

TEST_F(KosyncClientTest, GetProgressManyKeysDoNotBreakFieldLookup) {
  withCredentials();
  std::string body = "{";
  for (int i = 0; i < 500; i++) body += "\"k" + std::to_string(i) + "\":" + std::to_string(i) + ",";
  body += "\"progress\":\"/body/DocFragment[2]/body\"}";
  enqueue(200, body);
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.progress, "/body/DocFragment[2]/body");
}

TEST_F(KosyncClientTest, GetProgressDuplicateKeysResolveToTheLast) {
  withCredentials();
  enqueue(200, R"({"percentage":0.25,"percentage":0.75,"progress":"a","progress":"b"})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, 0.75f);  // ArduinoJson overwrites on a repeated key
  EXPECT_EQ(out.progress, "b");
}

TEST_F(KosyncClientTest, GetProgressWithEmptyDocumentIdStillRequests) {
  withCredentials();
  enqueue(200, "{}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress("", out), Error::OK);
  EXPECT_EQ(lastExchange().url, std::string(DEFAULT_BASE) + "/syncs/progress/");
  EXPECT_TRUE(out.document.empty());
}

TEST_F(KosyncClientTest, GetProgressDoesNotUrlEncodeTheDocumentId) {
  withCredentials();
  enqueue(200, "{}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress("a b/../c?x=1", out), Error::OK);
  EXPECT_EQ(lastExchange().url, std::string(DEFAULT_BASE) + "/syncs/progress/a b/../c?x=1");
}

TEST_F(KosyncClientTest, GetProgressEmptyObjectYieldsDefaults) {
  withCredentials();
  enqueue(200, "{}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.document, DOC_HASH);
  EXPECT_FLOAT_EQ(out.percentage, 0.0f);
  EXPECT_EQ(out.timestamp, 0);
  EXPECT_FALSE(out.position.has_value());
  // ArduinoJson's std::string converter serialises a non-string variant, so an
  // absent string field arrives as the four characters "null" rather than "".
  // KOReaderSyncClient.cpp:192-195 stores that verbatim.
  EXPECT_EQ(out.progress, "null");
  EXPECT_EQ(out.device, "null");
  EXPECT_EQ(out.deviceId, "null");
}

TEST_F(KosyncClientTest, GetProgressNonObjectJsonYieldsDefaults) {
  withCredentials();
  KOReaderProgress out;
  for (const char* body : {"[1,2,3]", "42", "\"text\"", "null"}) {
    enqueue(200, body);
    out.percentage = 9.0f;
    out.position = samplePosition();
    EXPECT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK) << body;
    EXPECT_EQ(out.progress, "null") << body;  // subscripting a non-object yields an unbound variant
    EXPECT_FLOAT_EQ(out.percentage, 0.0f) << body;
    EXPECT_EQ(out.timestamp, 0) << body;
    EXPECT_FALSE(out.position.has_value()) << body;
  }
}

TEST_F(KosyncClientTest, GetProgressWrongFieldTypesAreCoercedNotRejected) {
  withCredentials();
  enqueue(200, R"({"progress":42,"percentage":"0.25","device":true,"device_id":null,"timestamp":"soon"})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.progress, "42");  // ArduinoJson's std::string converter serialises non-strings
  EXPECT_FLOAT_EQ(out.percentage, 0.25f);
  EXPECT_EQ(out.device, "true");
  EXPECT_EQ(out.deviceId, "null");  // an explicit JSON null is serialised, not emptied
  EXPECT_EQ(out.timestamp, 0);      // "soon" is not a number
}

TEST_F(KosyncClientTest, GetProgressOutOfRangeNumbersPassThrough) {
  withCredentials();
  enqueue(200, R"({"percentage":-1.5,"timestamp":-7})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FLOAT_EQ(out.percentage, -1.5f);
  EXPECT_EQ(out.timestamp, -7);
}

TEST_F(KosyncClientTest, GetProgressHugeXpathIsAcceptedUnbounded) {
  withCredentials();
  const std::string huge(5000, 'p');
  enqueue(200, "{\"progress\":\"" + huge + "\"}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_EQ(out.progress.size(), 5000u);  // no field bound yet (T144)
}

TEST_F(KosyncClientTest, GetProgressPositionIgnoredOnThirdPartyServer) {
  withCredentials();
  KOREADER_STORE.setServerUrl(THIRD_PARTY_BASE);
  enqueue(200, R"({"percentage":0.5,"position":{"pctQ":500000,"spine":2,"page":3,"pages":8,"para":4,"xpath":"/x"}})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FALSE(out.position.has_value());
}

TEST_F(KosyncClientTest, GetProgressPositionParsedOnCrossPointServer) {
  withCredentials();
  enqueue(200,
          R"({"percentage":0.5,"position":{"pctQ":500000,"spine":2,"page":3,"pages":8,"para":4,)"
          R"("xpath":"/body/DocFragment[3]/body/p[4]"}})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  ASSERT_TRUE(out.position.has_value());
  EXPECT_EQ(out.position->pctQ, 500000u);
  EXPECT_EQ(out.position->spineIndex, 2);
  EXPECT_EQ(out.position->pageNumber, 3);
  EXPECT_EQ(out.position->totalPages, 8);
  ASSERT_TRUE(out.position->paragraphIndex.has_value());
  EXPECT_EQ(*out.position->paragraphIndex, 4);
  EXPECT_EQ(out.position->xpath, "/body/DocFragment[3]/body/p[4]");
}

TEST_F(KosyncClientTest, GetProgressPositionMissingFieldsUseDefaults) {
  withCredentials();
  enqueue(200, R"({"position":{}})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  ASSERT_TRUE(out.position.has_value());
  EXPECT_EQ(out.position->pctQ, 0u);
  EXPECT_EQ(out.position->spineIndex, 0);
  EXPECT_EQ(out.position->pageNumber, 0);
  EXPECT_EQ(out.position->totalPages, 1);  // pages 0 -> floored to 1
  EXPECT_FALSE(out.position->paragraphIndex.has_value());
  EXPECT_TRUE(out.position->xpath.empty());
}

TEST_F(KosyncClientTest, GetProgressPositionZeroParaAndNonStringXpath) {
  withCredentials();
  enqueue(200, R"({"position":{"pages":0,"para":0,"xpath":123}})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  ASSERT_TRUE(out.position.has_value());
  EXPECT_EQ(out.position->totalPages, 1);
  EXPECT_FALSE(out.position->paragraphIndex.has_value());
  EXPECT_TRUE(out.position->xpath.empty());
}

TEST_F(KosyncClientTest, GetProgressPositionNonObjectIsIgnored) {
  withCredentials();
  KOReaderProgress out;
  for (const char* body : {R"({"position":"x"})", R"({"position":5})", R"({"position":[1]})", R"({"position":null})"}) {
    enqueue(200, body);
    out.position = samplePosition();
    ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK) << body;
    EXPECT_FALSE(out.position.has_value()) << body;
  }
}

TEST_F(KosyncClientTest, GetProgressPositionOverflowingIntegersBecomeZero) {
  withCredentials();
  enqueue(200, R"({"position":{"spine":70000,"page":-1,"pages":65536,"para":100000}})");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  ASSERT_TRUE(out.position.has_value());
  EXPECT_EQ(out.position->spineIndex, 0);
  EXPECT_EQ(out.position->pageNumber, 0);
  EXPECT_EQ(out.position->totalPages, 1);
  EXPECT_FALSE(out.position->paragraphIndex.has_value());
}

TEST_F(KosyncClientTest, GetProgressPositionXpathIsNotCappedOnParse) {
  withCredentials();
  const std::string longXpath(500, 'x');
  enqueue(200, "{\"position\":{\"xpath\":\"" + longXpath + "\"}}");
  KOReaderProgress out;
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  ASSERT_TRUE(out.position.has_value());
  EXPECT_EQ(out.position->xpath.size(), 500u);  // the 120-byte cap applies to uploads only
}

TEST_F(KosyncClientTest, GetProgressResetsStalePositionBeforeParsing) {
  withCredentials();
  enqueue(200, "{}");
  KOReaderProgress out;
  out.position = samplePosition();
  ASSERT_EQ(KOReaderSyncClient::getProgress(DOC_HASH, out), Error::OK);
  EXPECT_FALSE(out.position.has_value());
}

// --- KOReaderSyncClient::updateProgress ------------------------------------

TEST_F(KosyncClientTest, UpdateProgressRequestShape) {
  withCredentials();
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::OK);
  const auto& ex = lastExchange();
  EXPECT_EQ(ex.url, std::string(DEFAULT_BASE) + "/syncs/progress");
  EXPECT_EQ(ex.method, "PUT");
  EXPECT_TRUE(ex.ended);
  ASSERT_EQ(ex.headers.size(), 5u);
  EXPECT_EQ(*ex.header("Accept"), "application/vnd.koreader.v1+json");
  EXPECT_EQ(*ex.header("x-auth-user"), "reader");
  EXPECT_EQ(*ex.header("x-auth-key"), md5Hex("secret"));
  EXPECT_EQ(*ex.header("Authorization"), "Basic cmVhZGVyOnNlY3JldA==");
  EXPECT_EQ(*ex.header("Content-Type"), "application/json");
}

TEST_F(KosyncClientTest, UpdateProgressBodyHasExactlyTheRequiredFields) {
  withCredentials();
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["document"].as<std::string>(), DOC_HASH);
  EXPECT_EQ(body["progress"].as<std::string>(), "/body/DocFragment[3]/body/p[7]/text().12");
  EXPECT_FLOAT_EQ(body["percentage"].as<float>(), 0.375f);
  EXPECT_EQ(body["device"].as<std::string>(), "CrossPoint");
  EXPECT_EQ(body["device_id"].as<std::string>(), "crosspoint-reader");
  EXPECT_FALSE(body["metadata"].is<JsonObjectConst>());
  EXPECT_FALSE(body["position"].is<JsonObjectConst>());
  EXPECT_TRUE(body["timestamp"].isNull());  // no client timestamp
  EXPECT_EQ(body.as<JsonObjectConst>().size(), 5u);
}

TEST_F(KosyncClientTest, UpdateProgressIncludesMetadataWhenPresent) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.metadata = KOReaderMetadata{"book.epub", "A Title", "Some Author"};
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["metadata"]["filename"].as<std::string>(), "book.epub");
  EXPECT_EQ(body["metadata"]["title"].as<std::string>(), "A Title");
  EXPECT_EQ(body["metadata"]["authors"].as<std::string>(), "Some Author");
  EXPECT_EQ(body["metadata"].as<JsonObjectConst>().size(), 3u);
}

TEST_F(KosyncClientTest, UpdateProgressMetadataIsNotServerGated) {
  // Only the `position` extension is CrossPoint-only; metadata mirrors KOReader
  // PR #15306 and goes to whichever server is configured.
  withCredentials();
  KOREADER_STORE.setServerUrl(THIRD_PARTY_BASE);
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.metadata = KOReaderMetadata{"book.epub", "A Title", "Some Author"};
  p.position = samplePosition();
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["metadata"]["filename"].as<std::string>(), "book.epub");
  EXPECT_TRUE(body["position"].isNull());
}

TEST_F(KosyncClientTest, UpdateProgressSendsPositionOnlyToCrossPointServer) {
  withCredentials();
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();

  KOREADER_STORE.setServerUrl(THIRD_PARTY_BASE);
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_TRUE(parseBody(lastExchange().body)["position"].isNull());

  KOREADER_STORE.setServerUrl("");
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["position"]["pctQ"].as<uint32_t>(), 375000u);
  EXPECT_EQ(body["position"]["spine"].as<int>(), 2);
  EXPECT_EQ(body["position"]["page"].as<int>(), 4);
  EXPECT_EQ(body["position"]["pages"].as<int>(), 9);
  EXPECT_EQ(body["position"]["para"].as<int>(), 7);
  EXPECT_EQ(body["position"]["xpath"].as<std::string>(), "/body/DocFragment[3]/body/p[7]");
  EXPECT_EQ(body["position"].as<JsonObjectConst>().size(), 6u);
}

TEST_F(KosyncClientTest, UpdateProgressOmitsUnsetParaAndEmptyXpath) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();
  p.position->paragraphIndex.reset();
  p.position->xpath.clear();
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  ASSERT_TRUE(body["position"].is<JsonObjectConst>());
  EXPECT_TRUE(body["position"]["para"].isNull());
  EXPECT_TRUE(body["position"]["xpath"].isNull());
  EXPECT_EQ(body["position"].as<JsonObjectConst>().size(), 4u);
}

TEST_F(KosyncClientTest, UpdateProgressXpathCapIs120Bytes) {
  withCredentials();
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();

  p.position->xpath = std::string(120, 'a');
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_EQ(parseBody(lastExchange().body)["position"]["xpath"].as<std::string>().size(), 120u);

  p.position->xpath = std::string(121, 'a');
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_TRUE(body["position"]["xpath"].isNull());
  EXPECT_EQ(body["position"]["para"].as<int>(), 7);  // the rest of the position survives
  EXPECT_EQ(body["progress"].as<std::string>(), p.progress);  // standard xpointer is never capped
}

TEST_F(KosyncClientTest, UpdateProgressXpathCapCountsBytesNotCodepoints) {
  withCredentials();
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();

  std::string utf8;
  for (int i = 0; i < 60; i++) utf8 += "\xC3\xA9";  // 60 x U+00E9 = 120 bytes, 60 codepoints
  p.position->xpath = utf8;
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_EQ(parseBody(lastExchange().body)["position"]["xpath"].as<std::string>(), utf8);

  utf8 += "\xC3\xA9";  // 122 bytes, still only 61 codepoints
  p.position->xpath = utf8;
  enqueue(200);
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_TRUE(parseBody(lastExchange().body)["position"]["xpath"].isNull());
}

TEST_F(KosyncClientTest, UpdateProgressMapsStatusCodes) {
  withCredentials();
  const struct {
    int status;
    Error expected;
  } table[] = {{200, Error::OK},           {201, Error::OK},           {204, Error::OK},
               {401, Error::AUTH_FAILED},  {402, Error::SERVER_ERROR}, {404, Error::SERVER_ERROR},
               {500, Error::SERVER_ERROR}, {0, Error::NETWORK_ERROR},  {-1, Error::NETWORK_ERROR}};
  for (const auto& row : table) {
    enqueue(row.status);
    EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), row.expected) << "status " << row.status;
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, row.status);
  }
}

TEST_F(KosyncClientTest, UpdateProgressGatesOnCredentialsAndHeap) {
  EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::NO_CREDENTIALS);
  withCredentials();
  platform_host::setHeap(34999, 1 << 20);
  EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::LOW_MEMORY);
  platform_host::setHeap(1 << 20, 19999);
  EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::LOW_MEMORY);
  EXPECT_TRUE(exchanges().empty());
}

TEST_F(KosyncClientTest, UpdateProgressPercentageIsNotClamped) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.percentage = 1.5f;
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_FLOAT_EQ(parseBody(lastExchange().body)["percentage"].as<float>(), 1.5f);
}

TEST_F(KosyncClientTest, UpdateProgressSerialisesEmptyStringsVerbatim) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p;
  p.document = "";
  p.progress = "";
  p.percentage = 0.0f;
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["document"].as<std::string>(), "");
  EXPECT_EQ(body["progress"].as<std::string>(), "");
  EXPECT_FLOAT_EQ(body["percentage"].as<float>(), 0.0f);
  EXPECT_EQ(body.as<JsonObjectConst>().size(), 5u);  // still every required field
}

TEST_F(KosyncClientTest, UpdateProgressEscapesControlCharactersInTheXpointer) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.progress = "/body/\"quote\"\\back\nnewline\ttab";
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_EQ(parseBody(lastExchange().body)["progress"].as<std::string>(), p.progress);
}

TEST_F(KosyncClientTest, UpdateProgressMetadataWithEmptyStringsIsStillSent) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.metadata = KOReaderMetadata{};
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  ASSERT_TRUE(body["metadata"].is<JsonObjectConst>());
  EXPECT_EQ(body["metadata"].as<JsonObjectConst>().size(), 3u);
  EXPECT_EQ(body["metadata"]["title"].as<std::string>(), "");
}

TEST_F(KosyncClientTest, UpdateProgressLongDocumentIdAndXpointerAreNotBounded) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.document = std::string(4000, 'd');
  p.progress = std::string(4000, 'x');
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);  // no field bound yet (T144)
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["document"].as<std::string>().size(), 4000u);
  EXPECT_EQ(body["progress"].as<std::string>().size(), 4000u);
}

TEST_F(KosyncClientTest, UpdateProgressPositionIsDroppedWhenAbsentEvenOnCrossPointServer) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.position.reset();
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_TRUE(parseBody(lastExchange().body)["position"].isNull());
}

TEST_F(KosyncClientTest, UpdateProgressPositionSurvivesExtremeCounters) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();
  p.position->pctQ = 4294967295u;
  p.position->spineIndex = 65535;
  p.position->pageNumber = 65535;
  p.position->totalPages = 65535;
  p.position->paragraphIndex = uint16_t{65535};
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  const JsonDocument body = parseBody(lastExchange().body);
  EXPECT_EQ(body["position"]["pctQ"].as<uint32_t>(), 4294967295u);
  EXPECT_EQ(body["position"]["pages"].as<int>(), 65535);
  EXPECT_EQ(body["position"]["para"].as<int>(), 65535);
}

TEST_F(KosyncClientTest, UpdateProgressXpathIsOmittedWhenEmptyEvenBelowTheCap) {
  withCredentials();
  enqueue(200);
  KOReaderProgress p = sampleProgress();
  p.position = samplePosition();
  p.position->xpath = "";
  ASSERT_EQ(KOReaderSyncClient::updateProgress(p), Error::OK);
  EXPECT_TRUE(parseBody(lastExchange().body)["position"]["xpath"].isNull());
}

TEST_F(KosyncClientTest, UpdateProgressRejectsUnparseableBaseUrl) {
  withCredentials();
  KOREADER_STORE.setServerUrl("/");  // base becomes "http:"
  enqueue(200);
  EXPECT_EQ(KOReaderSyncClient::updateProgress(sampleProgress()), Error::NETWORK_ERROR);
  EXPECT_TRUE(exchanges().empty());
  EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 0);
}

// --- errorString --------------------------------------------------------------

TEST_F(KosyncClientTest, ErrorStringsCoverEveryCode) {
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::OK), "Success");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::NO_CREDENTIALS), "No credentials configured");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::NETWORK_ERROR), "Network error");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::AUTH_FAILED), "Authentication failed");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::SERVER_ERROR), "Server error (try again later)");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::JSON_ERROR), "JSON parse error");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::NOT_FOUND), "No progress found");
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::LOW_MEMORY), "Not enough memory for sync — please retry");
  // USER_EXISTS is the only named code without its own text; it and every other
  // value in the enum's range fall through to the default arm. 15 is the top of
  // that range (9 enumerators need 4 bits), so the cast stays well-defined.
  EXPECT_STREQ(KOReaderSyncClient::errorString(Error::USER_EXISTS), "Unknown error");
  EXPECT_STREQ(KOReaderSyncClient::errorString(static_cast<Error>(15)), "Unknown error");
}

// --- SmartSync decision table -------------------------------------------------

TEST_F(KosyncClientTest, AlternateMatchMethodFlipsAndNames) {
  EXPECT_EQ(SmartSync::alternateMatchMethod(DocumentMatchMethod::FILENAME), DocumentMatchMethod::BINARY);
  EXPECT_EQ(SmartSync::alternateMatchMethod(DocumentMatchMethod::BINARY), DocumentMatchMethod::FILENAME);
  EXPECT_STREQ(SmartSync::matchMethodName(DocumentMatchMethod::FILENAME), "filename");
  EXPECT_STREQ(SmartSync::matchMethodName(DocumentMatchMethod::BINARY), "binary");
}

TEST_F(KosyncClientTest, AlternateIdProbedOnlyWhenHashedAndDifferent) {
  EXPECT_FALSE(SmartSync::shouldProbeAlternate("abc", ""));
  EXPECT_FALSE(SmartSync::shouldProbeAlternate("abc", "abc"));
  EXPECT_TRUE(SmartSync::shouldProbeAlternate("abc", "def"));
  EXPECT_TRUE(SmartSync::shouldProbeAlternate("", "def"));
}

TEST_F(KosyncClientTest, AlternateRecordNeverAdoptedUnlessItExists) {
  for (const Error alt : {Error::NOT_FOUND, Error::NETWORK_ERROR, Error::AUTH_FAILED, Error::JSON_ERROR}) {
    EXPECT_FALSE(SmartSync::preferAlternate(Error::NOT_FOUND, 0.0f, alt, 0.9f)) << alt;
    EXPECT_FALSE(SmartSync::preferAlternate(Error::OK, 0.1f, alt, 0.9f)) << alt;
  }
}

TEST_F(KosyncClientTest, AlternateRecordAdoptedWhenPrimaryMissing) {
  EXPECT_TRUE(SmartSync::preferAlternate(Error::NOT_FOUND, 0.0f, Error::OK, 0.2f));
  EXPECT_TRUE(SmartSync::preferAlternate(Error::NOT_FOUND, 0.0f, Error::OK, 0.0f));  // even at 0%
}

TEST_F(KosyncClientTest, AlternateRecordAdoptedOnlyWhenFurtherThanPrimary) {
  EXPECT_TRUE(SmartSync::preferAlternate(Error::OK, 0.30f, Error::OK, 0.31f));
  EXPECT_FALSE(SmartSync::preferAlternate(Error::OK, 0.30f, Error::OK, 0.30f));
  EXPECT_FALSE(SmartSync::preferAlternate(Error::OK, 0.30f, Error::OK, 0.29f));
}

TEST_F(KosyncClientTest, AlternateRecordCanOverrideFailedPrimaryFetch) {
  // A primary transport/auth failure leaves remote percentage at 0, so any
  // alternate record ahead of 0 wins and the sync proceeds on it.
  EXPECT_TRUE(SmartSync::preferAlternate(Error::NETWORK_ERROR, 0.0f, Error::OK, 0.05f));
  EXPECT_FALSE(SmartSync::preferAlternate(Error::SERVER_ERROR, 0.0f, Error::OK, 0.0f));
}

TEST_F(KosyncClientTest, ResolveAlreadySyncedWithinEpsilon) {
  EXPECT_EQ(SmartSync::resolve(0.5f, 0.5f), SmartSync::Resolution::ALREADY_SYNCED);
  EXPECT_EQ(SmartSync::resolve(0.001f, 0.0f), SmartSync::Resolution::ALREADY_SYNCED);  // exactly epsilon
  EXPECT_EQ(SmartSync::resolve(0.0f, 0.001f), SmartSync::Resolution::ALREADY_SYNCED);  // symmetric
  EXPECT_EQ(SmartSync::resolve(0.4f, 0.4009f), SmartSync::Resolution::ALREADY_SYNCED);
}

TEST_F(KosyncClientTest, ResolveUploadWhenLocalAhead) {
  EXPECT_EQ(SmartSync::resolve(0.0011f, 0.0f), SmartSync::Resolution::UPLOAD_LOCAL);
  EXPECT_EQ(SmartSync::resolve(0.9f, 0.1f), SmartSync::Resolution::UPLOAD_LOCAL);
  EXPECT_EQ(SmartSync::resolve(1.0f, 0.0f), SmartSync::Resolution::UPLOAD_LOCAL);
}

TEST_F(KosyncClientTest, ResolveApplyWhenRemoteAhead) {
  EXPECT_EQ(SmartSync::resolve(0.0f, 0.0011f), SmartSync::Resolution::APPLY_REMOTE);
  EXPECT_EQ(SmartSync::resolve(0.1f, 0.9f), SmartSync::Resolution::APPLY_REMOTE);
}

TEST_F(KosyncClientTest, AskModePreselectsFurthestSide) {
  EXPECT_EQ(SmartSync::askModeDefaultOption(0.6f, 0.5f), 1);  // Upload local
  EXPECT_EQ(SmartSync::askModeDefaultOption(0.5f, 0.5f), 0);  // Apply remote on a tie
  EXPECT_EQ(SmartSync::askModeDefaultOption(0.4f, 0.5f), 0);
}

// --- SmartSync rich-position construction --------------------------------------

TEST_F(KosyncClientTest, RichPositionQuantizesPercentageToMillionths) {
  EXPECT_EQ(SmartSync::buildRichPosition(0.25f, 0, 0, 1, std::nullopt, "").pctQ, 250000u);
  EXPECT_EQ(SmartSync::buildRichPosition(1.0f, 0, 0, 1, std::nullopt, "").pctQ, 1000000u);
  EXPECT_EQ(SmartSync::buildRichPosition(0.0f, 0, 0, 1, std::nullopt, "").pctQ, 0u);
  EXPECT_EQ(SmartSync::buildRichPosition(0.1234565f, 0, 0, 1, std::nullopt, "").pctQ, 123457u);  // rounds
}

TEST_F(KosyncClientTest, RichPositionClampsPercentage) {
  EXPECT_EQ(SmartSync::buildRichPosition(-0.5f, 0, 0, 1, std::nullopt, "").pctQ, 0u);
  EXPECT_EQ(SmartSync::buildRichPosition(1.5f, 0, 0, 1, std::nullopt, "").pctQ, 1000000u);
}

TEST_F(KosyncClientTest, RichPositionFloorsPageCountAtOne) {
  EXPECT_EQ(SmartSync::buildRichPosition(0.5f, 3, 2, 0, std::nullopt, "").totalPages, 1);
  EXPECT_EQ(SmartSync::buildRichPosition(0.5f, 3, 2, -4, std::nullopt, "").totalPages, 1);
  EXPECT_EQ(SmartSync::buildRichPosition(0.5f, 3, 2, 12, std::nullopt, "").totalPages, 12);
}

TEST_F(KosyncClientTest, RichPositionCarriesSpinePageParagraphAndXpath) {
  const auto pos = SmartSync::buildRichPosition(0.5f, 3, 2, 12, uint16_t{7}, "/body/DocFragment[4]/body/p[7]");
  EXPECT_EQ(pos.spineIndex, 3);
  EXPECT_EQ(pos.pageNumber, 2);
  ASSERT_TRUE(pos.paragraphIndex.has_value());
  EXPECT_EQ(*pos.paragraphIndex, 7);
  EXPECT_EQ(pos.xpath, "/body/DocFragment[4]/body/p[7]");
  const auto bare = SmartSync::buildRichPosition(0.5f, 3, 2, 12, std::nullopt, std::string(200, 'x'));
  EXPECT_FALSE(bare.paragraphIndex.has_value());
  EXPECT_EQ(bare.xpath.size(), 200u);  // the 120-byte cap is applied by the client, not here
}

TEST_F(KosyncClientTest, ResolveTreatsNotANumberAsRemoteAhead) {
  // fabs(NaN) <= epsilon is false and NaN > 0 is false, so a NaN on either
  // side falls through to APPLY_REMOTE rather than aborting the sync.
  const float nan = std::nanf("");
  EXPECT_EQ(SmartSync::resolve(nan, 0.5f), SmartSync::Resolution::APPLY_REMOTE);
  EXPECT_EQ(SmartSync::resolve(0.5f, nan), SmartSync::Resolution::APPLY_REMOTE);
  EXPECT_EQ(SmartSync::askModeDefaultOption(nan, 0.5f), 0);
}

TEST_F(KosyncClientTest, ResolveHandlesFullRangeAndNegativePercentages) {
  EXPECT_EQ(SmartSync::resolve(1.0f, 1.0f), SmartSync::Resolution::ALREADY_SYNCED);
  EXPECT_EQ(SmartSync::resolve(-0.5f, 0.5f), SmartSync::Resolution::APPLY_REMOTE);
  EXPECT_EQ(SmartSync::resolve(0.5f, -0.5f), SmartSync::Resolution::UPLOAD_LOCAL);
  EXPECT_EQ(SmartSync::resolve(0.0f, 0.0f), SmartSync::Resolution::ALREADY_SYNCED);
}

TEST_F(KosyncClientTest, EpsilonIsExactlyOneTenthOfAPercentagePoint) {
  EXPECT_FLOAT_EQ(SmartSync::SAME_PROGRESS_EPSILON, 0.001f);
  EXPECT_EQ(SmartSync::PCT_Q_SCALE, 1000000u);
}

TEST_F(KosyncClientTest, AlternateProbeComparesIdsByteForByte) {
  EXPECT_TRUE(SmartSync::shouldProbeAlternate("ABC", "abc"));  // case-sensitive
  EXPECT_FALSE(SmartSync::shouldProbeAlternate("", ""));
}

TEST_F(KosyncClientTest, RichPositionTruncatesIndicesToSixteenBits) {
  const auto pos = SmartSync::buildRichPosition(0.5f, 65536 + 3, 65536 + 5, 65536 + 7, std::nullopt, "");
  EXPECT_EQ(pos.spineIndex, 3);      // wraps: no range check (T144 owns validation)
  EXPECT_EQ(pos.pageNumber, 5);
  EXPECT_EQ(pos.totalPages, 7);
}

TEST_F(KosyncClientTest, RichPositionQuantizationRoundsRatherThanTruncates) {
  // pctQ = trunc(pct * 1e6 + 0.5): 0.4 of a quantum rounds down, 0.6 rounds up.
  EXPECT_EQ(SmartSync::buildRichPosition(0.0000004f, 0, 0, 1, std::nullopt, "").pctQ, 0u);
  EXPECT_EQ(SmartSync::buildRichPosition(0.0000006f, 0, 0, 1, std::nullopt, "").pctQ, 1u);
  EXPECT_EQ(SmartSync::buildRichPosition(0.9999999f, 0, 0, 1, std::nullopt, "").pctQ, 1000000u);
}

TEST_F(KosyncClientTest, RichPositionKeepsParagraphIndexZeroDistinctFromUnset) {
  const auto zero = SmartSync::buildRichPosition(0.5f, 0, 0, 1, uint16_t{0}, "");
  ASSERT_TRUE(zero.paragraphIndex.has_value());
  EXPECT_EQ(*zero.paragraphIndex, 0);
  // updateProgress serialises it, but getProgress folds a 0 back to unset.
  EXPECT_FALSE(SmartSync::buildRichPosition(0.5f, 0, 0, 1, std::nullopt, "").paragraphIndex.has_value());
}

// --- Settings URL helpers ------------------------------------------------------

TEST_F(KosyncClientTest, ServerUrlPrefillOffersHttpsWhenUnset) {
  EXPECT_EQ(KOReaderServerUrl::prefillForEntry(""), "https://");
  EXPECT_EQ(KOReaderServerUrl::prefillForEntry("sync.local"), "sync.local");
  EXPECT_EQ(KOReaderServerUrl::prefillForEntry("http://sync.local/"), "http://sync.local/");
}

TEST_F(KosyncClientTest, ServerUrlBareSchemeMeansDefault) {
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("https://"), "");
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("http://"), "");
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered(""), "");
}

TEST_F(KosyncClientTest, ServerUrlRealEntriesAreKeptVerbatim) {
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("https://sync.local"), "https://sync.local");
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("sync.local:8080/"), "sync.local:8080/");
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("HTTPS://"), "HTTPS://");  // case-sensitive match
  EXPECT_EQ(KOReaderServerUrl::normalizeEntered("https:// "), "https:// ");
}

TEST_F(KosyncClientTest, ServerUrlDisplayStripsScheme) {
  EXPECT_EQ(KOReaderServerUrl::stripScheme(DEFAULT_BASE), "sync.crosspointreader.com");
  EXPECT_EQ(KOReaderServerUrl::stripScheme("http://host:8080/path"), "host:8080/path");
  EXPECT_EQ(KOReaderServerUrl::stripScheme("host"), "host");
  EXPECT_EQ(KOReaderServerUrl::stripScheme("https://a://b"), "a://b");  // only the first scheme
  EXPECT_EQ(KOReaderServerUrl::stripScheme(""), "");
}

// --- ProgressMapper::generateXPath byte fallback --------------------------------

namespace {

// Bare "&" keeps expat from parsing, so the codepoint-exact resolver yields
// nothing and toSavedProgress reaches generateXPath's byte-count fallback.
// Layout: <html><body> = 12 bytes, p[1] opens at 12, p[2] at 22, p[3] at 38,
// 62 bytes in total.
constexpr char MALFORMED_THREE_P[] = "<html><body><p>abc</p><p>def & raw</p><p>ghi</p></body></html>";

std::shared_ptr<Epub> makeBook(const std::string& chapter, const int spineCount = 1) {
  auto epub = std::make_shared<Epub>();
  for (int i = 0; i < spineCount; i++) epub->addSpineItem("ch" + std::to_string(i) + ".xhtml", chapter, 100);
  return epub;
}

CrossPointPosition pageOf(const int spine, const int page, const int totalPages) {
  CrossPointPosition pos{};
  pos.spineIndex = spine;
  pos.pageNumber = page;
  pos.totalPages = totalPages;
  return pos;
}

}  // namespace

TEST_F(KosyncClientTest, ByteFallbackFirstPageIsChapterStart) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 0, 4)).xpath, "/body/DocFragment[1]/body");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 0, 1)).xpath, "/body/DocFragment[1]/body");
}

TEST_F(KosyncClientTest, ByteFallbackCountsParagraphsOpenedBeforeTargetByte) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  // page 1 of 3 -> intra 0.5 -> target byte 31: p[1] and p[2] have opened.
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 3)).xpath, "/body/DocFragment[1]/body/p[2]");
  // page 1 of 5 -> intra 0.25 -> target byte 15: only p[1] has opened.
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 5)).xpath, "/body/DocFragment[1]/body/p[1]");
}

TEST_F(KosyncClientTest, ByteFallbackLastPageCountsEveryParagraph) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 2, 3)).xpath, "/body/DocFragment[1]/body/p[3]");
}

TEST_F(KosyncClientTest, ByteFallbackUsesOneBasedDocFragment) {
  const auto epub = makeBook(MALFORMED_THREE_P, 3);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(2, 2, 3)).xpath, "/body/DocFragment[3]/body/p[3]");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(1, 0, 3)).xpath, "/body/DocFragment[2]/body");
}

TEST_F(KosyncClientTest, ByteFallbackWithoutParagraphsIsChapterStart) {
  const auto epub = makeBook("<html><body><div>text & more text</div></body></html>");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 2)).xpath, "/body/DocFragment[1]/body");
}

TEST_F(KosyncClientTest, ByteFallbackIgnoresParagraphsOutsideBody) {
  const auto epub = makeBook("<html><head><p>x</p><p>y</p></head><body><p>a</p><p>b & c</p></body></html>");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 2)).xpath, "/body/DocFragment[1]/body/p[2]");
}

TEST_F(KosyncClientTest, ByteFallbackEmptyChapterIsChapterStart) {
  const auto epub = makeBook("");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 2)).xpath, "/body/DocFragment[1]/body");
}

TEST_F(KosyncClientTest, ByteFallbackStreamFailureIsChapterStart) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  epub->failStreaming = true;
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 2)).xpath, "/body/DocFragment[1]/body");
}

TEST_F(KosyncClientTest, ByteFallbackOutOfRangeSpineIsChapterStart) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(5, 1, 2)).xpath, "/body/DocFragment[6]/body");
}

TEST_F(KosyncClientTest, ByteFallbackClampsPagesPastTheEndToTheWholeChapter) {
  // page 5 of 3 -> intra 2.5, clamped to 1.0: every paragraph is counted.
  const auto epub = makeBook(MALFORMED_THREE_P);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 5, 3)).xpath, "/body/DocFragment[1]/body/p[3]");
}

TEST_F(KosyncClientTest, ByteFallbackCountsEveryParagraphTagIncludingUnclosed) {
  const auto epub = makeBook("<html><body><p>a & b<p>c<p>d</body></html>");
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, 1, 2)).xpath, "/body/DocFragment[1]/body/p[3]");
}

TEST_F(KosyncClientTest, ByteFallbackNegativePageIsChapterStart) {
  const auto epub = makeBook(MALFORMED_THREE_P);
  EXPECT_EQ(ProgressMapper::toSavedProgress(epub, pageOf(0, -2, 3)).xpath, "/body/DocFragment[1]/body");
}

TEST_F(KosyncClientTest, ByteFallbackReportsPercentageAlongsideTheXpath) {
  const auto epub = makeBook(MALFORMED_THREE_P, 2);  // declaredSize 100 each
  const auto saved = ProgressMapper::toSavedProgress(epub, pageOf(1, 1, 3));
  EXPECT_EQ(saved.xpath, "/body/DocFragment[2]/body/p[2]");
  EXPECT_FLOAT_EQ(saved.percentage, 0.75f);  // one whole chapter plus half of the second
}

// --- ProgressMapper anchor-id attribute scanner -----------------------------------

namespace {

// toCrossPoint resolves the second paragraph and captures the first id seen on
// it or on an <a> inside it; the offset LUT just gives the page lookup a home.
// The xpointer needs the /text()[n].c tail: parseXPathSteps only returns an
// ancestry (and only ancestry mode runs the attribute scanner) when one is
// present -- ProgressMapper.cpp:169-177.
std::string anchorIdFor(const std::string& secondParagraph, GfxRenderer& renderer) {
  auto epub = std::make_shared<Epub>();
  epub->addSpineItem("ch1.xhtml", "<html><body><p>0123456789</p>" + secondParagraph + "</body></html>", 100);
  auto& lut = SectionStubRegistry::forSpine(0);
  lut.cachedPageCount = 2;
  lut.pageStartOffsets = {0, 10};
  const SavedProgressPosition ko{"/body/DocFragment[1]/body/p[2]/text()[1].0", 0.5f};
  const auto pos = ProgressMapper::toCrossPoint(epub, ko, renderer);
  EXPECT_TRUE(pos.hasVisibleTextOffset) << secondParagraph;
  EXPECT_EQ(pos.pageNumber, 1) << secondParagraph;
  return pos.xpathAnchorId;
}

}  // namespace

TEST_F(KosyncClientTest, AnchorScannerAcceptsSingleQuotedId) {
  EXPECT_EQ(anchorIdFor("<p id='second'>abcdefghij</p>", renderer), "second");
}

TEST_F(KosyncClientTest, AnchorScannerFindsIdAfterOtherAttributes) {
  EXPECT_EQ(anchorIdFor("<p class=\"x\" lang='en' id=\"second\">abcdefghij</p>", renderer), "second");
}

TEST_F(KosyncClientTest, AnchorScannerToleratesWhitespaceAroundEquals) {
  EXPECT_EQ(anchorIdFor("<p id = \"second\">abcdefghij</p>", renderer), "second");
  EXPECT_EQ(anchorIdFor("<p\n  id\t=\n'second'>abcdefghij</p>", renderer), "second");
}

TEST_F(KosyncClientTest, AnchorScannerIgnoresAttributesThatMerelyStartWithId) {
  EXPECT_EQ(anchorIdFor("<p idx=\"no\" identity=\"no\">abcdefghij</p>", renderer), "");
}

TEST_F(KosyncClientTest, AnchorScannerIgnoresPrefixedAndSuffixedIdNames) {
  EXPECT_EQ(anchorIdFor("<p xml:id=\"no\" data-id=\"no\" xid=\"no\">abcdefghij</p>", renderer), "");
}

TEST_F(KosyncClientTest, AnchorScannerIsCaseSensitive) {
  EXPECT_EQ(anchorIdFor("<p ID=\"no\" Id='no'>abcdefghij</p>", renderer), "");
}

TEST_F(KosyncClientTest, AnchorScannerKeepsSlashInsideQuotedValue) {
  EXPECT_EQ(anchorIdFor("<p title=\"a/b\" id=\"second\">abcdefghij</p>", renderer), "second");
  EXPECT_EQ(anchorIdFor("<p id=\"sec/ond\">abcdefghij</p>", renderer), "sec/ond");
}

TEST_F(KosyncClientTest, AnchorScannerCapturesChildAnchorWhenElementHasNoId) {
  EXPECT_EQ(anchorIdFor("<p><a id=\"child\">abc</a>defghij</p>", renderer), "child");
  EXPECT_EQ(anchorIdFor("<p><a id='child'/>abcdefghij</p>", renderer), "child");
}

TEST_F(KosyncClientTest, AnchorScannerFirstIdWins) {
  EXPECT_EQ(anchorIdFor("<p id=\"own\"><a id=\"child\">abc</a>defghij</p>", renderer), "own");
  EXPECT_EQ(anchorIdFor("<p><a id=\"one\"/>abc<a id=\"two\"/>defghij</p>", renderer), "one");
}

TEST_F(KosyncClientTest, AnchorScannerIgnoresNonAnchorChildren) {
  EXPECT_EQ(anchorIdFor("<p><span id=\"no\">abc</span>defghij</p>", renderer), "");
}

TEST_F(KosyncClientTest, AnchorScannerDiscardsUnterminatedValue) {
  EXPECT_EQ(anchorIdFor("<p id=\"broken>abcdefghij</p>", renderer), "");
}

TEST_F(KosyncClientTest, AnchorScannerTruncatesTo63Bytes) {
  const std::string longId(80, 'z');
  EXPECT_EQ(anchorIdFor("<p id=\"" + longId + "\">abcdefghij</p>", renderer), std::string(63, 'z'));
}

TEST_F(KosyncClientTest, AnchorScannerKeepsEmptyValueEmpty) {
  EXPECT_EQ(anchorIdFor("<p id=\"\"><a id=\"child\">abc</a>defghij</p>", renderer), "child");
}
