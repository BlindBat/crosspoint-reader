// One-shot parity goldens: a hash of every fixture chapter's serialized section
// file, captured from the pre-incremental-build implementation.
//
// This is the test that proves the incremental build (startBuild/buildSomeMore)
// did not change what lands on disk. It must stay green through the refactor;
// if it goes red, a sliced build is no longer byte-identical to a one-shot one
// and the cache has become a correctness hazard rather than a speed-up.
//
// Regenerating: only when FB2_SECTION_FILE_VERSION legitimately changes. Run
//   CROSSPOINT_FB2_GOLDEN_DUMP=1 <suite binary> --gtest_filter=*Golden*
// and paste the printed table over kGoldens below.

#include <Epub/ReaderRenderSpec.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "Fb2.h"
#include "Fb2/Fb2Section.h"
#include "Fb2TestSupport.h"

namespace {

using fb2test::fixturePath;
using fb2test::readAll;

// Every fixture in test/fb2/, in sorted order. Listed explicitly rather than
// globbed so a fixture that silently disappears fails the test.
constexpr const char* kFixtures[] = {
    "base64-corrupt-cover.fb2",
    "basic.fb2",
    "body-prefix.fb2",
    "cover-wrapped.fb2",
    "cp1251-declared.fb2",
    "cp1252-declared.fb2",
    "long.fb2",
    "malformed-truncated.fb2",
    "multi-author.fb2",
    "nested-deep.fb2",
    "nested-sections.fb2",
    "no-cover.fb2",
    "no-sections.fb2",
    "notes-body.fb2",
    "slice-boundary.fb2",
    "styles.fb2",
    "trailing-parent-text.fb2",
    "unicode-titles.fb2",
    "wrapper-only.fb2",
    "wrong-root.fb2",
};

struct Golden {
  const char* fixture;
  int chapter;
  uint64_t hash;  // 0 means createSectionFile() returned false for this chapter
};

// GENERATED — see the regeneration note at the top of this file.
constexpr Golden kGoldens[] = {
    {"base64-corrupt-cover.fb2", 0, 0xe5662742145ecc9cULL},
    {"basic.fb2", 0, 0x430dc984a5221d70ULL},
    {"basic.fb2", 1, 0xff5482e5434a3abcULL},
    {"body-prefix.fb2", 0, 0x261012990916872bULL},
    {"body-prefix.fb2", 1, 0xb1edbf448284d2a1ULL},
    {"cover-wrapped.fb2", 0, 0xe5662742145ecc9cULL},
    {"cp1251-declared.fb2", 0, 0xaa38522969be056dULL},
    {"cp1251-declared.fb2", 1, 0x4a0c21a7f80193dbULL},
    {"cp1252-declared.fb2", 0, 0xbd9fac96b7d1fe55ULL},
    {"long.fb2", 0, 0x97e8929e4adba952ULL},
    {"multi-author.fb2", 0, 0x059b3d4ac8faca4dULL},
    {"nested-deep.fb2", 0, 0x30a69056715633bcULL},
    {"nested-deep.fb2", 1, 0x7e795616ffb219c4ULL},
    {"nested-deep.fb2", 2, 0x6cbd1f5140d6294dULL},
    {"nested-deep.fb2", 3, 0xdae16576e8439972ULL},
    {"nested-sections.fb2", 0, 0xa5ead360db3db865ULL},
    {"nested-sections.fb2", 1, 0x2da05b819aef8e6cULL},
    {"nested-sections.fb2", 2, 0x7e4679c07db1926aULL},
    {"no-cover.fb2", 0, 0x446b63441e37d2c7ULL},
    {"no-sections.fb2", 0, 0x7c7341457be55cbbULL},
    {"notes-body.fb2", 0, 0xf595e62b2c3426b4ULL},
    {"slice-boundary.fb2", 0, 0x350b7bcfe30715a4ULL},
    {"styles.fb2", 0, 0xa74e8eee55605af9ULL},
    {"styles.fb2", 1, 0x8ef5e5c1511d9225ULL},
    {"trailing-parent-text.fb2", 0, 0xefa039cc876bdf4bULL},
    {"trailing-parent-text.fb2", 1, 0x72c9b693a6734f29ULL},
    {"unicode-titles.fb2", 0, 0x22dc9c703718d204ULL},
    {"unicode-titles.fb2", 1, 0x3eb94a2666720ec8ULL},
    {"wrapper-only.fb2", 0, 0x012d98825a7771adULL},
    {"wrapper-only.fb2", 1, 0x2209f0fac99c3ab9ULL},
    {"wrapper-only.fb2", 2, 0xa4f09cba06a551c2ULL},
    {"wrong-root.fb2", 0, 0xe24741dc596d6b78ULL},
};

ReaderRenderSpec makeSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 0;
  spec.lineCompression = 1.0f;
  spec.viewportWidth = 400;
  spec.viewportHeight = 64;
  return spec;
}

