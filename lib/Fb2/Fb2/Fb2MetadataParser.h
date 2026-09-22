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

  // Section scanning: chapters go to the sink as they complete, so the parser holds
  // nothing per chapter beyond the currently open sections.
  Fb2ChapterSink sink;
  uint16_t chapterCount = 0;
  bool sinkFailed = false;

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

  // One entry per currently open <section>. A chapter's slot is reserved in the
  // sink at its start tag (so chapters are numbered in document order) and the
  // chapter is written at its end tag, once its own byte length is known: full
  // span minus the spans of the child chapters, which are chapters of their own.
  struct OpenSection {
    Fb2::SectionInfo info;    // the chapter being built; unused when !isChapter
    size_t startOffset = 0;   // byte offset of its "<section" start tag
    size_t childBytes = 0;    // bytes claimed by child chapters
    int elemDepth = 0;        // element depth of the <section> itself
    uint16_t index = 0;       // chapter number
    bool isChapter = false;   // false past the chapter limit
    bool labelTaken = false;  // its first direct <p> has already been offered as a label
  };
  std::vector<OpenSection> openSections;

  // Track byte offset from expat
  void* parser = nullptr;

  void stopForSink();
  static void startElement(void* userData, const char* name, const char** atts);
  static void endElement(void* userData, const char* name);
  static void characterData(void* userData, const char* s, int len);

 public:
  Fb2MetadataParser(const std::string& filepath, const Fb2ChapterSink& sink) : filepath(filepath), sink(sink) {
    // FB2 sections nest two or three deep in practice; reserve once so the
    // stack never reallocates mid-parse.
    openSections.reserve(8);
  }
  bool parse();

  const std::string& getTitle() const { return title; }
  const std::string& getAuthor() const { return author; }
  const std::string& getLanguage() const { return language; }
  const std::string& getCoverBinaryId() const { return coverBinaryId; }
  uint16_t getChapterCount() const { return chapterCount; }
};
