#include "Fb2SectionParser.h"

#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <expat.h>

#include <cstring>

#include "Fb2XmlEncoding.h"

namespace {
constexpr size_t MIN_SIZE_FOR_POPUP = 50 * 1024;

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

const char* stripNs(const char* name) {
  const char* colon = strchr(name, ':');
  return colon ? colon + 1 : name;
}

// True when the element carries a name attribute (e.g. <body name="notes">).
bool hasNameAttribute(const char** atts) {
  if (!atts) return false;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], "name") == 0) return true;
  }
  return false;
}
}  // namespace

void Fb2SectionParser::flushPartWordBuffer() {
  const bool isBold = boldUntilDepth < depth;
  const bool isItalic = italicUntilDepth < depth;

  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }

  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues);
  partWordBufferIndex = 0;
  nextWordContinues = false;
}

void Fb2SectionParser::startNewTextBlock(const BlockStyle& blockStyle) {
  nextWordContinues = false;
  if (currentTextBlock) {
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setBlockStyle(
          currentTextBlock->getBlockStyle().getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      return;
    }
    makePages();
  }
  currentTextBlock = makeUniqueNoThrow<ParsedText>(spec.extraParagraphSpacing, spec.hyphenationEnabled,
                                                   spec.focusReadingEnabled, blockStyle);
  if (!currentTextBlock) {
    LOG_ERR("FB2", "OOM: text block");
    outOfMemory = true;
  }
}

