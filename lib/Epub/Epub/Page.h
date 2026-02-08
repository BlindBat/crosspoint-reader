#pragma once
#include <SdFat.h>

#include <string>
#include <utility>
#include <vector>

#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageImage = 2,
};

// represents something that has been added to a page
class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual PageElementTag getTag() const = 0;
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) = 0;
  virtual bool serialize(FsFile& file) = 0;
};

// a line from a block element
class PageLine final : public PageElement {
  std::shared_ptr<TextBlock> block;

 public:
  PageLine(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}
  PageElementTag getTag() const override { return TAG_PageLine; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageLine> deserialize(FsFile& file);
};

class PageImage final : public PageElement {
  std::string bmpCachePath;
  uint16_t imgWidth;
  uint16_t imgHeight;

 public:
  PageImage(const std::string& path, uint16_t w, uint16_t h, int16_t xPos, int16_t yPos)
      : PageElement(xPos, yPos), bmpCachePath(path), imgWidth(w), imgHeight(h) {}
  PageElementTag getTag() const override { return TAG_PageImage; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageImage> deserialize(FsFile& file);
};

class Page {
 public:
  // the list of block index and line numbers on this page
  std::vector<std::shared_ptr<PageElement>> elements;
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  bool serialize(FsFile& file) const;
  static std::unique_ptr<Page> deserialize(FsFile& file);
};
