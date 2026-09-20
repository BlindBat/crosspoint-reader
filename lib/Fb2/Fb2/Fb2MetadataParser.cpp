#include "Fb2MetadataParser.h"

#include <HalStorage.h>
#include <Logging.h>
#include <expat.h>

#include <cstring>

#include "Fb2XmlEncoding.h"

namespace {
// Strip namespace prefix from tag name (e.g., "l:title" -> "title")
const char* stripNs(const char* name) {
  const char* colon = strchr(name, ':');
  return colon ? colon + 1 : name;
}

// Extract attribute value from xlink:href="#id" -> "id"
std::string getXlinkHref(const char** atts) {
  if (!atts) return "";
  for (int i = 0; atts[i]; i += 2) {
    const char* attrName = stripNs(atts[i]);
    if (strcmp(attrName, "href") == 0) {
      const char* val = atts[i + 1];
      if (val[0] == '#') return std::string(val + 1);
      return std::string(val);
    }
  }
  return "";
}

// Collapses internal whitespace runs to single spaces, strips the ends, and cuts
// the result to FB2_MAX_LABEL_CHARS codepoints (never mid-character). Used only for
// a derived label: a <p> is prose, so without this a label could be a paragraph.
void normalizeLabel(std::string& text) {
  std::string out;
  out.reserve(text.size() < 256 ? text.size() : 256);
  bool pendingSpace = false;
  size_t chars = 0;
  for (const char rawChar : text) {
    const unsigned char byte = static_cast<unsigned char>(rawChar);
    if (byte <= ' ') {
      pendingSpace = !out.empty();
      continue;
    }
    const bool isContinuation = (byte & 0xC0) == 0x80;
    if (!isContinuation) {
      if (chars >= Fb2::FB2_MAX_LABEL_CHARS) break;
      if (pendingSpace) {
        out += ' ';
        chars++;
        pendingSpace = false;
        if (chars >= Fb2::FB2_MAX_LABEL_CHARS) break;
      }
      chars++;
    }
    out += rawChar;
  }
  text.swap(out);
}

// True when the element carries a name attribute (e.g. <body name="notes">).
bool hasNameAttribute(const char** atts) {
  if (!atts) return false;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], "name") == 0) return true;
  }
  return false;
}
constexpr size_t LABEL_SCAN_BYTES = Fb2::FB2_MAX_LABEL_CHARS * 4;
}  // namespace

void Fb2MetadataParser::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<Fb2MetadataParser*>(userData);
  const char* tag = stripNs(name);
  const int depth = self->elemDepth++;  // depth of this element

  if (strcmp(tag, "title-info") == 0 && !self->inBody) {
    self->inTitleInfo = true;
    return;
  }

  if (self->inTitleInfo) {
    if (strcmp(tag, "book-title") == 0) {
      self->context = Context::BOOK_TITLE;
      self->charBuffer.clear();
    } else if (strcmp(tag, "author") == 0) {
      self->inAuthor = true;
      self->authorFirstName.clear();
      self->authorMiddleName.clear();
      self->authorLastName.clear();
    } else if (self->inAuthor && strcmp(tag, "first-name") == 0) {
      self->context = Context::AUTHOR_FIRST_NAME;
      self->charBuffer.clear();
    } else if (self->inAuthor && strcmp(tag, "middle-name") == 0) {
      self->context = Context::AUTHOR_MIDDLE_NAME;
      self->charBuffer.clear();
    } else if (self->inAuthor && strcmp(tag, "last-name") == 0) {
      self->context = Context::AUTHOR_LAST_NAME;
      self->charBuffer.clear();
    } else if (strcmp(tag, "lang") == 0) {
      self->context = Context::LANG;
      self->charBuffer.clear();
    } else if (strcmp(tag, "coverpage") == 0) {
      self->context = Context::COVERPAGE;
    } else if (self->context == Context::COVERPAGE && strcmp(tag, "image") == 0) {
      self->coverBinaryId = getXlinkHref(atts);
    }
    return;
  }

  if (strcmp(tag, "body") == 0 && !self->inBody) {
    self->bodyCount++;
    // FB2 convention: bodies after the first carry a name attribute
    // (name="notes"/"comments") and hold auxiliary content, not reading
    // chapters. Leaving inBody unset skips their sections entirely.
    if (self->bodyCount > 1 && hasNameAttribute(atts)) {
      return;
    }
    self->inBody = true;
    // ponytail: a body's own <title>/<epigraph>, ahead of its first <section>,
    // is in no chapter's length, so it carries no progress weight (it reads
    // with the first chapter — see Fb2SectionParser::inBodyPrefix). It is a few
    // hundred bytes of a whole body; give it its own chapter if that ever shows.
    return;
  }

  if (self->inBody) {
    if (strcmp(tag, "section") == 0) {
      // Every section is a chapter, at any depth, numbered in start-tag order.
      // Past FB2_MAX_CHAPTERS a section is no longer a boundary: it reads as
      // part of the chapter containing it, so no text is lost.
      const bool isChapter = self->sections.size() < Fb2::FB2_MAX_CHAPTERS;
      const size_t startOffset = static_cast<size_t>(XML_GetCurrentByteIndex(static_cast<XML_Parser>(self->parser)));
      size_t entryIndex = NOT_A_CHAPTER;
      if (isChapter) {
        Fb2::SectionInfo info;
        // Untitled sections keep an empty title; the UI substitutes the
        // localized "Unnamed" label at display time.
        info.fileOffset = startOffset;
        const size_t level = self->openSections.size();
        info.level = static_cast<uint8_t>(level > 255 ? 255 : level);
        entryIndex = self->sections.size();
        self->sections.push_back(std::move(info));
      }
      self->openSections.push_back({entryIndex, startOffset, 0, depth, false});
      self->inSectionTitle = false;
    } else if (strcmp(tag, "title") == 0 && !self->openSections.empty() &&
               depth == self->openSections.back().elemDepth + 1 &&
               self->openSections.back().entryIndex != NOT_A_CHAPTER) {
      // Only a <title> that is a DIRECT child of the section is its title; a
      // <poem><title> or a child section's title is not. A real title always wins
      // over a label derived from the section's first paragraph.
      auto& entry = self->sections[self->openSections.back().entryIndex];
      if (entry.titleDerived) {
        entry.title.clear();
        entry.titleDerived = 0;
      }
      self->inSectionTitle = true;
      self->charBuffer.clear();
    } else if (strcmp(tag, "p") == 0 && self->inSectionTitle) {
      self->context = Context::SECTION_TITLE_P;
      self->charBuffer.clear();
    } else if (strcmp(tag, "p") == 0 && !self->openSections.empty() &&
               self->openSections.back().entryIndex != NOT_A_CHAPTER && !self->openSections.back().labelTaken &&
               self->sections[self->openSections.back().entryIndex].title.empty()) {
      // A title-less section borrows its own first paragraph as a chapter-list
      // label, at whatever depth it sits: real books routinely open a section with
      // an <epigraph> or <cite> rather than a bare <p>. It is still the section's
      // OWN text, because a <p> inside a child <section> belongs to that child
      // (openSections.back() is the innermost open section), and a <p> inside
      // <title> is claimed by the title branch above.
      self->context = Context::SECTION_LABEL_P;
      self->charBuffer.clear();
    }
  }
}

void Fb2MetadataParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<Fb2MetadataParser*>(userData);
  const char* tag = stripNs(name);
  if (self->elemDepth > 0) self->elemDepth--;
  const int depth = self->elemDepth;  // depth of this element

  if (strcmp(tag, "title-info") == 0 && self->inTitleInfo) {
    self->inTitleInfo = false;
    self->context = Context::NONE;
    return;
  }

  if (self->inTitleInfo) {
    if (strcmp(tag, "book-title") == 0 && self->context == Context::BOOK_TITLE) {
      self->title = self->charBuffer;
      self->context = Context::NONE;
    } else if (strcmp(tag, "author") == 0 && self->inAuthor) {
      self->inAuthor = false;
      // Build author string: "First Middle Last"
      std::string fullAuthor;
      if (!self->authorFirstName.empty()) {
        fullAuthor = self->authorFirstName;
      }
      if (!self->authorMiddleName.empty()) {
        if (!fullAuthor.empty()) fullAuthor += " ";
        fullAuthor += self->authorMiddleName;
      }
      if (!self->authorLastName.empty()) {
        if (!fullAuthor.empty()) fullAuthor += " ";
        fullAuthor += self->authorLastName;
      }
      if (!fullAuthor.empty() && self->author.empty()) {
        self->author = fullAuthor;
      }
      self->context = Context::NONE;
    } else if (self->inAuthor && strcmp(tag, "first-name") == 0 && self->context == Context::AUTHOR_FIRST_NAME) {
      self->authorFirstName = self->charBuffer;
      self->context = Context::NONE;
    } else if (self->inAuthor && strcmp(tag, "middle-name") == 0 && self->context == Context::AUTHOR_MIDDLE_NAME) {
      self->authorMiddleName = self->charBuffer;
      self->context = Context::NONE;
    } else if (self->inAuthor && strcmp(tag, "last-name") == 0 && self->context == Context::AUTHOR_LAST_NAME) {
      self->authorLastName = self->charBuffer;
      self->context = Context::NONE;
    } else if (strcmp(tag, "lang") == 0 && self->context == Context::LANG) {
      self->language = self->charBuffer;
      self->context = Context::NONE;
    } else if (strcmp(tag, "coverpage") == 0) {
      self->context = Context::NONE;
    }
    return;
  }

  if (self->inBody) {
    if (strcmp(tag, "title") == 0 && self->inSectionTitle && !self->openSections.empty() &&
        depth == self->openSections.back().elemDepth + 1) {
      self->inSectionTitle = false;
      self->context = Context::NONE;
    } else if (strcmp(tag, "p") == 0 && self->context == Context::SECTION_TITLE_P) {
      // Append paragraph text to the open section's own title.
      if (!self->openSections.empty() && self->openSections.back().entryIndex != NOT_A_CHAPTER) {
        std::string& title = self->sections[self->openSections.back().entryIndex].title;
        if (!title.empty() && !self->charBuffer.empty()) {
          title += " ";
        }
        title += self->charBuffer;
      }
      self->charBuffer.clear();
      self->context = Context::NONE;
    } else if (strcmp(tag, "p") == 0 && self->context == Context::SECTION_LABEL_P) {
      if (!self->openSections.empty() && self->openSections.back().entryIndex != NOT_A_CHAPTER) {
        self->openSections.back().labelTaken = true;
        normalizeLabel(self->charBuffer);
        if (!self->charBuffer.empty()) {
          auto& entry = self->sections[self->openSections.back().entryIndex];
          entry.title = self->charBuffer;
          entry.titleDerived = 1;
        }
      }
      self->charBuffer.clear();
      self->context = Context::NONE;
    } else if (strcmp(tag, "section") == 0) {
      if (!self->openSections.empty()) {
        const OpenSection closed = self->openSections.back();
        self->openSections.pop_back();

        // expat reports the start of the end tag; add its own length.
        const size_t endOffset =
            static_cast<size_t>(XML_GetCurrentByteIndex(static_cast<XML_Parser>(self->parser))) + strlen(name) + 3;
        const size_t span = endOffset > closed.startOffset ? endOffset - closed.startOffset : 0;

        if (closed.entryIndex != NOT_A_CHAPTER) {
          // Own bytes only: the child chapters' spans belong to those chapters,
          // so the chapter lengths partition the body instead of overlapping.
          self->sections[closed.entryIndex].length = span > closed.childBytes ? span - closed.childBytes : 0;
          if (!self->openSections.empty()) {
            self->openSections.back().childBytes += span;
          }
        }
        // A section past the cap is not a chapter, so its bytes stay with the
        // enclosing chapter and are deliberately not subtracted above.
      }
      self->inSectionTitle = false;
    } else if (strcmp(tag, "body") == 0) {
      self->inBody = false;
      // Unbalanced markup can leave sections open; a body never spans another.
      self->openSections.clear();
    }
  }
}

