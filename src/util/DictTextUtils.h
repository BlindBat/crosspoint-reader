#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Pure text helpers behind the dictionary activities: word-selection
// tokenisation and cursor rows (DictionaryWordSelectActivity) and the
// plain-text definition wrap (DictionaryDefinitionActivity).
namespace DictTextUtils {

// A token is selectable when it has an ASCII alphanumeric or a non-ASCII
// codepoint outside U+2000-U+206F (dashes, bullets and other General
// Punctuation that appear as standalone tokens are not words).
bool isSelectableToken(const char* text);

// Index of the box in `row` whose horizontal center is closest to centerX;
// -1 when the row has no words. Box exposes x, width and row; the firmware
// instantiates this once, for the word-select activity's WordBox.
template <typename Box>
int closestInRow(const Box* boxes, const size_t count, const uint16_t row, const int centerX) {
  int best = -1;
  int bestDistance = INT_MAX;
  for (size_t i = 0; i < count; i++) {
    if (boxes[i].row != row) continue;
    const int distance = std::abs(boxes[i].x + boxes[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = static_cast<int>(i);
    }
  }
  return best;
}

// Longest measurable/drawable span. Wrapped lines stay under the screen width
// (far below this); only pathological unbreakable tokens are split at this cap.
constexpr size_t MAX_LINE_BYTES = 191;

// One wrapped display line: a byte span of the definition. Wrapping keeps
// lines under the screen width, so uint16_t length is ample.
struct WrappedLine {
  uint32_t start;
  uint16_t len;
};

// Width in pixels of a byte span (at most MAX_LINE_BYTES) in the body font.
struct SpanMeasurer {
  void* ctx;
  int (*measure)(void* ctx, const char* text, size_t len);
};

// Greedy word-wrap of text[0..n) into byte spans. '\n' and NUL break lines
// (blank lines survive as paragraph spacing); ' ', '\t' and '\r' separate
// tokens. Tokens wider than maxWidth are split at the widest fitting UTF-8
// boundary. Trailing blank lines are trimmed.
void wrapDefinitionText(const char* text, uint32_t n, int maxWidth, int spaceWidth, const SpanMeasurer& measurer,
                        std::vector<WrappedLine>& lines);

}  // namespace DictTextUtils
