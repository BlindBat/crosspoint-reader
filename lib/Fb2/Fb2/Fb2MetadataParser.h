#pragma once

#include <string>
#include <vector>

#include "Fb2.h"

// Streaming expat-based parser for FB2 metadata.
// First pass: extracts title, author, language, cover ID from <description>,
// and scans <body> to identify section boundaries and titles with file offsets.
class Fb2MetadataParser {
  std::string filepath;

  // Metadata
  std::string title;
  std::string author;
  std::string language;
  std::string coverBinaryId;

  // Section scanning
  std::vector<Fb2::SectionInfo> sections;

  // Parser state
  enum class Context {
    NONE,
    TITLE_INFO,
    BOOK_TITLE,
    AUTHOR_FIRST_NAME,
    AUTHOR_MIDDLE_NAME,
    AUTHOR_LAST_NAME,
    LANG,
    COVERPAGE,
    BODY,
    SECTION_TITLE,
    SECTION_TITLE_P,
    SECTION_LABEL_P,
    BINARY_SCAN
  };
  Context context = Context::NONE;
  int bodyCount = 0;
  int elemDepth = 0;
  bool inBody = false;
  bool inTitleInfo = false;
  bool inAuthor = false;
  std::string charBuffer;
  std::string authorFirstName;
  std::string authorMiddleName;
  std::string authorLastName;
  bool inSectionTitle = false;

  // One entry per currently open <section>. Chapters are pushed to `sections`
  // at their start tag (so the vector is in document order) and their own byte
  // length is filled in at the end tag: full span minus the spans of the child
  // chapters, which are chapters of their own.
  struct OpenSection {
    size_t entryIndex;   // index into `sections`, or NOT_A_CHAPTER past the cap
    size_t startOffset;  // byte offset of its "<section" start tag
    size_t childBytes;   // bytes claimed by child chapters
    int elemDepth;       // element depth of the <section> itself
    bool labelTaken;     // its first direct <p> has already been offered as a label
  };
  static constexpr size_t NOT_A_CHAPTER = static_cast<size_t>(-1);
  std::vector<OpenSection> openSections;

  // Track byte offset from expat
  void* parser = nullptr;

  static void startElement(void* userData, const char* name, const char** atts);
  static void endElement(void* userData, const char* name);
  static void characterData(void* userData, const char* s, int len);

 public:
  explicit Fb2MetadataParser(const std::string& filepath) : filepath(filepath) {
    // FB2 sections nest two or three deep in practice; reserve once so the
    // stack never reallocates mid-parse.
    openSections.reserve(8);
  }
  bool parse();

  const std::string& getTitle() const { return title; }
  const std::string& getAuthor() const { return author; }
  const std::string& getLanguage() const { return language; }
  const std::string& getCoverBinaryId() const { return coverBinaryId; }
  const std::vector<Fb2::SectionInfo>& getSections() const { return sections; }
  // Hands the chapter list over instead of copying it: a copy would duplicate
  // every chapter title on the heap.
  std::vector<Fb2::SectionInfo>&& takeSections() { return std::move(sections); }
};
