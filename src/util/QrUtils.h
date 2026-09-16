#pragma once

#include <GfxRenderer.h>

#include <cstddef>
#include <string>

#include "components/themes/BaseTheme.h"

namespace QrUtils {

// QR version used to encode a payload of `len` bytes at ECC_LOW in byte mode.
// Coarse thresholds, not the encoder's exact capacities.
inline int versionForLength(const size_t len) {
  int version = 4;
  if (len > 114) version = 10;
  if (len > 395) version = 20;
  if (len > 1066) version = 30;
  if (len > 2110) version = 40;
  return version;
}

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils
