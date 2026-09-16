// Link-time stubs for symbols the compiled production TUs reference but this
// suite never exercises: the image pipeline (fixtures are text-only chapters,
// so no ImageBlock is ever built or deserialized). Everything on the text
// path -- Section, ChapterHtmlSlimParser, Page/TextBlock serialization,
// ParsedText layout, hyphenation, MiniBidi -- is the real code.

#include "SectionLinkStubs.h"

#include <Epub/Page.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>
#include <Serialization.h>

#include <new>

bool gImageBlockDeserializeFails = false;

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}
bool ImageBlock::imageExists() const { return false; }
bool ImageBlock::hasValidCache() const { return false; }
bool ImageBlock::needsDecode() const { return false; }
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::serialize(HalFile&) { return false; }

// Mirrors production ImageBlock::deserialize byte for byte -- same field order,
// same "the nothrow allocation may fail" contract -- so PageImage::deserialize
// can be exercised over real cache bytes. The rest of the image pipeline stays
// stubbed. gImageBlockDeserializeFails stands in for that allocation failing
// under memory pressure, the one way production yields a null block.
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile& file) {
  std::string path;
  std::string src;
  serialization::readString(file, path);
  serialization::readString(file, src);
  int16_t width = 0;
  int16_t height = 0;
  serialization::readPod(file, width);
  serialization::readPod(file, height);
  if (gImageBlockDeserializeFails) return nullptr;
  return std::unique_ptr<ImageBlock>(new (std::nothrow) ImageBlock(path, src, width, height));
}

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}
