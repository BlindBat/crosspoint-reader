#include "PngToBmpConverter.h"

#include <BitmapHelpers.h>
#include <HardwareSerial.h>
#include <SdFat.h>
#include <miniz.h>

#include <cstring>

namespace {

// PNG chunk type helper
uint32_t chunkType(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
}

uint32_t readBE32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
}

inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

void writeBmpHeader1bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 62);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Top-down
  write16(bmpOut, 1);
  write16(bmpOut, 1);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 2);
  write32(bmpOut, 2);

  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t b : palette) {
    bmpOut.write(b);
  }
}

// Paeth predictor for PNG filter
inline uint8_t paethPredictor(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = abs(p - a);
  const int pb = abs(p - b);
  const int pc = abs(p - c);
  if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
  if (pb <= pc) return static_cast<uint8_t>(b);
  return static_cast<uint8_t>(c);
}

// Convert a pixel (from potentially various color types) to grayscale
inline uint8_t toGray(const uint8_t* pixel, int colorType, int bytesPerPixel, const uint8_t* palette,
                      const uint8_t* trns, int trnsLen) {
  switch (colorType) {
    case 0: {  // Grayscale
      uint8_t gray = pixel[0];
      // Check if this grayscale value is transparent
      if (trns && trnsLen >= 2) {
        uint16_t trnsGray = (static_cast<uint16_t>(trns[0]) << 8) | trns[1];
        if (gray == static_cast<uint8_t>(trnsGray)) return 255;  // Transparent -> white
      }
      return gray;
    }
    case 2: {  // RGB
      uint8_t gray = (pixel[0] * 25 + pixel[1] * 50 + pixel[2] * 25) / 100;
      // Check tRNS for RGB transparency
      if (trns && trnsLen >= 6) {
        uint16_t trnsR = (static_cast<uint16_t>(trns[0]) << 8) | trns[1];
        uint16_t trnsG = (static_cast<uint16_t>(trns[2]) << 8) | trns[3];
        uint16_t trnsB = (static_cast<uint16_t>(trns[4]) << 8) | trns[5];
        if (pixel[0] == static_cast<uint8_t>(trnsR) && pixel[1] == static_cast<uint8_t>(trnsG) &&
            pixel[2] == static_cast<uint8_t>(trnsB)) {
          return 255;  // Transparent -> white
        }
      }
      return gray;
    }
    case 3: {  // Indexed (palette)
      int idx = pixel[0];
      // Check tRNS for palette transparency
      if (trns && idx < trnsLen && trns[idx] < 128) return 255;  // Transparent -> white
      if (palette) {
        return (palette[idx * 3] * 25 + palette[idx * 3 + 1] * 50 + palette[idx * 3 + 2] * 25) / 100;
      }
      return pixel[0];
    }
    case 4: {  // Grayscale + Alpha
      uint8_t alpha = pixel[1];
      if (alpha < 128) return 255;  // Transparent -> white
      return pixel[0];
    }
    case 6: {  // RGBA
      uint8_t alpha = pixel[3];
      if (alpha < 128) return 255;  // Transparent -> white
      return (pixel[0] * 25 + pixel[1] * 50 + pixel[2] * 25) / 100;
    }
    default:
      return 128;
  }
}

}  // namespace

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                       int targetMaxHeight) {
  Serial.printf("[%lu] [PNG] Converting PNG to 1-bit BMP (target: %dx%d)\n", millis(), targetMaxWidth, targetMaxHeight);

  // Read and verify PNG signature
  uint8_t sig[8];
  if (pngFile.read(sig, 8) != 8) return false;
  const uint8_t pngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (memcmp(sig, pngSig, 8) != 0) {
    Serial.printf("[%lu] [PNG] Not a valid PNG file\n", millis());
    return false;
  }

  // Parse IHDR
  uint8_t chunkHeader[8];
  if (pngFile.read(chunkHeader, 8) != 8) return false;
  uint32_t chunkLen = readBE32(chunkHeader);
  uint32_t type = chunkType(chunkHeader + 4);

  if (type != 0x49484452 || chunkLen < 13) {  // "IHDR"
    Serial.printf("[%lu] [PNG] Missing IHDR chunk\n", millis());
    return false;
  }

  uint8_t ihdr[13];
  if (pngFile.read(ihdr, 13) != 13) return false;
  pngFile.read(chunkHeader, 4);  // Skip CRC

  const int imgWidth = static_cast<int>(readBE32(ihdr));
  const int imgHeight = static_cast<int>(readBE32(ihdr + 4));
  const int bitDepth = ihdr[8];
  const int colorType = ihdr[9];
  const int compressionMethod = ihdr[10];
  const int filterMethod = ihdr[11];
  const int interlaceMethod = ihdr[12];

  Serial.printf("[%lu] [PNG] %dx%d, depth=%d, colorType=%d, interlace=%d\n", millis(), imgWidth, imgHeight, bitDepth,
                colorType, interlaceMethod);

  if (compressionMethod != 0 || filterMethod != 0) {
    Serial.printf("[%lu] [PNG] Unsupported compression/filter method\n", millis());
    return false;
  }

  if (interlaceMethod != 0) {
    Serial.printf("[%lu] [PNG] Interlaced PNG not supported\n", millis());
    return false;
  }

  // Safety limits
  if (imgWidth > 2048 || imgHeight > 3072 || imgWidth <= 0 || imgHeight <= 0) {
    Serial.printf("[%lu] [PNG] Image too large or invalid dimensions\n", millis());
    return false;
  }

  // Calculate bytes per pixel based on color type and bit depth
  int bytesPerPixel = 1;
  switch (colorType) {
    case 0:
      bytesPerPixel = 1;
      break;  // Grayscale
    case 2:
      bytesPerPixel = 3;
      break;  // RGB
    case 3:
      bytesPerPixel = 1;
      break;  // Indexed
    case 4:
      bytesPerPixel = 2;
      break;  // Grayscale + Alpha
    case 6:
      bytesPerPixel = 4;
      break;  // RGBA
    default:
      Serial.printf("[%lu] [PNG] Unsupported color type %d\n", millis(), colorType);
      return false;
  }

  // For 16-bit depth, double the bytes per component
  if (bitDepth == 16) {
    bytesPerPixel *= 2;
  }

  // Raw bytes per row (filter byte + pixel data)
  const int rawBytesPerRow = 1 + imgWidth * bytesPerPixel;

  // Read palette (PLTE) and transparency (tRNS) chunks, then concatenate IDAT data
  uint8_t* palette = nullptr;
  uint8_t* trns = nullptr;
  int trnsLen = 0;

  // Initialize inflate stream
  mz_stream inflateStream;
  memset(&inflateStream, 0, sizeof(inflateStream));
  if (mz_inflateInit(&inflateStream) != MZ_OK) {
    Serial.printf("[%lu] [PNG] Failed to initialize inflate\n", millis());
    return false;
  }

  // Allocate row buffers for PNG decoding
  auto* currentRow = static_cast<uint8_t*>(malloc(rawBytesPerRow));
  auto* prevRow = static_cast<uint8_t*>(calloc(rawBytesPerRow, 1));
  if (!currentRow || !prevRow) {
    Serial.printf("[%lu] [PNG] Failed to allocate row buffers\n", millis());
    mz_inflateEnd(&inflateStream);
    free(currentRow);
    free(prevRow);
    return false;
  }

  // Calculate output dimensions
  int outWidth = imgWidth;
  int outHeight = imgHeight;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  bool needsScaling = false;

  if (targetMaxWidth > 0 && targetMaxHeight > 0 && (imgWidth > targetMaxWidth || imgHeight > targetMaxHeight)) {
    const float scaleW = static_cast<float>(targetMaxWidth) / imgWidth;
    const float scaleH = static_cast<float>(targetMaxHeight) / imgHeight;
    const float scale = (scaleW < scaleH) ? scaleW : scaleH;

    outWidth = static_cast<int>(imgWidth * scale);
    outHeight = static_cast<int>(imgHeight * scale);
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;

    scaleX_fp = (static_cast<uint32_t>(imgWidth) << 16) / outWidth;
    scaleY_fp = (static_cast<uint32_t>(imgHeight) << 16) / outHeight;
    needsScaling = true;

    Serial.printf("[%lu] [PNG] Pre-scaling %dx%d -> %dx%d\n", millis(), imgWidth, imgHeight, outWidth, outHeight);
  }

  // Write BMP header
  writeBmpHeader1bit(bmpOut, outWidth, outHeight);

  const int bmpBytesPerRow = (outWidth + 31) / 32 * 4;
  auto* rowBuffer = static_cast<uint8_t*>(malloc(bmpBytesPerRow));
  if (!rowBuffer) {
    Serial.printf("[%lu] [PNG] Failed to allocate BMP row buffer\n", millis());
    mz_inflateEnd(&inflateStream);
    free(currentRow);
    free(prevRow);
    return false;
  }

  // Scaling accumulators
  uint32_t* rowAccum = nullptr;
  uint16_t* rowCount = nullptr;
  int currentOutY = 0;
  uint32_t nextOutY_srcStart = 0;

  if (needsScaling) {
    rowAccum = new (std::nothrow) uint32_t[outWidth]();
    rowCount = new (std::nothrow) uint16_t[outWidth]();
    if (!rowAccum || !rowCount) {
      Serial.printf("[%lu] [PNG] Failed to allocate scaling accumulators\n", millis());
      mz_inflateEnd(&inflateStream);
      free(currentRow);
      free(prevRow);
      free(rowBuffer);
      delete[] rowAccum;
      delete[] rowCount;
      return false;
    }
    nextOutY_srcStart = scaleY_fp;
  }

  // Ditherer
  auto* ditherer = new (std::nothrow) Atkinson1BitDitherer(outWidth);
  if (!ditherer) {
    Serial.printf("[%lu] [PNG] Failed to allocate ditherer\n", millis());
    mz_inflateEnd(&inflateStream);
    free(currentRow);
    free(prevRow);
    free(rowBuffer);
    delete[] rowAccum;
    delete[] rowCount;
    return false;
  }

  // Process PNG chunks - feed IDAT data to inflate, decode rows as they complete
  uint8_t idatReadBuf[1024];
  int inflatedPos = 0;  // Position within current row being inflated
  int srcY = 0;         // Current source row number
  bool success = true;
  bool reachedEnd = false;

  // Helper lambda to process a completed source row
  auto processRow = [&]() {
    // Un-filter the row
    const uint8_t filterType = currentRow[0];
    uint8_t* rowPixels = currentRow + 1;
    const uint8_t* prevPixels = prevRow + 1;
    const int bpp = (bytesPerPixel < 1) ? 1 : bytesPerPixel;  // filter byte stride

    switch (filterType) {
      case 0:  // None
        break;
      case 1:  // Sub
        for (int i = bpp; i < rawBytesPerRow - 1; i++) {
          rowPixels[i] += rowPixels[i - bpp];
        }
        break;
      case 2:  // Up
        for (int i = 0; i < rawBytesPerRow - 1; i++) {
          rowPixels[i] += prevPixels[i];
        }
        break;
      case 3:  // Average
        for (int i = 0; i < rawBytesPerRow - 1; i++) {
          const uint8_t a = (i >= bpp) ? rowPixels[i - bpp] : 0;
          const uint8_t b = prevPixels[i];
          rowPixels[i] += (a + b) / 2;
        }
        break;
      case 4:  // Paeth
        for (int i = 0; i < rawBytesPerRow - 1; i++) {
          const uint8_t a = (i >= bpp) ? rowPixels[i - bpp] : 0;
          const uint8_t b = prevPixels[i];
          const uint8_t c = (i >= bpp) ? prevPixels[i - bpp] : 0;
          rowPixels[i] += paethPredictor(a, b, c);
        }
        break;
      default:
        Serial.printf("[%lu] [PNG] Unknown filter type %d at row %d\n", millis(), filterType, srcY);
        break;
    }

    // Convert row pixels to grayscale
    // For sub-byte bit depths (1, 2, 4), unpack to 8-bit first
    if (!needsScaling) {
      memset(rowBuffer, 0, bmpBytesPerRow);
      for (int x = 0; x < imgWidth && x < outWidth; x++) {
        uint8_t gray;
        if (bitDepth < 8 && colorType == 3) {
          // Indexed with sub-byte packing
          const int pixelsPerByte = 8 / bitDepth;
          const int byteIdx = x / pixelsPerByte;
          const int bitIdx = (pixelsPerByte - 1 - (x % pixelsPerByte)) * bitDepth;
          const uint8_t mask = (1 << bitDepth) - 1;
          const uint8_t idx = (rowPixels[byteIdx] >> bitIdx) & mask;
          uint8_t indexedPixel = idx;
          gray = toGray(&indexedPixel, colorType, 1, palette, trns, trnsLen);
        } else if (bitDepth < 8 && colorType == 0) {
          // Grayscale with sub-byte packing
          const int pixelsPerByte = 8 / bitDepth;
          const int byteIdx = x / pixelsPerByte;
          const int bitIdx = (pixelsPerByte - 1 - (x % pixelsPerByte)) * bitDepth;
          const uint8_t mask = (1 << bitDepth) - 1;
          uint8_t val = (rowPixels[byteIdx] >> bitIdx) & mask;
          // Scale to 0-255
          gray = val * 255 / ((1 << bitDepth) - 1);
        } else if (bitDepth == 16) {
          // Take high byte of each 16-bit component
          const int srcBpp = bytesPerPixel / 2;  // Components count
          uint8_t pixel[4];
          for (int c = 0; c < srcBpp && c < 4; c++) {
            pixel[c] = rowPixels[x * bytesPerPixel + c * 2];  // High byte
          }
          gray = toGray(pixel, colorType, srcBpp, palette, trns, trnsLen);
        } else {
          gray = toGray(rowPixels + x * bytesPerPixel, colorType, bytesPerPixel, palette, trns, trnsLen);
        }
        const uint8_t bit = ditherer->processPixel(gray, x);
        const int byteIndex = x / 8;
        const int bitOffset = 7 - (x % 8);
        rowBuffer[byteIndex] |= (bit << bitOffset);
      }
      ditherer->nextRow();
      bmpOut.write(rowBuffer, bmpBytesPerRow);
    } else {
      // Prescaling path with area averaging
      for (int outX = 0; outX < outWidth; outX++) {
        const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
        const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;

        int sum = 0;
        int count = 0;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < imgWidth; srcX++) {
          uint8_t gray;
          if (bitDepth < 8 && colorType == 3) {
            const int pixelsPerByte = 8 / bitDepth;
            const int byteIdx = srcX / pixelsPerByte;
            const int bitIdx = (pixelsPerByte - 1 - (srcX % pixelsPerByte)) * bitDepth;
            const uint8_t mask = (1 << bitDepth) - 1;
            uint8_t idx = (rowPixels[byteIdx] >> bitIdx) & mask;
            gray = toGray(&idx, colorType, 1, palette, trns, trnsLen);
          } else if (bitDepth < 8 && colorType == 0) {
            const int pixelsPerByte = 8 / bitDepth;
            const int byteIdx = srcX / pixelsPerByte;
            const int bitIdx = (pixelsPerByte - 1 - (srcX % pixelsPerByte)) * bitDepth;
            const uint8_t mask = (1 << bitDepth) - 1;
            uint8_t val = (rowPixels[byteIdx] >> bitIdx) & mask;
            gray = val * 255 / ((1 << bitDepth) - 1);
          } else if (bitDepth == 16) {
            const int srcBpp = bytesPerPixel / 2;
            uint8_t pixel[4];
            for (int c = 0; c < srcBpp && c < 4; c++) {
              pixel[c] = rowPixels[srcX * bytesPerPixel + c * 2];
            }
            gray = toGray(pixel, colorType, srcBpp, palette, trns, trnsLen);
          } else {
            gray = toGray(rowPixels + srcX * bytesPerPixel, colorType, bytesPerPixel, palette, trns, trnsLen);
          }
          sum += gray;
          count++;
        }

        if (count == 0 && srcXStart < imgWidth) {
          uint8_t gray;
          if (bitDepth >= 8) {
            gray = toGray(rowPixels + srcXStart * bytesPerPixel, colorType, bytesPerPixel, palette, trns, trnsLen);
          } else {
            gray = 128;
          }
          sum = gray;
          count = 1;
        }

        rowAccum[outX] += sum;
        rowCount[outX] += count;
      }

      // Check if we've crossed into the next output row
      const uint32_t srcY_fp = static_cast<uint32_t>(srcY + 1) << 16;
      if (srcY_fp >= nextOutY_srcStart && currentOutY < outHeight) {
        memset(rowBuffer, 0, bmpBytesPerRow);
        for (int x = 0; x < outWidth; x++) {
          const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 255;
          const uint8_t bit = ditherer->processPixel(gray, x);
          const int byteIndex = x / 8;
          const int bitOffset = 7 - (x % 8);
          rowBuffer[byteIndex] |= (bit << bitOffset);
        }
        ditherer->nextRow();
        bmpOut.write(rowBuffer, bmpBytesPerRow);
        currentOutY++;

        memset(rowAccum, 0, outWidth * sizeof(uint32_t));
        memset(rowCount, 0, outWidth * sizeof(uint16_t));
        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
      }
    }

    // Swap current/prev row
    uint8_t* tmp = prevRow;
    prevRow = currentRow;
    currentRow = tmp;
    srcY++;
  };

  // Parse chunks and feed IDAT data to inflate
  while (!reachedEnd && success) {
    if (pngFile.read(chunkHeader, 8) != 8) {
      // If we've already decoded all rows, this is fine
      if (srcY >= imgHeight) break;
      Serial.printf("[%lu] [PNG] Unexpected end of file\n", millis());
      success = false;
      break;
    }

    chunkLen = readBE32(chunkHeader);
    type = chunkType(chunkHeader + 4);

    if (type == 0x504C5445) {  // "PLTE"
      if (chunkLen > 768) {
        Serial.printf("[%lu] [PNG] PLTE chunk too large\n", millis());
        success = false;
        break;
      }
      free(palette);
      palette = static_cast<uint8_t*>(malloc(chunkLen));
      if (!palette || pngFile.read(palette, chunkLen) != static_cast<int>(chunkLen)) {
        success = false;
        break;
      }
      pngFile.read(chunkHeader, 4);   // CRC
    } else if (type == 0x74524E53) {  // "tRNS"
      if (chunkLen > 256) {
        // Skip oversized tRNS
        pngFile.seekCur(chunkLen + 4);
        continue;
      }
      free(trns);
      trns = static_cast<uint8_t*>(malloc(chunkLen));
      trnsLen = chunkLen;
      if (!trns || pngFile.read(trns, chunkLen) != static_cast<int>(chunkLen)) {
        success = false;
        break;
      }
      pngFile.read(chunkHeader, 4);   // CRC
    } else if (type == 0x49444154) {  // "IDAT"
      // Feed compressed data to inflate in chunks
      uint32_t remaining = chunkLen;
      while (remaining > 0 && success) {
        const size_t toRead = (remaining > sizeof(idatReadBuf)) ? sizeof(idatReadBuf) : remaining;
        const int bytesRead = pngFile.read(idatReadBuf, toRead);
        if (bytesRead <= 0) {
          success = false;
          break;
        }
        remaining -= bytesRead;

        inflateStream.next_in = idatReadBuf;
        inflateStream.avail_in = bytesRead;

        while (inflateStream.avail_in > 0 && success) {
          inflateStream.next_out = currentRow + inflatedPos;
          inflateStream.avail_out = rawBytesPerRow - inflatedPos;

          const int ret = mz_inflate(&inflateStream, MZ_NO_FLUSH);
          if (ret != MZ_OK && ret != MZ_STREAM_END && ret != MZ_BUF_ERROR) {
            Serial.printf("[%lu] [PNG] Inflate error: %d\n", millis(), ret);
            success = false;
            break;
          }

          const int produced = (rawBytesPerRow - inflatedPos) - inflateStream.avail_out;
          inflatedPos += produced;

          // Complete row decoded
          if (inflatedPos >= rawBytesPerRow) {
            if (srcY < imgHeight) {
              processRow();
            }
            inflatedPos = 0;
          }

          if (ret == MZ_STREAM_END) {
            reachedEnd = true;
            break;
          }
        }
      }
      pngFile.read(chunkHeader, 4);   // CRC
    } else if (type == 0x49454E44) {  // "IEND"
      reachedEnd = true;
    } else {
      // Skip unknown chunks
      pngFile.seekCur(chunkLen + 4);  // data + CRC
    }
  }

  // Flush any remaining output rows for prescaling
  if (success && needsScaling && currentOutY < outHeight) {
    memset(rowBuffer, 0, bmpBytesPerRow);
    for (int x = 0; x < outWidth; x++) {
      const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 255;
      const uint8_t bit = ditherer->processPixel(gray, x);
      const int byteIndex = x / 8;
      const int bitOffset = 7 - (x % 8);
      rowBuffer[byteIndex] |= (bit << bitOffset);
    }
    ditherer->nextRow();
    bmpOut.write(rowBuffer, bmpBytesPerRow);
    currentOutY++;
  }

  // Cleanup
  mz_inflateEnd(&inflateStream);
  free(currentRow);
  free(prevRow);
  free(rowBuffer);
  free(palette);
  free(trns);
  delete[] rowAccum;
  delete[] rowCount;
  delete ditherer;

  if (success) {
    Serial.printf("[%lu] [PNG] Successfully converted PNG to 1-bit BMP (%dx%d)\n", millis(), outWidth, outHeight);
  }
  return success;
}
