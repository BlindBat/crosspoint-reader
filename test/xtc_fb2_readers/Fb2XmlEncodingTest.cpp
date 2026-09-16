#include <expat.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "Fb2/Fb2XmlEncoding.h"

// The single-byte codepage tables behind fb2RegisterExtraEncodings (FR-103):
// windows-1251/1252 bytes must map to the right code points, unassigned bytes
// must be rejected, and unknown encodings must still fail. Higher-level
// decoding through the FB2 parsers is covered by fb2_metadata_parser.

namespace {

struct Parsed {
  bool ok = false;
  XML_Error error = XML_ERROR_NONE;
  std::string text;
};

Parsed parseWith(const std::string& encoding, const std::string& body) {
  Parsed out;
  XML_Parser parser = XML_ParserCreate(nullptr);
  fb2RegisterExtraEncodings(parser);
  XML_SetUserData(parser, &out.text);
  XML_SetCharacterDataHandler(parser, [](void* ud, const XML_Char* s, int len) {
    static_cast<std::string*>(ud)->append(s, static_cast<size_t>(len));
  });
  const std::string doc = "<?xml version=\"1.0\" encoding=\"" + encoding + "\"?><r>" + body + "</r>";
  out.ok = XML_Parse(parser, doc.data(), static_cast<int>(doc.size()), 1) == XML_STATUS_OK;
  out.error = XML_GetErrorCode(parser);
  XML_ParserFree(parser);
  return out;
}

std::vector<uint32_t> codepoints(const std::string& utf8) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < utf8.size();) {
    const uint8_t b = static_cast<uint8_t>(utf8[i]);
    uint32_t cp;
    size_t len;
    if (b < 0x80) {
      cp = b;
      len = 1;
    } else if ((b & 0xE0) == 0xC0) {
      cp = b & 0x1F;
      len = 2;
    } else if ((b & 0xF0) == 0xE0) {
      cp = b & 0x0F;
      len = 3;
    } else {
      cp = b & 0x07;
      len = 4;
    }
    for (size_t k = 1; k < len && i + k < utf8.size(); ++k) cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3F);
    out.push_back(cp);
    i += len;
  }
  return out;
}

// Single-byte body from a list of byte values.
std::string bytes(std::initializer_list<int> values) {
  std::string s;
  for (const int v : values) s.push_back(static_cast<char>(v));
  return s;
}

}  // namespace

TEST(Fb2XmlEncoding, Cp1251CyrillicBlockIsContiguous) {
  std::string body;
  for (int b = 0xC0; b <= 0xFF; ++b) body.push_back(static_cast<char>(b));
  const auto r = parseWith("windows-1251", body);
  ASSERT_TRUE(r.ok) << XML_ErrorString(r.error);
  const auto cps = codepoints(r.text);
  ASSERT_EQ(cps.size(), 64u);
  for (int i = 0; i < 64; ++i) EXPECT_EQ(cps[i], 0x0410u + i) << "byte " << (0xC0 + i);
}

TEST(Fb2XmlEncoding, Cp1251PunctuationAndYoAreMapped) {
  const auto r = parseWith("windows-1251", bytes({0xA8, 0xB8, 0xB9, 0x88, 0x80, 0x96, 0xA0}));
  ASSERT_TRUE(r.ok) << XML_ErrorString(r.error);
  EXPECT_EQ(codepoints(r.text), (std::vector<uint32_t>{0x0401, 0x0451, 0x2116, 0x20AC, 0x0402, 0x2013, 0x00A0}));
}

TEST(Fb2XmlEncoding, Cp1251AsciiHalfIsIdentity) {
  const auto r = parseWith("cp1251", "Hello 123");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.text, "Hello 123");
}

TEST(Fb2XmlEncoding, Cp1251UnassignedByteIsRejected) {
  const auto r = parseWith("windows-1251", bytes({0x98}));
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error, XML_ERROR_UNKNOWN_ENCODING);  // the encoding was accepted; the byte was not
}

TEST(Fb2XmlEncoding, Cp1252C1RangeAndLatin1IdentityAreMapped) {
  const auto r = parseWith("windows-1252", bytes({0x80, 0x99, 0x9F, 0x8A, 0xA0, 0xE9, 0xFF}));
  ASSERT_TRUE(r.ok) << XML_ErrorString(r.error);
  EXPECT_EQ(codepoints(r.text), (std::vector<uint32_t>{0x20AC, 0x2122, 0x0178, 0x0160, 0x00A0, 0x00E9, 0x00FF}));
}

