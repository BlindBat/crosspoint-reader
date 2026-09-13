// Link-time stubs for symbols the compiled production TUs reference but this
// suite does not exercise (image blocks, BiDi reordering, hyphenation
// patterns). Text serialization is the real code.

#include <BidiUtils.h>
#include <Epub/Page.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }
void Hyphenator::setPreferredLanguage(const std::string&) {}
const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;

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

// FB2 sections are text-only; no ImageBlock is ever built, but Page.cpp
// references the symbols.
ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}
bool ImageBlock::imageExists() const { return false; }
bool ImageBlock::hasValidCache() const { return false; }
bool ImageBlock::needsDecode() const { return false; }
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }
