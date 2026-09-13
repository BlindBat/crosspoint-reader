#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Epub;
class GfxRenderer;

// Registry-backed stand-in for the pagination-LUT queries lib/Epub's Section
// offers ProgressMapper. Tests configure per-spine LUT data through
// SectionStubRegistry; the production code under test constructs Sections
// that read from it.
namespace SectionStubRegistry {

struct SpineLut {
  std::optional<uint16_t> cachedPageCount;
  // pageStartOffsets[i] is the first visible-text offset on page i. Empty
  // means "no offset LUT": getPageForVisibleTextOffset returns nullopt.
  std::vector<uint32_t> pageStartOffsets;
  std::map<uint16_t, uint16_t> paragraphPages;
  std::map<uint16_t, uint16_t> listItemPages;
  std::map<std::string, uint16_t> anchorPages;
  // Observability for tests.
  bool lastPreferFirstAtOffset = false;
  int visibleOffsetQueries = 0;
};

inline std::map<int, SpineLut>& all() {
  static std::map<int, SpineLut> registry;
  return registry;
}

inline SpineLut& forSpine(const int spineIndex) { return all()[spineIndex]; }

inline void reset() { all().clear(); }

}  // namespace SectionStubRegistry

class Section {
 public:
  Section(const std::shared_ptr<Epub>& /*epub*/, const int spineIndex, GfxRenderer& /*renderer*/)
      : spineIndex(spineIndex) {}

  std::optional<uint16_t> getCachedPageCount() const {
    return SectionStubRegistry::forSpine(spineIndex).cachedPageCount;
  }

  std::optional<uint16_t> getPageForVisibleTextOffset(const uint32_t offset,
                                                      const bool preferFirstAtOffset = false) const {
    auto& lut = SectionStubRegistry::forSpine(spineIndex);
    lut.visibleOffsetQueries++;
    lut.lastPreferFirstAtOffset = preferFirstAtOffset;
    if (lut.pageStartOffsets.empty()) return std::nullopt;
    uint16_t page = 0;
    for (size_t i = 0; i < lut.pageStartOffsets.size(); i++) {
      if (lut.pageStartOffsets[i] <= offset) page = static_cast<uint16_t>(i);
    }
    return page;
  }

  std::optional<uint16_t> getPageForParagraphIndex(const uint16_t pIndex) const {
    return lookup(SectionStubRegistry::forSpine(spineIndex).paragraphPages, pIndex);
  }

  std::optional<uint16_t> getPageForListItemIndex(const uint16_t liIndex) const {
    return lookup(SectionStubRegistry::forSpine(spineIndex).listItemPages, liIndex);
  }

  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const {
    const auto& pages = SectionStubRegistry::forSpine(spineIndex).anchorPages;
    const auto it = pages.find(anchor);
    if (it == pages.end()) return std::nullopt;
    return it->second;
  }

 private:
  static std::optional<uint16_t> lookup(const std::map<uint16_t, uint16_t>& pages, const uint16_t key) {
    const auto it = pages.find(key);
    if (it == pages.end()) return std::nullopt;
    return it->second;
  }

  int spineIndex;
};