void Fb2MetadataParser::characterData(void* userData, const char* s, int len) {
  auto* self = static_cast<Fb2MetadataParser*>(userData);

  if (self->context == Context::BOOK_TITLE || self->context == Context::AUTHOR_FIRST_NAME ||
      self->context == Context::AUTHOR_MIDDLE_NAME || self->context == Context::AUTHOR_LAST_NAME ||
      self->context == Context::LANG || self->context == Context::SECTION_TITLE_P) {
    self->charBuffer.append(s, len);
  } else if (self->context == Context::SECTION_LABEL_P && self->charBuffer.size() < LABEL_SCAN_BYTES) {
    // Bounded: normalizeLabel cuts to FB2_MAX_LABEL_CHARS codepoints, and 4 bytes
    // per codepoint is the UTF-8 worst case, so nothing longer can matter.
    self->charBuffer.append(s, len);
  }
}

bool Fb2MetadataParser::parse() {
  XML_Parser xmlParser = XML_ParserCreate(nullptr);
  if (!xmlParser) {
    LOG_ERR("FB2", "Could not create XML parser");
    return false;
  }

  parser = xmlParser;

  fb2RegisterExtraEncodings(xmlParser);
  XML_SetUserData(xmlParser, this);
  XML_SetElementHandler(xmlParser, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser, characterData);

  HalFile file;
  if (!Storage.openFileForRead("FB2", filepath, file)) {
    XML_ParserFree(xmlParser);
    parser = nullptr;
    return false;
  }

  bool success = true;
  int done;
  do {
    void* const buf = XML_GetBuffer(xmlParser, 1024);
    if (!buf) {
      LOG_ERR("FB2", "Could not allocate parser buffer");
      success = false;
      break;
    }

    const int len = file.read(buf, 1024);
    if (len <= 0 && file.available() > 0) {
      LOG_ERR("FB2", "File read error");
      success = false;
      break;
    }

    done = file.available() == 0;

    if (XML_ParseBuffer(xmlParser, len, done) == XML_STATUS_ERROR) {
      LOG_ERR("FB2", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(xmlParser),
              XML_ErrorString(XML_GetErrorCode(xmlParser)));
      success = false;
      break;
    }
  } while (!done);

  XML_ParserFree(xmlParser);
  parser = nullptr;
  file.close();

  // If no sections found, treat entire body as one section
  if (success && sections.empty()) {
    LOG_DBG("FB2", "No sections found, treating entire file as one section");
    // Re-parse is too expensive; create a dummy section covering the whole file.
    // The book title stands in for the section title (empty titles display as "Unnamed").
    HalFile sizeFile;
    if (Storage.openFileForRead("FB2", filepath, sizeFile)) {
      Fb2::SectionInfo info;
      info.title = title;
      info.fileOffset = 0;
      info.length = sizeFile.size();
      info.level = 0;
      sections.push_back(std::move(info));
    }
  }

  // sections grew by doubling, so it can hold up to 2x the capacity it needs; the
  // book keeps this vector for as long as it is open, so give the slack back.
  sections.shrink_to_fit();

  return success;
}
