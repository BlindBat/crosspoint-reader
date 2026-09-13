// Link-time stand-ins for symbols the compiled production sources reference
// but the suite never drives (pattern: chapter_html_slim_parser's
// ParserLinkStubs.cpp). SettingsList.h emits calls into SdCardFontRegistry
// for the SD-font-aware settings entries; every test runs with a null
// registry, so these bodies are unreachable.

#include <SdCardFontRegistry.h>

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }
