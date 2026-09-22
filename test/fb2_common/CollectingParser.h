#pragma once

// Fb2MetadataParser with a sink that collects chapters into a test-only vector, in
// chapter-number order, for suites that inspect the parsed chapter list directly.

#include <string>
#include <vector>

#include "Fb2/Fb2MetadataParser.h"

namespace fb2test {

// The parser under test with a sink that collects chapters into a test-only vector,
// in chapter-number order.
class CollectingParser {
 public:
  explicit CollectingParser(const std::string& path, const bool failWrites = false)
      : failWrites(failWrites), parser(path, Fb2ChapterSink{this, &reserve, &write}) {}
  bool parse() { return parser.parse(); }
  const std::vector<Fb2::SectionInfo>& getSections() const { return sections; }
  const std::string& getTitle() const { return parser.getTitle(); }
  const std::string& getAuthor() const { return parser.getAuthor(); }
  const std::string& getLanguage() const { return parser.getLanguage(); }
  const std::string& getCoverBinaryId() const { return parser.getCoverBinaryId(); }
  int writes = 0;

 private:
  static bool reserve(void* ctx) {
    static_cast<CollectingParser*>(ctx)->sections.emplace_back();
    return true;
  }
  static bool write(void* ctx, const uint16_t index, Fb2::SectionInfo& chapter) {
    auto* self = static_cast<CollectingParser*>(ctx);
    self->writes++;
    if (self->failWrites) return false;
    self->sections[index] = chapter;
    return true;
  }

  std::vector<Fb2::SectionInfo> sections;
  bool failWrites;
  Fb2MetadataParser parser;
};

}  // namespace fb2test

using fb2test::CollectingParser;