void XMLCALL Fb2SectionParser::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<Fb2SectionParser*>(userData);
  const char* tag = stripNs(name);

  // If we've already passed our target section, skip everything
  if (self->pastTargetSection || self->outOfMemory) {
    self->depth++;
    return;
  }

  if (self->skipUntilDepth < self->depth) {
    self->depth++;
    return;
  }

  // Skip <description> and <binary> blocks during section parsing
  if (strcmp(tag, "description") == 0 || strcmp(tag, "binary") == 0) {
    self->skipUntilDepth = self->depth;
    self->depth++;
    return;
  }

  // Track body element. Bodies after the first with a name attribute
  // (name="notes"/"comments") are auxiliary per the FB2 convention and are
  // skipped, matching Fb2MetadataParser's section numbering.
  if (strcmp(tag, "body") == 0) {
    self->bodyCount++;
    if (self->bodyCount > 1 && hasNameAttribute(atts)) {
      self->skipUntilDepth = self->depth;
      self->depth++;
      return;
    }
    self->inBody = true;
    self->depth++;
    return;
  }

  // Track top-level sections within body
  if (strcmp(tag, "section") == 0 && self->inBody) {
    // Count only top-level sections (direct children of body), matching
    // Fb2MetadataParser's numbering. Sections nested inside an earlier
    // chapter are content of that chapter, not chapters of their own.
    if (self->sectionNesting == 0 && !self->inTargetSection) {
      if (self->topLevelSectionCount == self->targetSectionIndex) {
        self->inTargetSection = true;
        self->targetSectionDepth = self->depth;
      }
      self->topLevelSectionCount++;
    }
    // Nested sections inside the target section are just processed normally
    self->sectionNesting++;
    self->depth++;
    return;
  }

  // If targetSectionIndex is -1, process everything in body (single-section fallback)
  // Otherwise, only process content when inside the target section
  if (self->targetSectionIndex >= 0 && !self->inTargetSection) {
    self->depth++;
    return;
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  auto indentedBlockStyle = BlockStyle();
  indentedBlockStyle.marginLeft = 20;

  // FB2 content tags
  if (strcmp(tag, "p") == 0) {
    auto paragraphBlockStyle = BlockStyle();
    paragraphBlockStyle.textAlignDefined = true;
    const auto align = (self->spec.paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                           ? CssTextAlign::Justify
                           : static_cast<CssTextAlign>(self->spec.paragraphAlignment);
    paragraphBlockStyle.alignment = align;
    self->startNewTextBlock(paragraphBlockStyle);
  } else if (strcmp(tag, "title") == 0) {
    self->startNewTextBlock(centeredBlockStyle);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
  } else if (strcmp(tag, "subtitle") == 0) {
    self->startNewTextBlock(centeredBlockStyle);
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
  } else if (strcmp(tag, "epigraph") == 0) {
    auto epigraphStyle = BlockStyle();
    epigraphStyle.marginLeft = 30;
    epigraphStyle.textAlignDefined = true;
    epigraphStyle.alignment = CssTextAlign::Right;
    self->startNewTextBlock(epigraphStyle);
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
  } else if (strcmp(tag, "poem") == 0 || strcmp(tag, "stanza") == 0) {
    self->startNewTextBlock(centeredBlockStyle);
  } else if (strcmp(tag, "v") == 0) {
    // Verse line - start new text block for each line
    self->startNewTextBlock(centeredBlockStyle);
  } else if (strcmp(tag, "cite") == 0) {
    self->startNewTextBlock(indentedBlockStyle);
  } else if (strcmp(tag, "empty-line") == 0) {
    // Force a blank line
    auto emptyBlockStyle = BlockStyle();
    emptyBlockStyle.marginTop =
        static_cast<int16_t>(self->renderer.getLineHeight(self->spec.fontId, self->spec.lineCompression));
    self->startNewTextBlock(emptyBlockStyle);
  } else if (strcmp(tag, "strong") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
  } else if (strcmp(tag, "emphasis") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
  } else if (strcmp(tag, "strikethrough") == 0) {
    // No strikethrough rendering support, treat as regular text
  } else if (strcmp(tag, "image") == 0) {
    self->startNewTextBlock(centeredBlockStyle);
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    self->depth++;
    self->characterData(userData, "[Image]", 7);
    self->skipUntilDepth = self->depth - 1;
    return;
  } else if (strcmp(tag, "table") == 0) {
    self->startNewTextBlock(centeredBlockStyle);
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    self->depth++;
    self->characterData(userData, "[Table omitted]", 15);
    self->skipUntilDepth = self->depth - 1;
    return;
  } else if (strcmp(tag, "a") == 0) {
    // Hyperlinks - just render the text content
  } else if (strcmp(tag, "sup") == 0 || strcmp(tag, "sub") == 0) {
    // Superscript/subscript - render as regular text
  } else if (strcmp(tag, "code") == 0) {
    // Code blocks - render as regular text
  }

  self->depth++;
}

void XMLCALL Fb2SectionParser::characterData(void* userData, const char* s, const int len) {
  auto* self = static_cast<Fb2SectionParser*>(userData);

  if (self->pastTargetSection || self->outOfMemory || !self->currentTextBlock) {
    return;
  }

  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Only process text when inside the target section (or processing all body content)
  if (self->targetSectionIndex >= 0 && !self->inTargetSection) {
    return;
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->nextWordContinues = false;
      continue;
    }

    // Skip BOM
    const char FEFF_BYTE_1 = static_cast<char>(0xEF);
    const char FEFF_BYTE_2 = static_cast<char>(0xBB);
    const char FEFF_BYTE_3 = static_cast<char>(0xBF);
    if (s[i] == FEFF_BYTE_1 && (i + 2 < len) && s[i + 1] == FEFF_BYTE_2 && s[i + 2] == FEFF_BYTE_3) {
      i += 2;
      continue;
    }

    if (self->partWordBufferIndex >= FB2_MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // Split long text blocks to prevent memory issues
  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->spec.fontId, self->spec.viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock, uint32_t) { self->addLineToPage(textBlock); }, false);
  }
}

