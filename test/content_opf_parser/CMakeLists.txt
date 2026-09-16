# EPUB package-level parser suite: drives the real ContainerParser,
# ContentOpfParser, TocNavParser and TocNcxParser over in-tree expat at
# device-parity flags (XML_GE=0, XML_CONTEXT_BYTES=1024). The storage HAL is a
# stdio stub (ContentOpfParser spills its manifest to a temp .items.bin) and
# BookMetadataCache is a recording fake so the spine/TOC callbacks can be
# asserted directly. Every fixture is composed in the test source, including
# the malformed corpus (truncation, lying sizes, hostile nesting, bad encodings).
add_executable(ContentOpfParserTest
  ContainerParserTest.cpp
  ContentOpfParserTest.cpp
  TocParsersTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNcxParser.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ContentOpfParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_compile_definitions(ContentOpfParserTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(ContentOpfParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ContentOpfParserTest)
