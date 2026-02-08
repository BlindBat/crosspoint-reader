#pragma once

#include <EpdFontData.h>

#include <cstdint>
#include <string>

namespace ImageUtils {

// Convert an image file (JPEG or PNG, auto-detected) to a 1-bit BMP at outputBmpPath.
// Prescales to fit within maxWidth x maxHeight.
// Returns true on success.
bool convertImageToBmp(const std::string& inputImagePath, const std::string& outputBmpPath, int maxWidth,
                       int maxHeight);

// Read width and height from a BMP file header.
// Returns true on success.
bool readBmpDimensions(const std::string& bmpPath, uint16_t& width, uint16_t& height);

// Generate a minimal 1-bit BMP stub cover (plain white background).
// Returns true on success.
bool generateStubCoverBmp(const std::string& outputPath, int width, int height);

// Generate a decorated 1-bit BMP stub cover with double-line border and centered title/author text.
// Uses the provided font data for text rendering. Falls back to plain stub if fontData is null.
// Returns true on success.
bool generateDecoratedStubCoverBmp(const std::string& outputPath, int width, int height, const std::string& title,
                                   const std::string& author, const EpdFontData* fontData);

}  // namespace ImageUtils
