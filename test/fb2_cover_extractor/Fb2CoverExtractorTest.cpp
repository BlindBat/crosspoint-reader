#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "Fb2/Fb2CoverExtractor.h"
#include "Fb2TestSupport.h"
#include "JpegToBmpConverter.h"

namespace {

using fb2test::fileExists;
using fb2test::fixturePath;
using fb2test::readAll;
using fb2test::writeAll;

// Independent base64 encoder (RFC 4648, no line wrapping) so the expected
// bytes never come from the code under test.
std::string base64Encode(const std::string& data) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  while (i + 2 < data.size()) {
    const uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                       static_cast<uint8_t>(data[i + 2]);
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
    out += alphabet[n & 63];
    i += 3;
  }
  if (i + 1 == data.size()) {
    const uint32_t n = static_cast<uint8_t>(data[i]) << 16;
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += "==";
  } else if (i + 2 == data.size()) {
    const uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
    out += '=';
  }
  return out;
}

std::string minimalFb2WithBinary(const std::string& binaryId, const std::string& base64Text) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">\n"
         "  <body><section><p>text</p></section></body>\n"
         "  <binary id=\"" +
         binaryId + "\" content-type=\"image/jpeg\">" + base64Text +
         "</binary>\n"
         "</FictionBook>\n";
}

class Fb2CoverExtractorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp.valid());
    JpegToBmpConverterStubState::instance().reset();
  }

  std::string outPath(const char* name = "cover.bmp") const { return tmp.path() + "/" + name; }

  fb2test::TempDir tmp;
};

TEST_F(Fb2CoverExtractorTest, DecodesSingleLineBase64ByteForByte) {
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "cover.jpg", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(readAll(outPath()), "STUBJPEG:basic-cover-payload-0123456789");
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 1);
  // The intermediate .cover.jpg must not be left behind.
  EXPECT_FALSE(fileExists(tmp.path() + "/.cover.jpg"));
}

