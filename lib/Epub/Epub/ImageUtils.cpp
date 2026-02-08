#include "ImageUtils.h"

#include <EpdFont.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <algorithm>
#include <vector>

namespace ImageUtils {

bool convertImageToBmp(const std::string& inputImagePath, const std::string& outputBmpPath, int maxWidth,
                       int maxHeight) {
  FsFile imageFile;
  if (!SdMan.openFileForRead("IMG", inputImagePath, imageFile)) {
    return false;
  }

  // Detect format by reading first 8 bytes
  uint8_t header[8];
  const int bytesRead = imageFile.read(header, 8);
  imageFile.seekSet(0);  // Rewind for the converter

  if (bytesRead < 2) {
    Serial.printf("[%lu] [IMG] Image file too small\n", millis());
    imageFile.close();
    return false;
  }

  FsFile bmpFile;
  if (!SdMan.openFileForWrite("IMG", outputBmpPath, bmpFile)) {
    imageFile.close();
    return false;
  }

  bool success = false;

  // JPEG: starts with 0xFF 0xD8
  if (header[0] == 0xFF && header[1] == 0xD8) {
    Serial.printf("[%lu] [IMG] Detected JPEG format\n", millis());
    success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(imageFile, bmpFile, maxWidth, maxHeight);
  }
  // PNG: starts with 0x89 0x50 0x4E 0x47
  else if (bytesRead >= 4 && header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) {
    Serial.printf("[%lu] [IMG] Detected PNG format\n", millis());
    success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(imageFile, bmpFile, maxWidth, maxHeight);
  } else {
    Serial.printf("[%lu] [IMG] Unsupported image format (header: %02X %02X)\n", millis(), header[0], header[1]);
  }

  imageFile.close();
  bmpFile.close();

  if (!success) {
    SdMan.remove(outputBmpPath.c_str());
  }
  return success;
}

bool readBmpDimensions(const std::string& bmpPath, uint16_t& width, uint16_t& height) {
  FsFile file;
  if (!SdMan.openFileForRead("IMG", bmpPath, file)) {
    return false;
  }

  // BMP header: 'BM' at offset 0, width at offset 18 (4 bytes LE), height at offset 22 (4 bytes signed LE)
  uint8_t header[26];
  if (file.read(header, 26) != 26) {
    file.close();
    return false;
  }
  file.close();

  if (header[0] != 'B' || header[1] != 'M') {
    return false;
  }

  const int32_t w = static_cast<int32_t>(header[18]) | (static_cast<int32_t>(header[19]) << 8) |
                    (static_cast<int32_t>(header[20]) << 16) | (static_cast<int32_t>(header[21]) << 24);
  int32_t h = static_cast<int32_t>(header[22]) | (static_cast<int32_t>(header[23]) << 8) |
              (static_cast<int32_t>(header[24]) << 16) | (static_cast<int32_t>(header[25]) << 24);

  // Height can be negative (top-down BMP)
  if (h < 0) h = -h;

  width = static_cast<uint16_t>(w);
  height = static_cast<uint16_t>(h);
  return true;
}

namespace {

void writeBmpHeaders(FsFile& bmpFile, int width, int height, uint32_t pixelDataSize) {
  const uint32_t headerSize = 14 + 40 + 8;
  const uint32_t fileSize = headerSize + pixelDataSize;

  const uint8_t fileHeader[14] = {
      'B',
      'M',
      static_cast<uint8_t>(fileSize),
      static_cast<uint8_t>(fileSize >> 8),
      static_cast<uint8_t>(fileSize >> 16),
      static_cast<uint8_t>(fileSize >> 24),
      0,
      0,
      0,
      0,
      static_cast<uint8_t>(headerSize),
      static_cast<uint8_t>(headerSize >> 8),
      static_cast<uint8_t>(headerSize >> 16),
      static_cast<uint8_t>(headerSize >> 24),
  };
  bmpFile.write(fileHeader, 14);

  uint8_t infoHeader[40] = {};
  infoHeader[0] = 40;
  infoHeader[4] = static_cast<uint8_t>(width);
  infoHeader[5] = static_cast<uint8_t>(width >> 8);
  infoHeader[6] = static_cast<uint8_t>(width >> 16);
  infoHeader[7] = static_cast<uint8_t>(width >> 24);
  infoHeader[8] = static_cast<uint8_t>(height);
  infoHeader[9] = static_cast<uint8_t>(height >> 8);
  infoHeader[10] = static_cast<uint8_t>(height >> 16);
  infoHeader[11] = static_cast<uint8_t>(height >> 24);
  infoHeader[12] = 1;  // planes
  infoHeader[14] = 1;  // bits per pixel
  infoHeader[32] = 2;  // colorsUsed
  infoHeader[36] = 2;  // colorsImportant
  bmpFile.write(infoHeader, 40);

  const uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  bmpFile.write(palette, 8);
}

}  // namespace

bool generateStubCoverBmp(const std::string& outputPath, int width, int height) {
  FsFile bmpFile;
  if (!SdMan.openFileForWrite("IMG", outputPath, bmpFile)) {
    return false;
  }

  const int rowBytes = ((width + 31) / 32) * 4;
  const uint32_t pixelDataSize = static_cast<uint32_t>(rowBytes) * height;

  writeBmpHeaders(bmpFile, width, height, pixelDataSize);

  // All-white pixel data
  auto* rowBuf = static_cast<uint8_t*>(malloc(rowBytes));
  if (!rowBuf) {
    bmpFile.close();
    SdMan.remove(outputPath.c_str());
    return false;
  }
  memset(rowBuf, 0xFF, rowBytes);
  for (int y = 0; y < height; y++) {
    bmpFile.write(rowBuf, rowBytes);
  }

  free(rowBuf);
  bmpFile.close();
  return true;
}

bool generateDecoratedStubCoverBmp(const std::string& outputPath, int width, int height, const std::string& title,
                                   const std::string& author, const EpdFontData* fontData) {
  if (!fontData) {
    return generateStubCoverBmp(outputPath, width, height);
  }

  const int rowBytes = ((width + 31) / 32) * 4;
  const size_t pixelDataSize = static_cast<size_t>(rowBytes) * height;

  auto* pixels = static_cast<uint8_t*>(malloc(pixelDataSize));
  if (!pixels) {
    return generateStubCoverBmp(outputPath, width, height);
  }
  memset(pixels, 0xFF, pixelDataSize);  // All white

  // Set a pixel to black in the bottom-up BMP buffer.
  // Screen coordinates: (0,0) = top-left, Y increases downward.
  auto setBlack = [&](int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const int bmpRow = height - 1 - y;
    pixels[bmpRow * rowBytes + x / 8] &= ~(1 << (7 - (x % 8)));
  };

  // Draw double-line border frame (proportionally scaled)
  const int outerMargin = std::max(2, std::min(width, height) / 25);
  const int innerMargin = outerMargin + std::max(2, outerMargin / 2);

  // Outer rectangle
  for (int x = outerMargin; x < width - outerMargin; x++) {
    setBlack(x, outerMargin);
    setBlack(x, height - 1 - outerMargin);
  }
  for (int y = outerMargin; y < height - outerMargin; y++) {
    setBlack(outerMargin, y);
    setBlack(width - 1 - outerMargin, y);
  }

  // Inner rectangle
  for (int x = innerMargin; x < width - innerMargin; x++) {
    setBlack(x, innerMargin);
    setBlack(x, height - 1 - innerMargin);
  }
  for (int y = innerMargin; y < height - innerMargin; y++) {
    setBlack(innerMargin, y);
    setBlack(width - 1 - innerMargin, y);
  }

  // Text rendering
  EpdFont font(fontData);
  const int textMargin = innerMargin + std::max(3, innerMargin / 2);
  const int maxTextWidth = width - textMargin * 2;

  if (maxTextWidth > 10) {
    // Render a string glyph-by-glyph into the pixel buffer
    auto renderText = [&](const char* str, int startX, int baselineY) {
      int cursorX = startX;
      const auto* s = reinterpret_cast<const uint8_t*>(str);
      uint32_t cp;
      while ((cp = utf8NextCodepoint(&s))) {
        const EpdGlyph* glyph = font.getGlyph(cp);
        if (!glyph) continue;

        const uint8_t* bmp = &fontData->bitmap[glyph->dataOffset];
        for (int gy = 0; gy < glyph->height; gy++) {
          const int screenY = baselineY - glyph->top + gy;
          for (int gx = 0; gx < glyph->width; gx++) {
            const int pixPos = gy * glyph->width + gx;
            // 1-bit font: 8 pixels per byte, MSB first
            if ((bmp[pixPos / 8] >> (7 - (pixPos % 8))) & 1) {
              setBlack(cursorX + glyph->left + gx, screenY);
            }
          }
        }
        cursorX += glyph->advanceX;
      }
    };

    // Word-wrap text into lines that fit within maxTextWidth
    auto wrapText = [&](const std::string& text, int maxLines) -> std::vector<std::string> {
      std::vector<std::string> lines;
      size_t start = 0;
      std::string currentLine;

      while (start <= text.size() && static_cast<int>(lines.size()) < maxLines) {
        size_t spacePos = text.find(' ', start);
        if (spacePos == std::string::npos) spacePos = text.size();

        std::string word = text.substr(start, spacePos - start);
        start = spacePos + 1;
        if (word.empty()) continue;

        std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
        int tw, th;
        font.getTextDimensions(testLine.c_str(), &tw, &th);

        if (tw <= maxTextWidth || currentLine.empty()) {
          currentLine = testLine;
        } else {
          lines.push_back(currentLine);
          currentLine = word;
        }
      }
      if (!currentLine.empty()) {
        if (static_cast<int>(lines.size()) < maxLines) {
          lines.push_back(currentLine);
        } else {
          // Append remainder to last line (will be clipped by setBlack bounds)
          lines.back() += " " + currentLine;
        }
      }
      return lines;
    };

    const int lineHeight = fontData->advanceY;
    const int bottomLimit = height - innerMargin - lineHeight;

    // Title: word-wrapped, centered around 1/3 height
    int cursorY = height / 3;
    if (!title.empty()) {
      auto titleLines = wrapText(title, 4);
      // Center the title block around height/3
      int titleStartY = height / 3 - static_cast<int>(titleLines.size() - 1) * lineHeight / 2;
      for (const auto& line : titleLines) {
        if (titleStartY > bottomLimit) break;
        int lw, lh;
        font.getTextDimensions(line.c_str(), &lw, &lh);
        renderText(line.c_str(), (width - lw) / 2, titleStartY);
        titleStartY += lineHeight;
      }
      cursorY = titleStartY + 4;
    }

    // Author: word-wrapped, below title block
    if (!author.empty()) {
      auto authorLines = wrapText(author, 2);
      for (const auto& line : authorLines) {
        if (cursorY > bottomLimit) break;
        int lw, lh;
        font.getTextDimensions(line.c_str(), &lw, &lh);
        renderText(line.c_str(), (width - lw) / 2, cursorY);
        cursorY += lineHeight;
      }
    }
  }

  // Write BMP file
  FsFile bmpFile;
  if (!SdMan.openFileForWrite("IMG", outputPath, bmpFile)) {
    free(pixels);
    return false;
  }

  writeBmpHeaders(bmpFile, width, height, static_cast<uint32_t>(pixelDataSize));
  bmpFile.write(pixels, pixelDataSize);

  free(pixels);
  bmpFile.close();
  return true;
}

}  // namespace ImageUtils
