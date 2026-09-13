// Link-time stubs for symbols the compiled production TUs reference but the
// suite does not exercise (rendering, BiDi reordering, hyphenation patterns).

#include <BidiUtils.h>
#include <Epub/Page.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }

namespace BidiUtils {
bool startsWithRtl(const char*, int) { return false; }
int detectParagraphLevel(const char*, int fallbackLevel, int) { return fallbackLevel; }
bool isTransparentMark(uint32_t) { return false; }
bool computeVisualWordOrder(const std::vector<std::string>& words, bool, std::vector<uint16_t>& order) {
  order.resize(words.size());
  for (size_t index = 0; index < words.size(); ++index) order[index] = static_cast<uint16_t>(index);
  return true;
}
}  // namespace BidiUtils

// Page.cpp is not part of this suite; PageLine's virtuals still need bodies.
void PageLine::render(GfxRenderer&, int, int, int) {}
bool PageLine::serialize(HalFile&) { return false; }
void PageImage::render(GfxRenderer&, int, int, int) {}
void PageImage::renderPlaceholder(GfxRenderer&, int, int) const {}
bool PageImage::serialize(HalFile&) { return false; }
void PageHorizontalRule::render(GfxRenderer&, int, int, int) {}
bool PageHorizontalRule::serialize(HalFile&) { return false; }
