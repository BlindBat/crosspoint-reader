#pragma once

#include <Epub/Page.h>
#include <Epub/ParsedText.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/blocks/BlockStyle.h>
#include <Epub/blocks/TextBlock.h>
#include <expat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>

class GfxRenderer;

#define FB2_MAX_WORD_SIZE 200

// Expat-based parser for FB2 section content.
// Converts FB2 tags into Pages using the same rendering pipeline as EPUB.
class Fb2SectionParser {
  const std::string& filepath;
  size_t sectionLength;
  GfxRenderer& renderer;
  const ReaderRenderSpec spec;
  std::function<void(std::unique_ptr<Page>)> completePageFn;
  std::function<void()> popupFn;

  int targetSectionIndex;
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  int topLevelSectionCount = 0;
  int targetSectionDepth = -1;  // depth at which the target section was entered
  bool inTargetSection = false;
  bool pastTargetSection = false;
  bool inBody = false;
  bool outOfMemory = false;

  char partWordBuffer[FB2_MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;

  void flushPartWordBuffer();
  void startNewTextBlock(const BlockStyle& blockStyle);
  void makePages();
  void addLineToPage(std::shared_ptr<TextBlock> line);

  static void XMLCALL startElement(void* userData, const char* name, const char** atts);
  static void XMLCALL characterData(void* userData, const char* s, int len);
  static void XMLCALL endElement(void* userData, const char* name);

 public:
  explicit Fb2SectionParser(const std::string& filepath, size_t sectionLength, int targetSectionIndex,
                            GfxRenderer& renderer, const ReaderRenderSpec& spec,
                            const std::function<void(std::unique_ptr<Page>)>& completePageFn,
                            const std::function<void()>& popupFn = nullptr)
      : filepath(filepath),
        sectionLength(sectionLength),
        renderer(renderer),
        spec(spec),
        completePageFn(completePageFn),
        popupFn(popupFn),
        targetSectionIndex(targetSectionIndex) {}

  bool parseAndBuildPages();
};