TEST(Fb2XmlEncoding, Cp1252UnassignedBytesAreRejected) {
  for (const int b : {0x81, 0x8D, 0x8F, 0x90, 0x9D}) {
    const auto r = parseWith("cp1252", bytes({b}));
    EXPECT_FALSE(r.ok) << "byte " << b;
    EXPECT_NE(r.error, XML_ERROR_UNKNOWN_ENCODING) << "byte " << b;
  }
}

TEST(Fb2XmlEncoding, NamesAreCaseInsensitiveWithCpAliases) {
  for (const char* name : {"Windows-1251", "WINDOWS-1251", "CP1251", "Cp1251"}) {
    const auto r = parseWith(name, bytes({0xB8}));
    ASSERT_TRUE(r.ok) << name << ": " << XML_ErrorString(r.error);
    EXPECT_EQ(codepoints(r.text), (std::vector<uint32_t>{0x0451})) << name;
  }
  for (const char* name : {"Windows-1252", "CP1252"}) {
    const auto r = parseWith(name, bytes({0x80}));
    ASSERT_TRUE(r.ok) << name;
    EXPECT_EQ(codepoints(r.text), (std::vector<uint32_t>{0x20AC})) << name;
  }
}

TEST(Fb2XmlEncoding, PrefixOrSuffixVariantsAreNotAccepted) {
  EXPECT_EQ(parseWith("windows-12510", "a").error, XML_ERROR_UNKNOWN_ENCODING);
  EXPECT_EQ(parseWith("windows-125", "a").error, XML_ERROR_UNKNOWN_ENCODING);
}

TEST(Fb2XmlEncoding, UnknownEncodingStillFails) {
  const auto r = parseWith("koi8-r", "a");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, XML_ERROR_UNKNOWN_ENCODING);
}

TEST(Fb2XmlEncoding, Cp1251TableMatchesTheReferenceCodepage) {
  // Independent windows-1251 table; -1 marks the one unassigned byte (0x98).
  static const int kRef[128] = {
      0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
      0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, -1,     0x2122,
      0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
      0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
      0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
      0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
      0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
      0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
      0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
      0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};
  std::string body;
  std::vector<uint32_t> expected;
  for (int i = 0; i < 128; ++i) {
    if (kRef[i] < 0) continue;
    body.push_back(static_cast<char>(0x80 + i));
    expected.push_back(static_cast<uint32_t>(kRef[i]));
  }
  const auto r = parseWith("windows-1251", body);
  ASSERT_TRUE(r.ok) << XML_ErrorString(r.error);
  EXPECT_EQ(codepoints(r.text), expected);
}

TEST(Fb2XmlEncoding, Cp1252TableMatchesTheReferenceCodepage) {
  static const int kRef[32] = {0x20AC, -1,     0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                               0x2039, 0x0152, -1,     0x017D, -1,     -1,     0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                               0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, -1,     0x017E, 0x0178};
  std::string body;
  std::vector<uint32_t> expected;
  for (int i = 0; i < 32; ++i) {
    if (kRef[i] < 0) continue;
    body.push_back(static_cast<char>(0x80 + i));
    expected.push_back(static_cast<uint32_t>(kRef[i]));
  }
  for (int b = 0xA0; b <= 0xFF; ++b) {  // Latin-1 identity half
    body.push_back(static_cast<char>(b));
    expected.push_back(static_cast<uint32_t>(b));
  }
  const auto r = parseWith("windows-1252", body);
  ASSERT_TRUE(r.ok) << XML_ErrorString(r.error);
  EXPECT_EQ(codepoints(r.text), expected);
}

TEST(Fb2XmlEncoding, WithoutTheHandlerTheEncodingIsUnknown) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  const std::string doc = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><r>a</r>";
  EXPECT_EQ(XML_Parse(parser, doc.data(), static_cast<int>(doc.size()), 1), XML_STATUS_ERROR);
  EXPECT_EQ(XML_GetErrorCode(parser), XML_ERROR_UNKNOWN_ENCODING);
  XML_ParserFree(parser);
}

TEST(Fb2XmlEncoding, Utf8AndAsciiDocumentsStillParseWithTheHandlerInstalled) {
  const auto utf8 = parseWith("UTF-8", "caf\xC3\xA9");
  ASSERT_TRUE(utf8.ok) << XML_ErrorString(utf8.error);
  EXPECT_EQ(codepoints(utf8.text), (std::vector<uint32_t>{'c', 'a', 'f', 0x00E9}));
  const auto ascii = parseWith("US-ASCII", "plain");
  ASSERT_TRUE(ascii.ok);
  EXPECT_EQ(ascii.text, "plain");
}
