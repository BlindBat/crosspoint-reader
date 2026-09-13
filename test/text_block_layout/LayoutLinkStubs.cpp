// Link-time stubs for symbols the compiled production TUs reference but this
// suite does not exercise. Hyphenation is REAL here (Hyphenator + Liang
// patterns are compiled from production sources); only BiDi reordering is
// stubbed to identity because every fixture paragraph is LTR (MiniBidi has its
// own dedicated suite, test/minibidi_arabic).

#include <BidiUtils.h>

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
