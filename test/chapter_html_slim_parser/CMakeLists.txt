add_executable(ChapterHtmlSlimParserTest
  ChapterHtmlSlimParserTest.cpp
  ChapterHtmlSlimParserCorpusTest.cpp
  ChapterHtmlSlimParserAllocTest.cpp
  ParserLinkStubs.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${REPO_ROOT}/test/support/PlatformHost.cpp
)

# In-tree expat at device-parity flags (XML_GE=0, XML_CONTEXT_BYTES=1024): the
# html corpus pins depend on the firmware's parser, not the host system's.
target_include_directories(ChapterHtmlSlimParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/support
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/Platform
)

target_link_libraries(ChapterHtmlSlimParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(ChapterHtmlSlimParserTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  CORPUS_HTML_DIR="${REPO_ROOT}/test/corpus/html"
)

gtest_discover_tests(ChapterHtmlSlimParserTest)
