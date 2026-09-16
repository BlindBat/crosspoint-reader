// Host tests for the fonts.json manifest parser extracted from
// FontDownloadActivity (src/util/FontManifest.cpp) — FR-120.
//
// The manifest is untrusted input fetched over the network, so alongside the
// happy path this covers non-JSON, a non-object root, hostile nesting,
// truncation, missing and mistyped fields, absurd numbers, duplicate family
// names and oversized arrays. Behaviour is pinned as it stands: hardening the
// parser further is a separate task.

#include <ArduinoJson.h>
#include <FontInstaller.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "util/FontManifest.h"

namespace {

struct Parsed {
  DeserializationError jsonError;
  FontManifestError result = FontManifestError::OK;
  std::string baseUrl;
  std::vector<std::string> labels;
  std::vector<FontManifestFamily> families;
};

// Deserialize then parse, keeping both error channels visible.
Parsed parse(const std::string& json) {
  Parsed out;
  JsonDocument doc;
  out.jsonError = deserializeJson(doc, json);
  // Pre-populate so the "containers are cleared" contract is exercised on
  // every call, not just the one test that names it.
  out.baseUrl = "stale";
  out.labels.emplace_back("stale");
  out.families.emplace_back();
  out.result = parseFontManifest(doc, out.baseUrl, out.labels, out.families);
  return out;
}

const char* const MINIMAL = R"({"version":1,"baseUrl":"https://example.test/fonts/","families":[]})";

std::string manifestWith(const std::string& body) { return std::string("{\"version\":1,") + body + "}"; }

}  // namespace

// --- Happy path ---

TEST(FontManifest, ParsesAWellFormedManifest) {
  const Parsed p = parse(R"({
    "version": 1,
    "baseUrl": "https://example.test/f/",
    "scriptGroups": [{"tag":"latin","label":"Latin"},{"tag":"cjk","label":"CJK"}],
    "families": [
      {"name":"Alpha","description":"A serif","styles":["Regular","Bold"],"scripts":["latin"],
       "files":[{"name":"Alpha_12.cpfont","size":100,"crc32":1},
                {"name":"Alpha_14.cpfont","size":250,"crc32":2}]},
      {"name":"Beta","description":"","styles":[],"scripts":["latin","cjk"],
       "files":[{"name":"Beta_14.cpfont","size":7,"crc32":4294967295}]}
    ]
  })");

  ASSERT_FALSE(p.jsonError);
  ASSERT_EQ(p.result, FontManifestError::OK);
  EXPECT_EQ(p.baseUrl, "https://example.test/f/");
  EXPECT_EQ(p.labels, (std::vector<std::string>{"Latin", "CJK"}));
  ASSERT_EQ(p.families.size(), 2u);

  EXPECT_EQ(p.families[0].name, "Alpha");
  EXPECT_EQ(p.families[0].description, "A serif");
  EXPECT_EQ(p.families[0].styles, (std::vector<std::string>{"Regular", "Bold"}));
  EXPECT_EQ(p.families[0].scriptMask, 0x1u);
  EXPECT_EQ(p.families[0].totalSize, 350u);
  ASSERT_EQ(p.families[0].files.size(), 2u);
  EXPECT_EQ(p.families[0].files[1].name, "Alpha_14.cpfont");
  EXPECT_EQ(p.families[0].files[1].size, 250u);
  EXPECT_EQ(p.families[0].files[1].crc32, 2u);
  // installed/hasUpdate are the caller's business, not the parser's.
  EXPECT_FALSE(p.families[0].installed);
  EXPECT_FALSE(p.families[0].hasUpdate);

  EXPECT_EQ(p.families[1].scriptMask, 0x3u);
  EXPECT_EQ(p.families[1].files[0].crc32, 4294967295u);
  EXPECT_TRUE(p.families[1].styles.empty());
}

TEST(FontManifest, OutputContainersAreClearedBeforeBeingRefilled) {
  const Parsed p = parse(MINIMAL);

  ASSERT_EQ(p.result, FontManifestError::OK);
  EXPECT_EQ(p.baseUrl, "https://example.test/fonts/");
  EXPECT_TRUE(p.labels.empty());
  EXPECT_TRUE(p.families.empty());
}

TEST(FontManifest, OptionalTopLevelFieldsDefaultToEmpty) {
  const Parsed p = parse(R"({"version":1})");

  ASSERT_EQ(p.result, FontManifestError::OK);
  EXPECT_EQ(p.baseUrl, "");
  EXPECT_TRUE(p.labels.empty());
  EXPECT_TRUE(p.families.empty());
}

