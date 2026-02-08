#include "Page.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int /* fontId */, const int xOffset, const int yOffset) {
  FsFile bmpFile;
  if (!SdMan.openFileForRead("PGI", bmpCachePath, bmpFile)) {
    Serial.printf("[%lu] [PGI] Failed to open BMP for render: %s\n", millis(), bmpCachePath.c_str());
    return;
  }

  Bitmap bitmap(bmpFile);
  const auto err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    Serial.printf("[%lu] [PGI] BMP parse error: %s\n", millis(), Bitmap::errorToString(err));
    bmpFile.close();
    return;
  }

  renderer.drawBitmap1Bit(bitmap, xPos + xOffset, yPos + yOffset, imgWidth, imgHeight);
  bmpFile.close();
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, imgWidth);
  serialization::writePod(file, imgHeight);
  serialization::writeString(file, bmpCachePath);
  return true;
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos, yPos;
  uint16_t w, h;
  std::string path;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, w);
  serialization::readPod(file, h);
  serialization::readString(file, path);
  return std::unique_ptr<PageImage>(new PageImage(path, w, h, xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset);
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));
    if (!el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      page->elements.push_back(std::move(pi));
    } else {
      Serial.printf("[%lu] [PGE] Deserialization failed: Unknown tag %u\n", millis(), tag);
      return nullptr;
    }
  }

  return page;
}
