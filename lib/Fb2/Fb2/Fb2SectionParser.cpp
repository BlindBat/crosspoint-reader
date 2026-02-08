#include "Fb2SectionParser.h"

#include <Epub/ImageUtils.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <expat.h>

#include <cstring>

#include "Fb2CoverExtractor.h"

namespace {
constexpr size_t MIN_SIZE_FOR_POPUP = 50 * 1024;

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

const char* stripNs(const char* name) {
  const char* colon = strchr(name, ':');
  return colon ? colon + 1 : name;
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
      currentTextBlock->setBlockStyle(currentTextBlock->getBlockStyle().getCombinedBlockStyle(blockStyle));
      return;
    }
    makePages();
  }
  currentTextBlock.reset(new ParsedText(extraParagraphSpacing, hyphenationEnabled, blockStyle));
}

void XMLCALL Fb2SectionParser::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<Fb2SectionParser*>(userData);
  const char* tag = stripNs(name);

  // If we've already passed our target section, skip everything
  if (self->pastTargetSection) {
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

  // Track body element
  if (strcmp(tag, "body") == 0) {
    self->inBody = true;
    self->depth++;
    if (self->bodyContentDepth < 0) {
      self->bodyContentDepth = self->depth;
    }
    return;
  }

  // Track top-level sections within body
  if (strcmp(tag, "section") == 0 && self->inBody) {
    // Only count sections at body's direct-child depth (matching Fb2MetadataParser).
    // Nested sections inside non-target sections must not be counted.
    const bool isTopLevel = (self->bodyContentDepth >= 0 && self->depth == self->bodyContentDepth);
    if (isTopLevel && !self->inTargetSection) {
      if (self->topLevelSectionCount == self->targetSectionIndex) {
        self->inTargetSection = true;
        self->targetSectionDepth = self->depth;
      }
      self->topLevelSectionCount++;
    }
    // Nested sections inside target section are just processed normally
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
    const auto align = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                           ? CssTextAlign::Justify
                           : static_cast<CssTextAlign>(self->paragraphAlignment);
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
        static_cast<int16_t>(self->renderer.getLineHeight(self->fontId) * self->lineCompression);
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
    // Extract xlink:href or l:href attribute
    std::string imageRef;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        const char* attrName = stripNs(atts[i]);
        if (strcmp(attrName, "href") == 0) {
          imageRef = atts[i + 1];
          break;
        }
      }
    }

    // Try to render actual image
    bool imageRendered = false;
    if (!imageRef.empty() && !self->imageCacheDir.empty()) {
      // Strip leading '#' from href (FB2 binary references)
      if (!imageRef.empty() && imageRef[0] == '#') {
        imageRef = imageRef.substr(1);
      }

      const std::string bmpPath = self->imageCacheDir + "/" + imageRef + ".bmp";

      // Convert image to BMP if not already cached
      if (!SdMan.exists(bmpPath.c_str())) {
        const std::string tmpImagePath = self->imageCacheDir + "/.tmp_img";
        bool extracted = false;

        // Use direct offset-based extraction if available (avoids full XML re-parsing)
        if (self->binaryOffsets) {
          auto it = self->binaryOffsets->find(imageRef);
          if (it != self->binaryOffsets->end()) {
            extracted = Fb2CoverExtractor::extractBinaryByOffset(self->filepath, it->second, tmpImagePath);
          }
        }

        // Fall back to full XML parsing if offset not available
        if (!extracted) {
          extracted = Fb2CoverExtractor::extractBinaryToFile(self->filepath, imageRef, tmpImagePath);
        }

        if (extracted) {
          ImageUtils::convertImageToBmp(tmpImagePath, bmpPath, self->viewportWidth, self->viewportHeight);
          SdMan.remove(tmpImagePath.c_str());
        }
      }

      // Read BMP dimensions and add to page
      uint16_t imgW, imgH;
      if (SdMan.exists(bmpPath.c_str()) && ImageUtils::readBmpDimensions(bmpPath, imgW, imgH)) {
        self->addImageToPage(bmpPath, imgW, imgH);
        imageRendered = true;
      }
    }

    // Fallback to placeholder
    if (!imageRendered) {
      self->startNewTextBlock(centeredBlockStyle);
      self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
      self->depth++;
      self->characterData(userData, "[Image]", 7);
      self->skipUntilDepth = self->depth - 1;
      return;
    }

    self->skipUntilDepth = self->depth;
    self->depth++;
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

  if (self->pastTargetSection) {
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
        self->renderer, self->fontId, self->viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

void XMLCALL Fb2SectionParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<Fb2SectionParser*>(userData);
  const char* tag = stripNs(name);

  if (self->pastTargetSection) {
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
  if (strcmp(tag, "section") == 0 && self->inBody && self->inTargetSection) {
    // depth has already been decremented above; compare with the depth at which the target section opened
    if (self->depth == self->targetSectionDepth) {
      self->inTargetSection = false;
      self->pastTargetSection = true;
    }
  }

  if (strcmp(tag, "body") == 0) {
    self->inBody = false;
  }
}

void Fb2SectionParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (currentPageNextY + lineHeight > viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
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
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();

  if (blockStyle.marginTop > 0) {
    currentPageNextY += blockStyle.marginTop;
  }
  if (blockStyle.paddingTop > 0) {
    currentPageNextY += blockStyle.paddingTop;
  }

  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY += blockStyle.paddingBottom;
  }

  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

void Fb2SectionParser::addImageToPage(const std::string& bmpPath, uint16_t width, uint16_t height) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  // Flush current text block
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }

  // Reset text block for content after the image
  auto paragraphBlockStyle = BlockStyle();
  paragraphBlockStyle.textAlignDefined = true;
  const auto align = (paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                         ? CssTextAlign::Justify
                         : static_cast<CssTextAlign>(paragraphAlignment);
  paragraphBlockStyle.alignment = align;
  currentTextBlock.reset(new ParsedText(extraParagraphSpacing, hyphenationEnabled, paragraphBlockStyle));

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  // Clamp to viewport dimensions
  uint16_t displayW = width;
  uint16_t displayH = height;
  if (displayW > viewportWidth) {
    displayH = static_cast<uint16_t>(static_cast<float>(height) * viewportWidth / width);
    displayW = viewportWidth;
  }
  if (displayH > viewportHeight) {
    displayW = static_cast<uint16_t>(static_cast<float>(width) * viewportHeight / height);
    displayH = viewportHeight;
  }

  // Center horizontally
  const int16_t xPos = static_cast<int16_t>((viewportWidth - displayW) / 2);

  // Check if fits on current page
  if (currentPageNextY + displayH > viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  currentPage->elements.push_back(std::make_shared<PageImage>(bmpPath, displayW, displayH, xPos, currentPageNextY));
  currentPageNextY += displayH + lineHeight / 2;
}

bool Fb2SectionParser::parseAndBuildPages() {
  auto paragraphBlockStyle = BlockStyle();
  paragraphBlockStyle.textAlignDefined = true;
  const auto align = (paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                         ? CssTextAlign::Justify
                         : static_cast<CssTextAlign>(paragraphAlignment);
  paragraphBlockStyle.alignment = align;
  startNewTextBlock(paragraphBlockStyle);

  XML_Parser xmlParser = XML_ParserCreate(nullptr);
  if (!xmlParser) {
    Serial.printf("[%lu] [FB2] Could not create parser for section\n", millis());
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("FB2", filepath, file)) {
    XML_ParserFree(xmlParser);
    return false;
  }

  if (popupFn && sectionLength >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  // For FB2, we parse the entire file but the section offsets help us identify content.
  // Since expat requires well-formed XML from the start, we parse the whole file.
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

    const size_t len = file.read(buf, 1024);
    if (len == 0 && file.available() > 0) {
      success = false;
      break;
    }

    done = file.available() == 0;

    if (XML_ParseBuffer(xmlParser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [FB2] Section parse error: %s\n", millis(), XML_ErrorString(XML_GetErrorCode(xmlParser)));
      success = false;
      break;
    }

    // Stop early if we've finished parsing the target section
    if (pastTargetSection) {
      break;
    }
  } while (!done);

  XML_ParserFree(xmlParser);
  file.close();

  // Flush remaining content
  if (success && currentTextBlock) {
    makePages();
    if (currentPage) {
      completePageFn(std::move(currentPage));
      currentPage.reset();
    }
    currentTextBlock.reset();
  }

  return success;
}
