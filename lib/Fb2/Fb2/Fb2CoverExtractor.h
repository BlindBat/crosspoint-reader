#pragma once

#include <string>

// Extracts and converts cover images from FB2 files.
// FB2 stores images as base64-encoded data in <binary> elements.
class Fb2CoverExtractor {
  std::string filepath;
  std::string binaryId;
  std::string outputBmpPath;

  // Extract base64 data for the specified binary ID to a temp JPEG file
  bool extractBinaryToJpeg(const std::string& tempJpegPath) const;

 public:
  explicit Fb2CoverExtractor(const std::string& filepath, const std::string& binaryId, const std::string& outputBmpPath)
      : filepath(filepath), binaryId(binaryId), outputBmpPath(outputBmpPath) {}

  // Extract cover and convert to BMP at outputBmpPath
  bool extract() const;

  // Extract cover and convert to 1-bit thumbnail BMP
  bool extractThumb(const std::string& thumbPath, int height) const;

  // Extract a <binary> element by ID to a file on SD card (reusable for inline images)
  static bool extractBinaryToFile(const std::string& fb2Path, const std::string& binaryId,
                                  const std::string& outputPath);

  // Extract a <binary> element by direct file seek (avoids full XML re-parsing)
  // fileOffset is the byte offset of the <binary> tag start, recorded during metadata parsing.
  static bool extractBinaryByOffset(const std::string& fb2Path, size_t fileOffset, const std::string& outputPath);
};