TEST(FontManifest, OptionalFamilyFieldsDefaultToEmpty) {
  const Parsed p = parse(manifestWith(R"("families":[{}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 1u);
  EXPECT_EQ(p.families[0].name, "");
  EXPECT_EQ(p.families[0].description, "");
  EXPECT_TRUE(p.families[0].styles.empty());
  EXPECT_TRUE(p.families[0].files.empty());
  EXPECT_EQ(p.families[0].totalSize, 0u);
  EXPECT_EQ(p.families[0].scriptMask, 0u);
}

// --- Version gate ---

TEST(FontManifest, OnlyVersionOneIsAccepted) {
  EXPECT_EQ(parse(R"({"version":0,"families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
  EXPECT_EQ(parse(R"({"version":2,"families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
  EXPECT_EQ(parse(R"({"version":-1,"families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
  EXPECT_EQ(parse(R"({"families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
  // A stringly-typed version is not an integer 1.
  EXPECT_EQ(parse(R"({"version":"1","families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
  EXPECT_EQ(parse(R"({"version":1.5,"families":[]})").result, FontManifestError::UNSUPPORTED_VERSION);
}

// --- Malformed and hostile input ---

TEST(FontManifest, NonJsonInputFailsDeserializationAndThenTheVersionGate) {
  for (const char* junk : {"", "not json at all", "{", "{\"version\":", "\xFF\xFE\x00\x01"}) {
    const Parsed p = parse(junk);
    EXPECT_TRUE(p.jsonError) << junk;
    EXPECT_EQ(p.result, FontManifestError::UNSUPPORTED_VERSION) << junk;
  }
}

TEST(FontManifest, ANonObjectRootIsRejected) {
  for (const char* root : {"[]", "[1,2,3]", "\"hello\"", "123", "true", "null"}) {
    EXPECT_EQ(parse(root).result, FontManifestError::UNSUPPORTED_VERSION) << root;
  }
}

TEST(FontManifest, TruncatedManifestsAreRejected) {
  const std::string full = R"({"version":1,"baseUrl":"u","scriptGroups":[{"tag":"t","label":"L"}],)"
                           R"("families":[{"name":"A","files":[{"name":"f","size":1,"crc32":2}]}]})";
  // Every proper prefix must fail deserialization rather than yielding a
  // half-built manifest.
  for (size_t cut = 1; cut < full.size(); cut++) {
    const Parsed p = parse(full.substr(0, cut));
    ASSERT_TRUE(p.jsonError) << "prefix of length " << cut;
  }
  ASSERT_FALSE(parse(full).jsonError);
}

TEST(FontManifest, HostileNestingIsRejectedByTheDeserializer) {
  std::string deep = R"({"version":1,"families":)";
  constexpr int DEPTH = 200;
  for (int i = 0; i < DEPTH; i++) deep += "[";
  for (int i = 0; i < DEPTH; i++) deep += "]";
  deep += "}";

  const Parsed p = parse(deep);

  // The deserializer stops at its nesting limit; FontDownloadActivity aborts on
  // that error and never calls the parser. Run anyway, the partially built
  // document yields only empty, unusable family records — no recursion, no
  // unbounded allocation.
  EXPECT_EQ(p.jsonError.code(), DeserializationError::TooDeep);
  EXPECT_EQ(p.result, FontManifestError::OK);
  for (const auto& family : p.families) {
    EXPECT_TRUE(family.name.empty());
    EXPECT_TRUE(family.files.empty());
    EXPECT_EQ(family.totalSize, 0u);
  }
}

// --- Script groups ---

TEST(FontManifest, ScriptGroupWithoutATagOrLabelIsMalformed) {
  EXPECT_EQ(parse(manifestWith(R"("scriptGroups":[{"label":"L"}],"families":[])")).result,
            FontManifestError::MALFORMED);
  EXPECT_EQ(parse(manifestWith(R"("scriptGroups":[{"tag":"t"}],"families":[])")).result, FontManifestError::MALFORMED);
  EXPECT_EQ(parse(manifestWith(R"("scriptGroups":[{"tag":"","label":""}],"families":[])")).result,
            FontManifestError::MALFORMED);
  EXPECT_EQ(parse(manifestWith(R"("scriptGroups":[{"tag":"a","label":"A"},{}],"families":[])")).result,
            FontManifestError::MALFORMED);
  // Non-string tag/label read as empty and are rejected the same way.
  EXPECT_EQ(parse(manifestWith(R"("scriptGroups":[{"tag":7,"label":"A"}],"families":[])")).result,
            FontManifestError::MALFORMED);
}

TEST(FontManifest, ScriptGroupsBeyondThirtyTwoAreIgnored) {
  std::string groups = R"("scriptGroups":[)";
  for (int i = 0; i < 40; i++) {
    if (i > 0) groups += ",";
    // Groups past the 32-bit mask are never validated either — index 35 is
    // missing its label and must still not fail the parse.
    if (i == 35) {
      groups += R"({"tag":"g35"})";
    } else {
      groups += R"({"tag":"g)" + std::to_string(i) + R"(","label":"L)" + std::to_string(i) + R"("})";
    }
  }
  groups += "]";

  const Parsed p = parse(manifestWith(groups + R"(,"families":[{"name":"A","scripts":["g0","g31","g35"]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  EXPECT_EQ(p.labels.size(), FONT_MANIFEST_MAX_SCRIPT_GROUPS);
  EXPECT_EQ(p.labels.front(), "L0");
  EXPECT_EQ(p.labels.back(), "L31");
  ASSERT_EQ(p.families.size(), 1u);
  EXPECT_EQ(p.families[0].scriptMask, 0x1u | (uint32_t{1} << 31));
}

TEST(FontManifest, UnknownAndNonStringScriptEntriesContributeNoMaskBits) {
  const Parsed p = parse(manifestWith(
      R"("scriptGroups":[{"tag":"latin","label":"Latin"}],)"
      R"("families":[{"name":"A","scripts":["greek",null,7,{"tag":"latin"},["latin"],"latin","latin"]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 1u);
  EXPECT_EQ(p.families[0].scriptMask, 0x1u);  // only the two literal "latin" entries match
}

TEST(FontManifest, ANonArrayScriptGroupsOrFamiliesKeyYieldsEmptyCollections) {
  const Parsed p = parse(manifestWith(R"("scriptGroups":{"tag":"a"},"families":"nope")"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  EXPECT_TRUE(p.labels.empty());
  EXPECT_TRUE(p.families.empty());
}

// --- File entries ---

TEST(FontManifest, MissingOrMistypedCrc32IsMalformed) {
  const auto withFile = [](const std::string& fileObj) {
    return parse(manifestWith(R"("families":[{"name":"A","files":[)" + fileObj + "]}]")).result;
  };

  EXPECT_EQ(withFile(R"({"name":"f","size":1})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":"1"})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":-1})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":1.5})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":null})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":4294967296})"), FontManifestError::MALFORMED);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":0})"), FontManifestError::OK);
  EXPECT_EQ(withFile(R"({"name":"f","size":1,"crc32":4294967295})"), FontManifestError::OK);
}

TEST(FontManifest, AMalformedFileEntryAbortsTheWholeManifest) {
  const Parsed p = parse(manifestWith(
      R"("families":[{"name":"Good","files":[{"name":"a","size":1,"crc32":1}]},)"
      R"({"name":"Bad","files":[{"name":"b","size":1}]},)"
      R"({"name":"Never","files":[{"name":"c","size":1,"crc32":3}]}])"));

  EXPECT_EQ(p.result, FontManifestError::MALFORMED);
}

TEST(FontManifest, FileSizesAccumulateIntoTheFamilyTotal) {
  const Parsed p = parse(manifestWith(
      R"("families":[{"name":"A","files":[{"name":"a","size":10,"crc32":1},)"
      R"({"name":"b","size":20,"crc32":2},{"name":"c","size":30,"crc32":3}]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 1u);
  EXPECT_EQ(p.families[0].files.size(), 3u);
  EXPECT_EQ(p.families[0].totalSize, 60u);
}

TEST(FontManifest, OutOfRangeFileSizesFallBackToZero) {
  // Pinning current behaviour: `size` is read through `| 0`, whose int default
  // makes anything outside int range read as 0 rather than failing the parse.
  const Parsed p = parse(manifestWith(
      R"("families":[{"name":"A","files":[{"name":"huge","size":1000000000000,"crc32":1},)"
      R"({"name":"nan","size":"12","crc32":2},{"name":"ok","size":5,"crc32":3}]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families[0].files.size(), 3u);
  EXPECT_EQ(p.families[0].files[0].size, 0u);
  EXPECT_EQ(p.families[0].files[1].size, 0u);
  EXPECT_EQ(p.families[0].files[2].size, 5u);
  EXPECT_EQ(p.families[0].totalSize, 5u);
}

TEST(FontManifest, DuplicateFamilyNamesAreBothRetained) {
  const Parsed p = parse(manifestWith(
      R"("families":[{"name":"Dup","files":[{"name":"a","size":1,"crc32":1}]},)"
      R"({"name":"Dup","files":[{"name":"b","size":2,"crc32":2}]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 2u);
  EXPECT_EQ(p.families[0].name, "Dup");
  EXPECT_EQ(p.families[1].name, "Dup");
  EXPECT_EQ(p.families[0].totalSize, 1u);
  EXPECT_EQ(p.families[1].totalSize, 2u);
}

TEST(FontManifest, ALargeFamilyArrayParsesWithoutLoss) {
  std::string families = R"("families":[)";
  constexpr int COUNT = 300;
  for (int i = 0; i < COUNT; i++) {
    if (i > 0) families += ",";
    families += R"({"name":"F)" + std::to_string(i) + R"(","files":[{"name":"f","size":1,"crc32":1}]})";
  }
  families += "]";

  const Parsed p = parse(manifestWith(families));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), static_cast<size_t>(COUNT));
  EXPECT_EQ(p.families.back().name, "F299");
}

TEST(FontManifest, HostileNamesSurviveParsingAndAreRejectedByTheInstaller) {
  // The parser is a transport decoder, not a validator: the path checks that
  // stop "../" live in FontInstaller and run before anything touches the SD
  // card. Both halves are asserted here so the pairing cannot silently rot.
  const Parsed p = parse(manifestWith(
      R"("families":[{"name":"../../.crosspoint","files":[{"name":"../evil.cpfont","size":1,"crc32":1}]},)"
      R"({"name":"Good/Bad","files":[{"name":"ok_14.cpfont.tmp","size":1,"crc32":2}]}])"));

  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 2u);
  EXPECT_EQ(p.families[0].name, "../../.crosspoint");

  for (const auto& family : p.families) {
    EXPECT_FALSE(FontInstaller::isValidFamilyName(family.name.c_str())) << family.name;
    for (const auto& file : family.files) {
      EXPECT_FALSE(FontInstaller::isValidCpfontFilename(file.name.c_str())) << file.name;
    }
  }
}

TEST(FontManifest, TheAcceptedVersionIsTheOneTheHeaderPublishes) {
  // Drives the macro through the parser rather than comparing it to a literal,
  // so bumping FONTS_MANIFEST_VERSION alone cannot leave the gate behind.
  const auto atVersion = [](const int version) {
    return parse(R"({"version":)" + std::to_string(version) + R"(,"families":[]})").result;
  };

  EXPECT_EQ(atVersion(FONTS_MANIFEST_VERSION), FontManifestError::OK);
  EXPECT_EQ(atVersion(FONTS_MANIFEST_VERSION + 1), FontManifestError::UNSUPPORTED_VERSION);
  EXPECT_EQ(atVersion(FONTS_MANIFEST_VERSION - 1), FontManifestError::UNSUPPORTED_VERSION);
}

// --- Bad encoding ---

TEST(FontManifest, InvalidUtf8InStringsIsCarriedThroughAndRejectedDownstream) {
  // ArduinoJson copies string bytes verbatim, so a manifest can smuggle a lone
  // continuation byte or a truncated multi-byte sequence into a family name.
  const Parsed p = parse(manifestWith(
      "\"families\":[{\"name\":\"Bad\x80"
      "Cont\",\"files\":[{\"name\":\"a\",\"size\":1,\"crc32\":1}]},"
      "{\"name\":\"Trunc\xE4\xB8\",\"files\":[{\"name\":\"b\",\"size\":1,\"crc32\":2}]}]"));

  ASSERT_FALSE(p.jsonError);
  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 2u);

  // Both names survive intact, and the validator rejects them because non-ASCII
  // bytes are not alphanumeric.
  EXPECT_EQ(p.families[0].name, "Bad\x80"
                                "Cont");
  EXPECT_EQ(p.families[1].name, "Trunc\xE4\xB8");
  EXPECT_FALSE(FontInstaller::isValidFamilyName(p.families[0].name.c_str()));
  EXPECT_FALSE(FontInstaller::isValidFamilyName(p.families[1].name.c_str()));
}

TEST(FontManifest, AnEscapedNulSilentlyTruncatesTheRestOfTheName) {
  // ArduinoJson hands out a const char*, so an escaped NUL ends the name and
  // everything after it is dropped without failing the parse. A hostile entry
  // therefore installs under its harmless prefix rather than being rejected.
  const Parsed p = parse(manifestWith("\"families\":[{\"name\":\"Ok\\u0000/../escape\","
                                      "\"files\":[{\"name\":\"f\",\"size\":1,\"crc32\":1}]}]"));

  ASSERT_FALSE(p.jsonError);
  ASSERT_EQ(p.result, FontManifestError::OK);
  ASSERT_EQ(p.families.size(), 1u);

  const std::string& name = p.families[0].name;
  EXPECT_EQ(name, "Ok");
  EXPECT_EQ(name.size(), std::strlen(name.c_str())) << "no embedded NUL survives into the std::string";
  EXPECT_TRUE(FontInstaller::isValidFamilyName(name.c_str())) << "the surviving prefix passes validation";
}