// FNV-1a over the whole section file. Not cryptographic; it only has to change
// when the bytes change.
uint64_t hashBytes(const std::string& bytes) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const unsigned char c : bytes) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return h;
}

// Build every chapter of every fixture and hash the resulting section file.
std::vector<Golden> captureAll(const std::string& tmpPath, GfxRenderer& renderer,
                               std::vector<std::string>& fixtureNames) {
  std::vector<Golden> out;
  for (const char* name : kFixtures) {
    auto book = std::make_shared<Fb2>(fixturePath(name), tmpPath);
    if (!book->load()) continue;  // wrong-root / truncated: nothing to paginate
    fixtureNames.emplace_back(name);
    for (int i = 0; i < book->getSectionCount(); i++) {
      auto section = std::make_unique<Fb2Section>(book, i, renderer);
      const bool ok = section->createSectionFile(makeSpec());
      const std::string path = book->getCachePath() + "/sections/" + std::to_string(i) + ".bin";
      out.push_back({fixtureNames.back().c_str(), i, ok ? hashBytes(readAll(path)) : 0});
    }
  }
  return out;
}

TEST(Fb2SectionGolden, OneShotOutputMatchesCapturedGoldens) {
  fb2test::TempDir tmp;
  ASSERT_TRUE(tmp.valid());
  GfxRenderer renderer;

  std::vector<std::string> names;  // owns the strings the Goldens point at
  names.reserve(sizeof(kFixtures) / sizeof(kFixtures[0]));
  const auto captured = captureAll(tmp.path(), renderer, names);
  ASSERT_FALSE(captured.empty()) << "no fixture produced a section file";

  if (std::getenv("CROSSPOINT_FB2_GOLDEN_DUMP") != nullptr) {
    std::printf("constexpr Golden kGoldens[] = {\n");
    for (const auto& g : captured) {
      std::printf("    {\"%s\", %d, 0x%016llxULL},\n", g.fixture, g.chapter, static_cast<unsigned long long>(g.hash));
    }
    std::printf("};\n");
    GTEST_SKIP() << "dumped goldens; paste them into Fb2SectionGoldenTest.cpp";
  }

  const size_t expectedCount = sizeof(kGoldens) / sizeof(kGoldens[0]);
  ASSERT_EQ(captured.size(), expectedCount) << "fixture set changed; regenerate the goldens";
  for (size_t i = 0; i < expectedCount; i++) {
    EXPECT_STREQ(captured[i].fixture, kGoldens[i].fixture) << "at index " << i;
    EXPECT_EQ(captured[i].chapter, kGoldens[i].chapter) << "at index " << i;
    EXPECT_EQ(captured[i].hash, kGoldens[i].hash)
        << kGoldens[i].fixture << " chapter " << kGoldens[i].chapter << " serialized differently";
  }
}

}  // namespace
