// Allocation guards for ChapterHtmlSlimParser::startElement, the per-element hot path.
//
// startElement runs once per HTML element in a chapter (tens of thousands of times for a
// long section). It reads the class/style/dir attribute values straight out of expat's
// `atts` array, which stays alive for the duration of the callback, so nothing on that
// path may copy them into owning strings.
//
// AllocCounter is single-threaded by contract; these tests run on the gtest main thread.

#include <AllocCounter.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {

class ChapterHtmlSlimParserAllocTest : public ::testing::Test {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               {},
                               &cssParser};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(false); }

  // Runs `iterations` start/end element pairs and returns the counted operator-new calls.
  size_t countElementPasses(const XML_Char** atts, int iterations) {
    // Warm first: the first pass through a tag can grow parser-owned containers.
    ChapterHtmlSlimParser::startElement(&parser, "span", atts);
    ChapterHtmlSlimParser::endElement(&parser, "span");

    alloc_counter::CountingScope scope;
    for (int i = 0; i < iterations; ++i) {
      ChapterHtmlSlimParser::startElement(&parser, "span", atts);
      ChapterHtmlSlimParser::endElement(&parser, "span");
    }
    return scope.count();
  }
};

// Attribute values deliberately longer than any std::string small-buffer capacity
// (22 bytes on libc++, 15 on libstdc++), so an owning copy is guaranteed to hit the heap.
constexpr const char* kLongClass = "chapter-body first-paragraph drop-cap-holder";
constexpr const char* kLongStyle = "text-align: justify; margin-left: 2em; text-indent: 1.5em";

TEST_F(ChapterHtmlSlimParserAllocTest, ElementWithNoAttributesAllocatesNothing) {
  const XML_Char* bare[] = {nullptr};
  const size_t count = countElementPasses(bare, 10);
  EXPECT_EQ(count, 0u);
}

// Fails before the string_view/const char* rewrite: each pass copied class, style and dir
// into three std::strings, so ten passes cost thirty heap blocks.
TEST_F(ChapterHtmlSlimParserAllocTest, ClassStyleAndDirAttributesAllocateNothing) {
  const XML_Char* withAttrs[] = {"class", kLongClass, "style", kLongStyle, "dir", "ltr", nullptr};
  const size_t count = countElementPasses(withAttrs, 10);
  EXPECT_EQ(count, 0u);
}

// The attributes must still reach the CSS cascade and the direction override.
TEST_F(ChapterHtmlSlimParserAllocTest, DirAttributeStillOverridesDirection) {
  const XML_Char* rtl[] = {"class", kLongClass, "style", kLongStyle, "dir", "RTL", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "span", rtl);
  EXPECT_TRUE(parser.effectiveDirectionDefined);
  EXPECT_EQ(parser.effectiveDirection, CssTextDirection::Rtl);
  ChapterHtmlSlimParser::endElement(&parser, "span");

  const XML_Char* ltr[] = {"dir", "ltr", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "span", ltr);
  EXPECT_TRUE(parser.effectiveDirectionDefined);
  EXPECT_EQ(parser.effectiveDirection, CssTextDirection::Ltr);
  ChapterHtmlSlimParser::endElement(&parser, "span");
}

// display:none arrives through the inline style attribute, which must still be parsed.
TEST_F(ChapterHtmlSlimParserAllocTest, InlineDisplayNoneStillSkipsTheSubtree) {
  const XML_Char* hidden[] = {"class", kLongClass, "style", "display: none; color: red", nullptr};
  const int depthBefore = parser.depth;
  ChapterHtmlSlimParser::startElement(&parser, "span", hidden);
  EXPECT_EQ(parser.skipUntilDepth, depthBefore);
  EXPECT_EQ(parser.depth, depthBefore + 1);
}

}  // namespace
