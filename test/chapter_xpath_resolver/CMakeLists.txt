# ChapterXPathResolver suite: real resolver + real Utf8 + in-tree expat
# (compiled with the same feature flags as the firmware build, see
# xml_parser_utils), driven through an in-memory Epub stub that streams
# fixture XHTML.
add_executable(ChapterXPathResolverTest
  ChapterXPathResolverTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ChapterXPathResolverTest PRIVATE
  ${REPO_ROOT}/test/kosync_common/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ChapterXPathResolverTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(ChapterXPathResolverTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChapterXPathResolverTest)