void XMLCALL Fb2SectionParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<Fb2SectionParser*>(userData);
  const char* tag = stripNs(name);

  if (self->pastTargetSection || self->outOfMemory) {
    self->depth--;
    return;
  }

  // Flush buffer before style changes on block-closing tags
  const bool processingContent = self->inTargetSection || self->targetSectionIndex < 0;
  if (processingContent && self->partWordBufferIndex > 0) {
    bool isBlock = strcmp(tag, "p") == 0 || strcmp(tag, "title") == 0 || strcmp(tag, "subtitle") == 0 ||
                   strcmp(tag, "epigraph") == 0 || strcmp(tag, "v") == 0 || strcmp(tag, "cite") == 0 ||
                   strcmp(tag, "poem") == 0 || strcmp(tag, "stanza") == 0 || strcmp(tag, "section") == 0;
    bool isInline = strcmp(tag, "strong") == 0 || strcmp(tag, "emphasis") == 0 || strcmp(tag, "a") == 0;

    if (isBlock || isInline || self->depth == 1) {
      self->flushPartWordBuffer();
      if (isInline) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth--;

  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Track closing of sections — check if we're leaving the target top-level section
  if (strcmp(tag, "section") == 0 && self->inBody) {
    if (self->sectionNesting > 0) {
      self->sectionNesting--;
    }
    // depth has already been decremented above; compare with the depth at which the target section opened
    if (self->inTargetSection && self->depth == self->targetSectionDepth) {
      self->inTargetSection = false;
      self->pastTargetSection = true;
    }
  }

  if (strcmp(tag, "body") == 0) {
    self->inBody = false;
  }
}

void Fb2SectionParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(spec.fontId, spec.lineCompression);

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      LOG_ERR("FB2", "OOM: page");
      outOfMemory = true;
      return;
    }
    currentPageNextY = 0;
  }

  if (currentPageNextY + lineHeight > spec.viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      LOG_ERR("FB2", "OOM: page");
      outOfMemory = true;
      return;
    }
    currentPageNextY = 0;
  }

  const int16_t xOffset = line->getBlockStyle().leftInset();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY += lineHeight;
}

void Fb2SectionParser::makePages() {
  if (!currentTextBlock) {
    return;
  }

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      LOG_ERR("FB2", "OOM: page");
      outOfMemory = true;
      return;
    }
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.getLineHeight(spec.fontId, spec.lineCompression);
  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();

  if (blockStyle.marginTop > 0) {
    currentPageNextY += blockStyle.marginTop;
  }
  if (blockStyle.paddingTop > 0) {
    currentPageNextY += blockStyle.paddingTop;
  }

  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth = (horizontalInset < spec.viewportWidth)
                                      ? static_cast<uint16_t>(spec.viewportWidth - horizontalInset)
                                      : spec.viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer, spec.fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock, uint32_t) { addLineToPage(textBlock); });

  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY += blockStyle.paddingBottom;
  }

  if (spec.extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

bool Fb2SectionParser::parseAndBuildPages() {
  auto paragraphBlockStyle = BlockStyle();
  paragraphBlockStyle.textAlignDefined = true;
  const auto align = (spec.paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                         ? CssTextAlign::Justify
                         : static_cast<CssTextAlign>(spec.paragraphAlignment);
  paragraphBlockStyle.alignment = align;
  startNewTextBlock(paragraphBlockStyle);
  if (outOfMemory) {
    return false;
  }

  XML_Parser xmlParser = XML_ParserCreate(nullptr);
  if (!xmlParser) {
    LOG_ERR("FB2", "Could not create parser for section");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("FB2", filepath, file)) {
    XML_ParserFree(xmlParser);
    return false;
  }

  if (popupFn && sectionLength >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  // For FB2, we parse the entire file but the section offsets help us identify content.
  // Since expat requires well-formed XML from the start, we parse the whole file.
  fb2RegisterExtraEncodings(xmlParser);
  XML_SetUserData(xmlParser, this);
  XML_SetElementHandler(xmlParser, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser, characterData);

  bool success = true;
  int done;
  do {
    void* const buf = XML_GetBuffer(xmlParser, 1024);
    if (!buf) {
      success = false;
      break;
    }

    const int len = file.read(buf, 1024);
    if (len <= 0 && file.available() > 0) {
      success = false;
      break;
    }

    done = file.available() == 0;

    if (XML_ParseBuffer(xmlParser, len, done) == XML_STATUS_ERROR) {
      LOG_ERR("FB2", "Section parse error: %s", XML_ErrorString(XML_GetErrorCode(xmlParser)));
      success = false;
      break;
    }

    // Stop early if we've finished parsing the target section or ran out of memory
    if (pastTargetSection || outOfMemory) {
      break;
    }
  } while (!done);

  XML_ParserFree(xmlParser);

  if (outOfMemory) {
    return false;
  }

  // Flush remaining content
  if (success && currentTextBlock) {
    makePages();
    if (currentPage) {
      completePageFn(std::move(currentPage));
      currentPage.reset();
    }
    currentTextBlock.reset();
  }

  return success && !outOfMemory;
}
