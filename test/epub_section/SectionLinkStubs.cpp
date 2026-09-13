// Link-time stubs for symbols the compiled production TUs reference but this
// suite never exercises: the image pipeline (fixtures are text-only chapters,
// so no ImageBlock is ever built or deserialized). Everything on the text
// path -- Section, ChapterHtmlSlimParser, Page/TextBlock serialization,
// ParsedText layout, hyphenation, MiniBidi -- is the real code.

#include <Epub/Page.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}
bool ImageBlock::imageExists() const { return false; }
bool ImageBlock::hasValidCache() const { return false; }
bool ImageBlock::needsDecode() const { return false; }
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}
