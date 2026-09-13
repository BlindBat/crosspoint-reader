# Compiles the real ParsedText + TextBlock pipeline (not the trimmed stubs the
# chapter_html_slim_parser suite uses) so page content is assertable, plus
# in-tree expat with device-parity flags.
add_executable(Fb2SectionParserTest
  Fb2SectionParserTest.cpp
  Fb2SectionParserLinkStubs.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2SectionParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2SectionParserTest PRIVATE
  ${REPO_ROOT}/test/fb2_common/stubs
  ${REPO_ROOT}/test/fb2_common
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Serialization
)

target_compile_definitions(Fb2SectionParserTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  FB2_FIXTURE_DIR="${REPO_ROOT}/test/fb2"
)

target_link_libraries(Fb2SectionParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2SectionParserTest)