TEST_F(Fb2CoverExtractorTest, DecodesWrappedMultilineBase64WithWhitespace) {
  // target.png's base64 is wrapped at 16 chars per line with leading spaces;
  // a decoy binary precedes it to prove matching is by id, not first-binary.
  Fb2CoverExtractor extractor(fixturePath("cover-wrapped.fb2"), "target.png", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(readAll(outPath()), "STUBPNG:wrapped-cover-payload-ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

TEST_F(Fb2CoverExtractorTest, MatchesBinaryByIdNotDocumentOrder) {
  Fb2CoverExtractor extractor(fixturePath("cover-wrapped.fb2"), "decoy.png", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(readAll(outPath()), "DECOY-BINARY-MUST-NOT-BE-EXTRACTED");
}

// Documents current behavior: invalid base64 characters are silently skipped,
// so a corrupted binary "extracts" successfully with garbage bytes instead of
// failing. Expected bytes below are the streaming decode of the fixture's
// cleaned character sequence "R09PREREFUQQ==".
TEST_F(Fb2CoverExtractorTest, InvalidBase64CharsAreSilentlySkipped) {
  Fb2CoverExtractor extractor(fixturePath("base64-corrupt-cover.fb2"), "bad.jpg", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(readAll(outPath()), std::string("GOODDD\x15\x44\x10", 9));
}

TEST_F(Fb2CoverExtractorTest, MissingBinaryIdFailsAndLeavesNoFiles) {
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "no-such-binary", outPath());
  EXPECT_FALSE(extractor.extract());
  EXPECT_FALSE(fileExists(outPath()));
  EXPECT_FALSE(fileExists(tmp.path() + "/.cover.jpg"));
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 0);
}

// Documents current behavior: the extractor expects the id with the '#'
// already stripped (the metadata parser does that); a raw "#id" never matches.
TEST_F(Fb2CoverExtractorTest, IdWithLeadingHashDoesNotMatch) {
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "#cover.jpg", outPath());
  EXPECT_FALSE(extractor.extract());
}

TEST_F(Fb2CoverExtractorTest, IdMatchIsExactNotPrefix) {
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "cover", outPath());
  EXPECT_FALSE(extractor.extract());
}

// Documents current behavior: content-type is ignored; PNG binaries are
// routed through the JPEG converter all the same.
TEST_F(Fb2CoverExtractorTest, PngContentTypeStillUsesJpegConverter) {
  Fb2CoverExtractor extractor(fixturePath("cover-wrapped.fb2"), "target.png", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(JpegToBmpConverterStubState::instance().streamCalls, 1);
}

TEST_F(Fb2CoverExtractorTest, LargeBinaryStreamsAcrossParserBufferBoundaries) {
  // 5000 pseudo-random bytes -> ~6.7KB of base64, far beyond the parser's
  // 1KB read buffer, so decoding must survive arbitrary chunk splits.
  std::string payload;
  payload.reserve(5000);
  uint32_t seed = 0x12345678;
  for (int i = 0; i < 5000; i++) {
    seed = seed * 1664525u + 1013904223u;
    payload += static_cast<char>((seed >> 16) & 0xFF);
  }
  const std::string fb2Path = tmp.path() + "/large.fb2";
  ASSERT_TRUE(writeAll(fb2Path, minimalFb2WithBinary("big.jpg", base64Encode(payload))));

  Fb2CoverExtractor extractor(fb2Path, "big.jpg", outPath());
  ASSERT_TRUE(extractor.extract());
  EXPECT_EQ(readAll(outPath()), payload);
  EXPECT_EQ(JpegToBmpConverterStubState::instance().lastInputSize, payload.size());
}

TEST_F(Fb2CoverExtractorTest, PaddingCarriesPartialGroupBytes) {
  // "QQ==" -> one byte 'A'; "QUI=" -> two bytes "AB". Exercises both '='
  // flush branches of the streaming decoder.
  const std::string oneBytePath = tmp.path() + "/one.fb2";
  ASSERT_TRUE(writeAll(oneBytePath, minimalFb2WithBinary("x", "QQ==")));
  Fb2CoverExtractor one(oneBytePath, "x", outPath("one.bmp"));
  ASSERT_TRUE(one.extract());
  EXPECT_EQ(readAll(outPath("one.bmp")), "A");

  const std::string twoBytePath = tmp.path() + "/two.fb2";
  ASSERT_TRUE(writeAll(twoBytePath, minimalFb2WithBinary("x", "QUI=")));
  Fb2CoverExtractor two(twoBytePath, "x", outPath("two.bmp"));
  ASSERT_TRUE(two.extract());
  EXPECT_EQ(readAll(outPath("two.bmp")), "AB");
}

TEST_F(Fb2CoverExtractorTest, ConverterFailureRemovesOutputBmp) {
  JpegToBmpConverterStubState::instance().failNextConversion = true;
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "cover.jpg", outPath());
  EXPECT_FALSE(extractor.extract());
  EXPECT_FALSE(fileExists(outPath()));
}

TEST_F(Fb2CoverExtractorTest, ThumbRequestsSixTenthsAspectAndOneBitConverter) {
  Fb2CoverExtractor extractor(fixturePath("basic.fb2"), "cover.jpg", "");
  const std::string thumbPath = outPath("thumb_100.bmp");
  ASSERT_TRUE(extractor.extractThumb(thumbPath, 100));
  const auto& state = JpegToBmpConverterStubState::instance();
  EXPECT_EQ(state.oneBitCalls, 1);
  EXPECT_EQ(state.lastTargetMaxWidth, 60);  // height * 0.6
  EXPECT_EQ(state.lastTargetMaxHeight, 100);
  EXPECT_EQ(readAll(thumbPath), "STUBJPEG:basic-cover-payload-0123456789");
}

TEST_F(Fb2CoverExtractorTest, MissingSourceFileFailsExtract) {
  Fb2CoverExtractor extractor("/nonexistent/book.fb2", "cover.jpg", outPath());
  EXPECT_FALSE(extractor.extract());
}

}  // namespace
