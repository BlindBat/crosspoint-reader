# Integration suite: fixture.fb2 -> Fb2 metadata -> Fb2SectionParser ->
# serialized section file -> Fb2Section::loadPage round-trip. Compiles the
# REAL Page/ParsedText/TextBlock serialization code so the round-trip is
# genuine, plus in-tree expat with device-parity flags.
add_executable(Fb2SectionCacheTest
  Fb2SectionCacheTest.cpp
  Fb2SectionGoldenTest.cpp
  Fb2SectionCacheLinkStubs.cpp
  ${REPO_ROOT}/test/support/AllocCounter.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2MetadataParser.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2CoverExtractor.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2Section.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2SectionParser.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2/Fb2XmlEncoding.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2SectionCacheTest PRIVATE
  ${REPO_ROOT}/test/support
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

target_compile_definitions(Fb2SectionCacheTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  FB2_FIXTURE_DIR="${REPO_ROOT}/test/fb2"
)

target_link_libraries(Fb2SectionCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2SectionCacheTest)
