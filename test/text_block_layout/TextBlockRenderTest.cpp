// Render-path tests for TextBlock::render — the per-line, per-repaint hot path.
//
// Two things are pinned here:
//   1. the exact drawText sequence a ruby-bearing line emits (position, text, style, base
//      direction, and the order words and their ruby overlays are issued in), and
//   2. that a repeated render of the same line allocates nothing.
//
// The GfxRenderer stub records draw calls into fixed static storage and never touches the
// heap, so (1) and (2) can be measured on the same code path. AllocCounter is
// single-threaded by contract; these tests run on the gtest main thread.

#include <AllocCounter.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// Stub metrics: 10 px per codepoint (regular), ascender 12 px. RUBY_CONTINUE and SUP are
// decoration bits, so they measure at the regular advance too.
constexpr int kAdvance = GfxRenderer::kRegularAdvance;
constexpr int kAscender = GfxRenderer::kAscender;

// One ruby group ("漢字" annotated as a unit) followed by an unannotated word.
std::unique_ptr<TextBlock> makeRubyLine(const std::string& rubyText) {
  const std::vector<std::string> words = {"漢", "字", "tail"};
  const std::vector<int16_t> xpos = {0, 20, 40};
  const std::vector<EpdFontFamily::Style> styles = {EpdFontFamily::REGULAR, EpdFontFamily::RUBY_CONTINUE,
                                                    EpdFontFamily::REGULAR};
  std::vector<std::string> ruby = {rubyText, "", ""};
  auto block = std::make_unique<TextBlock>(words, xpos, styles, std::vector<uint8_t>{}, std::vector<uint16_t>{},
                                           BlockStyle(), std::move(ruby), std::vector<TextBlock::LinkSpan>{});
  return block;
}

class TextBlockRenderTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  void SetUp() override { GfxRenderer::resetDrawCalls(); }
};

TEST_F(TextBlockRenderTest, RubyOverlayIsCenteredOverItsGroupAndDrawnAfterTheLeaderWord) {
  const auto block = makeRubyLine("かんじ");  // 3 codepoints -> 30 px
  ASSERT_TRUE(block->valid());

  block->render(renderer, /*fontId=*/0, /*x=*/100, /*y=*/200);

  // hasRuby() lifts every word by ascender/2 so the annotation has room above the line.
  const int wordY = 200 + kAscender / 2;
  const int rubyY = wordY - kAscender;
  // Group is "漢" + "字" = 20 px wide, the annotation 30 px, so it overhangs 5 px each side.
  const int rubyX = 100 - (3 * kAdvance - 2 * kAdvance) / 2;

  ASSERT_EQ(GfxRenderer::drawCallCount, 4u);

  EXPECT_STREQ(GfxRenderer::drawCalls[0].text, "漢");
  EXPECT_EQ(GfxRenderer::drawCalls[0].x, 100);
  EXPECT_EQ(GfxRenderer::drawCalls[0].y, wordY);
  EXPECT_EQ(GfxRenderer::drawCalls[0].style, EpdFontFamily::REGULAR);

  EXPECT_STREQ(GfxRenderer::drawCalls[1].text, "かんじ");
  EXPECT_EQ(GfxRenderer::drawCalls[1].x, rubyX);
  EXPECT_EQ(GfxRenderer::drawCalls[1].y, rubyY);
  EXPECT_EQ(GfxRenderer::drawCalls[1].style, EpdFontFamily::SUP);
  EXPECT_EQ(GfxRenderer::drawCalls[1].baseDir, static_cast<int>(BidiUtils::BidiBaseDir::LTR));

  EXPECT_STREQ(GfxRenderer::drawCalls[2].text, "字");
  EXPECT_EQ(GfxRenderer::drawCalls[2].x, 100 + 20);
  EXPECT_EQ(GfxRenderer::drawCalls[2].y, wordY);
  EXPECT_EQ(GfxRenderer::drawCalls[2].style, EpdFontFamily::RUBY_CONTINUE);

  EXPECT_STREQ(GfxRenderer::drawCalls[3].text, "tail");
  EXPECT_EQ(GfxRenderer::drawCalls[3].x, 100 + 40);
  EXPECT_EQ(GfxRenderer::drawCalls[3].y, wordY);
}

TEST_F(TextBlockRenderTest, LineWithoutRubyDrawsOnlyItsWordsAtTheUnshiftedBaseline) {
  const auto block = makeRubyLine("");  // no annotation anywhere -> hasRuby() false
  ASSERT_TRUE(block->valid());

  block->render(renderer, /*fontId=*/0, /*x=*/100, /*y=*/200);

  ASSERT_EQ(GfxRenderer::drawCallCount, 3u);
  EXPECT_STREQ(GfxRenderer::drawCalls[0].text, "漢");
  EXPECT_EQ(GfxRenderer::drawCalls[0].y, 200);  // no ruby shift
  EXPECT_STREQ(GfxRenderer::drawCalls[1].text, "字");
  EXPECT_STREQ(GfxRenderer::drawCalls[2].text, "tail");
}

// Fails before the ruby pre-pass was folded into the word loop: each render resized a
// std::vector<RubyDrawInfo> to the word count, i.e. one heap block per line per repaint.
TEST_F(TextBlockRenderTest, RepeatedRenderOfARubyLineAllocatesNothing) {
  const auto block = makeRubyLine("かんじ");
  ASSERT_TRUE(block->valid());
  block->render(renderer, 0, 100, 200);  // warm

  size_t count = 0;
  {
    alloc_counter::CountingScope scope;
    for (int i = 0; i < 10; ++i) {
      GfxRenderer::resetDrawCalls();
      block->render(renderer, 0, 100, 200);
    }
    count = scope.count();
  }
  EXPECT_EQ(count, 0u);
}

// Same guard with an annotation past any std::string small-buffer capacity: before the
// change the per-render copy of the ruby text was a second heap block on top of the vector.
TEST_F(TextBlockRenderTest, RepeatedRenderOfALongRubyAnnotationAllocatesNothing) {
  const auto block = makeRubyLine("かんじかんじかんじ");  // 27 bytes
  ASSERT_TRUE(block->valid());
  block->render(renderer, 0, 100, 200);  // warm

  size_t count = 0;
  {
    alloc_counter::CountingScope scope;
    for (int i = 0; i < 10; ++i) {
      GfxRenderer::resetDrawCalls();
      block->render(renderer, 0, 100, 200);
    }
    count = scope.count();
  }
  EXPECT_EQ(count, 0u);
}

}  // namespace
